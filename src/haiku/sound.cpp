/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/sound.cpp
// Purpose:     wxSound class for Haiku
// Author:      wxWidgets Team (Ported for Haiku)
// Created:     2023
// Copyright:   (c) wxWidgets Team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_SOUND

#include "wx/sound.h"
#include "wx/file.h"
#include "wx/log.h"
#include "wx/intl.h"

#include <Sound.h>
#include <SoundPlayer.h>
#include <File.h>
#include <StorageKit.h>
#include <string.h>
#include <MediaDefs.h>

// File-scope static members (following sound_osx.cpp pattern)
static wxVector<wxSoundData*> s_soundsPlaying;
static wxMutex s_soundMutex;

////////////////////////////////////////////////////////////////////////////
// wxSoundData
////////////////////////////////////////////////////////////////////////////

wxSoundData::wxSoundData()
    : m_markedForDeletion(false),
      m_sound(nullptr),
      m_refCnt(1)
{
}

wxSoundData::~wxSoundData()
{
    if (m_sound)
    {
        // BSound has private destructor - use ReleaseRef()
        m_sound->ReleaseRef();
        m_sound = nullptr;
    }
}

void wxSoundData::IncRef()
{
    wxMutexLocker locker(s_soundMutex);
    m_refCnt++;
}

void wxSoundData::DecRef()
{
    wxMutexLocker locker(s_soundMutex);
    if (--m_refCnt == 0)
        delete this;
}

void wxSoundData::MarkForDeletion()
{
    m_markedForDeletion = true;
}

////////////////////////////////////////////////////////////////////////////
// Internal class to handle BSound playback via BSoundPlayer
////////////////////////////////////////////////////////////////////////////

class wxHaikuSoundPlayer : public BSoundPlayer
{
public:
    wxHaikuSoundPlayer(wxSoundData* soundData, bool loop)
        : BSoundPlayer("wxSoundPlayer"),
          m_soundData(soundData),
          m_loop(loop),
          m_offset(0),
          m_playing(true)
    {
    }
    
    virtual void HandleEvent(const media_raw_audio_format& /*format*/,
                            void* buffer, size_t bufferSize,
                            const media_raw_audio_format& /*userFormat*/)
    {
        if (!m_playing || !m_soundData || !m_soundData->m_sound)
        {
            memset(buffer, 0, bufferSize);
            return;
        }
        
        BSound* sound = m_soundData->m_sound;  // Non-const for GetDataAt()
        size_t copied = 0;
        
        while (copied < bufferSize)
        {
            size_t toCopy = bufferSize - copied;
            size_t used = 0;
            
            if (!sound->GetDataAt(m_offset, 
                                 static_cast<uint8*>(buffer) + copied, 
                                 toCopy, &used))
            {
                // End of sound
                if (m_loop)
                {
                    m_offset = 0;
                    continue;
                }
                else
                {
                    // Fill remainder with silence
                    memset(static_cast<uint8*>(buffer) + copied, 0, toCopy);
                    m_playing = false;
                    break;
                }
            }
            
            m_offset += used;
            copied += used;
        }
    }
    
    virtual status_t HandleMessage(int32 message, const void* /*data*/,
                                  size_t /*size*/)
    {
        if (message == B_MEDIA_NODE_STOPPED)
        {
            // Playback completed - notify wxSound
            wxSound::SoundStopped(m_soundData);
        }
        return B_OK;
    }
    
    bool IsPlaying() const { return m_playing; }
    
private:
    wxSoundData*    m_soundData;
    bool            m_loop;
    off_t           m_offset;
    volatile bool   m_playing;
};

////////////////////////////////////////////////////////////////////////////
// wxSound
////////////////////////////////////////////////////////////////////////////

wxSound::wxSound()
{
    Init();
}

wxSound::wxSound(const wxString& sFileName, bool isResource)
{
    Init();
    Create(sFileName, isResource);
}

wxSound::wxSound(size_t size, const void* data)
{
    Init();
    Create(size, data);
}

wxSound::~wxSound()
{
    Free();
}

void wxSound::Init()
{
    m_data = nullptr;
}

void wxSound::Free()
{
    if (m_data)
    {
        // Check if this sound is currently playing
        bool isPlaying = false;
        {
            wxMutexLocker locker(s_soundMutex);
            for (size_t i = 0; i < s_soundsPlaying.size(); i++)
            {
                if (s_soundsPlaying[i] == m_data)
                {
                    isPlaying = true;
                    break;
                }
            }
        }
        
        if (isPlaying)
        {
            // Mark for deletion after playback completes
            m_data->MarkForDeletion();
        }
        else
        {
            m_data->DecRef();
        }
        m_data = nullptr;
    }
}

bool wxSound::Create(const wxString& fileName, bool isResource)
{
    wxASSERT_MSG(!isResource, 
        wxT("Loading sound from resources is only supported on Windows"));
    
    Free();
    
    entry_ref ref;
    if (get_ref_for_path(fileName.fn_str(), &ref) != B_OK)
    {
        wxLogError(_("Couldn't find sound file '%s'."), fileName);
        return false;
    }
    
    BSound* sound = new(std::nothrow) BSound(&ref, true);
    if (!sound)
    {
        wxLogError(_("Couldn't allocate memory for sound '%s'."), fileName);
        return false;
    }
    
    if (sound->InitCheck() != B_OK)
    {
        wxLogError(_("Sound file '%s' is in unsupported format."), fileName);
        sound->ReleaseRef();
        return false;
    }
    
    m_data = new wxSoundData();
    m_data->m_sound = sound;
    
    return true;
}

bool wxSound::Create(size_t size, const void* data)
{
    wxASSERT(data != nullptr);
    
    Free();
    
    if (size < 44)
    {
        wxLogError(_("Sound data is too small to be a valid WAV file."));
        return false;
    }
    
    const wxUint8* bytes = static_cast<const wxUint8*>(data);
    
    // Basic WAV header validation
    if (memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0)
    {
        wxLogError(_("Sound data is not a valid WAV file."));
        return false;
    }
    
    // Parse format chunk
    media_raw_audio_format format;
    memset(&format, 0, sizeof(format));
    
    if (memcmp(bytes + 12, "fmt ", 4) == 0)
    {
        wxUint16 channels = *((wxUint16*)(bytes + 22));
        wxUint32 sampleRate = *((wxUint32*)(bytes + 24));
        wxUint16 bitsPerSample = *((wxUint16*)(bytes + 34));
        
        format.channel_count = channels;
        format.frame_rate = sampleRate;
        
        if (bitsPerSample == 16)
            format.format = media_raw_audio_format::B_AUDIO_INT;
        else if (bitsPerSample == 32)
            format.format = media_raw_audio_format::B_AUDIO_FLOAT;
        else if (bitsPerSample == 8)
            format.format = media_raw_audio_format::B_AUDIO_UCHAR;
        else
        {
            wxLogError(_("Unsupported bits per sample: %d"), bitsPerSample);
            return false;
        }
        
        format.byte_order = B_MEDIA_LITTLE_ENDIAN;
        format.buffer_size = 4096;
    }
    else
    {
        wxLogError(_("WAV format chunk not found."));
        return false;
    }
    
    // Find data chunk
    size_t dataOffset = 44;
    for (size_t i = 36; i < size - 8; i++)
    {
        if (memcmp(bytes + i, "data", 4) == 0)
        {
            dataOffset = i + 8;
            break;
        }
    }
    
    size_t dataSize = size - dataOffset;
    if (dataSize == 0)
    {
        wxLogError(_("No audio data found in WAV file."));
        return false;
    }
    
    void* soundData = malloc(dataSize);
    if (!soundData)
    {
        wxLogError(_("Couldn't allocate memory for sound data."));
        return false;
    }
    
    memcpy(soundData, bytes + dataOffset, dataSize);
    
    BSound* sound = new(std::nothrow) BSound(soundData, dataSize, format, true);
    if (!sound)
    {
        free(soundData);
        wxLogError(_("Couldn't allocate BSound object."));
        return false;
    }
    
    if (sound->InitCheck() != B_OK)
    {
        wxLogError(_("Sound data is in unsupported format."));
        sound->ReleaseRef();
        return false;
    }
    
    m_data = new wxSoundData();
    m_data->m_sound = sound;
    
    return true;
}

bool wxSound::DoPlay(unsigned flags) const
{
    wxCHECK_MSG(m_data && m_data->m_sound, false, 
        wxT("Attempt to play invalid sound data"));
    
    bool loop = (flags & wxSOUND_LOOP) != 0;
    bool async = (flags & wxSOUND_ASYNC) != 0;
    
    // Create sound player
    wxHaikuSoundPlayer* player = new(std::nothrow) wxHaikuSoundPlayer(m_data, loop);
    if (!player || player->InitCheck() != B_OK)
    {
        delete player;
        wxLogError(_("Couldn't initialize sound player."));
        return false;
    }
    
    player->SetVolume(1.0f);
    
    // Start the player
    status_t status = player->Start();
    if (status != B_OK)
    {
        delete player;
        wxLogError(_("Couldn't start sound player."));
        return false;
    }
    
    // Track this sound as playing for async mode
    if (async)
    {
        wxMutexLocker locker(s_soundMutex);
        s_soundsPlaying.push_back(m_data);
        m_data->IncRef();
        return true;
    }
    
    // For synchronous playback, wait for completion
    bigtime_t duration = m_data->m_sound->Duration();
    if (duration > 0)
    {
        // Poll until playback completes or is stopped
        while (player->IsPlaying())
        {
            snooze(10000); // 10ms
            if (m_data->m_markedForDeletion)
                break;
        }
    }
    
    // Stop and cleanup
    player->Stop();
    
    // Remove from playing list
    {
        wxMutexLocker locker(s_soundMutex);
        for (size_t i = 0; i < s_soundsPlaying.size(); i++)
        {
            if (s_soundsPlaying[i] == m_data)
            {
                s_soundsPlaying.erase(s_soundsPlaying.begin() + i);
                if (m_data->m_markedForDeletion)
                {
                    m_data->DecRef();
                }
                break;
            }
        }
    }
    
    delete player;
    
    return !m_data->m_markedForDeletion;
}

void wxSound::Stop()
{
    StopAll();
}

void wxSound::StopAll()
{
    wxMutexLocker locker(s_soundMutex);
    
    for (size_t i = 0; i < s_soundsPlaying.size(); i++)
    {
        s_soundsPlaying[i]->MarkForDeletion();
    }
}

bool wxSound::IsPlaying()
{
    return IsPlayingAny();
}

bool wxSound::IsPlayingAny()
{
    wxMutexLocker locker(s_soundMutex);
    return s_soundsPlaying.size() > 0;
}

void wxSound::SoundStopped(wxSoundData* data)
{
    // Called by wxHaikuSoundPlayer when playback completes
    wxMutexLocker locker(s_soundMutex);
    
    for (size_t i = 0; i < s_soundsPlaying.size(); i++)
    {
        if (s_soundsPlaying[i] == data)
        {
            s_soundsPlaying.erase(s_soundsPlaying.begin() + i);
            
            if (data->m_markedForDeletion)
            {
                data->DecRef();
            }
            break;
        }
    }
}

#endif // wxUSE_SOUND

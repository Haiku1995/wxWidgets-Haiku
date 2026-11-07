/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/sound_sdl.cpp
// Purpose:     wxSound backend using SDL (Haiku)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_SOUND && wxUSE_LIBSDL

#include <SDL.h>

#ifndef WX_PRECOMP
    #include "wx/event.h"
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/utils.h"
    #include "wx/module.h"
#endif

#include "wx/thread.h"
#include "wx/sound.h"

// ----------------------------------------------------------------------------
// Notification event (same pattern as your Unix version)
// ----------------------------------------------------------------------------

static inline void ZeroMemory(void* ptr, std::size_t size)
{
    std::memset(ptr, 0, size);
}

template <typename T>
inline void wxZeroMemory(T& obj)
{
    // Cast is needed just to avoid clang "nontrivial-memcall" warning.
    ::ZeroMemory(static_cast<void*>(&obj), sizeof(obj));
}
class wxSoundBackendSDLNotification : public wxEvent
{
public:
    wxDECLARE_DYNAMIC_CLASS(wxSoundBackendSDLNotification);
    wxSoundBackendSDLNotification();
    wxEvent *Clone() const override { return new wxSoundBackendSDLNotification(*this); }
};

typedef void (wxEvtHandler::*wxSoundBackendSDLNotificationFunction)
             (wxSoundBackendSDLNotification&);

wxDECLARE_EVENT(wxEVT_SOUND_BACKEND_SDL_NOTIFICATION, wxSoundBackendSDLNotification);

#define EVT_SOUND_BACKEND_SDL_NOTIFICATON(func) \
    DECLARE_EVENT_TABLE_ENTRY(wxEVT_SOUND_BACKEND_SDL_NOTIFICATION, \
                              -1,                       \
                              -1,                       \
                              wxEVENT_HANDLER_CAST( wxSoundBackendSDLNotificationFunction, func ), \
                              nullptr ),

wxIMPLEMENT_DYNAMIC_CLASS(wxSoundBackendSDLNotification, wxEvtHandler);
wxDEFINE_EVENT( wxEVT_SOUND_BACKEND_SDL_NOTIFICATION, wxSoundBackendSDLNotification );

wxSoundBackendSDLNotification::wxSoundBackendSDLNotification()
{
    SetEventType(wxEVT_SOUND_BACKEND_SDL_NOTIFICATION);
}

class wxSoundBackendSDL;
class wxSoundBackendSDLEvtHandler : public wxEvtHandler
{
public:
    explicit wxSoundBackendSDLEvtHandler(wxSoundBackendSDL *bk) : m_backend(bk) {}

private:
    void OnNotify(wxSoundBackendSDLNotification& WXUNUSED(event));

    wxSoundBackendSDL *m_backend;
    wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(wxSoundBackendSDLEvtHandler, wxEvtHandler)
    EVT_SOUND_BACKEND_SDL_NOTIFICATON(wxSoundBackendSDLEvtHandler::OnNotify)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// Backend
// ----------------------------------------------------------------------------

class wxSoundBackendSDL : public wxSoundBackend
{
public:
    wxSoundBackendSDL()
        : m_initialized(false),
          m_playing(false),
          m_audioOpen(false),
          m_data(nullptr),
          m_pos(0),
          m_loop(false),
          m_evtHandler(nullptr)
#if SDL_MAJOR_VERSION >= 2
        , m_device(0)
#endif
    {
        wxZeroMemory(m_spec);
    }

    ~wxSoundBackendSDL() override
    {
        Stop();
        CloseAudio();
        delete m_evtHandler;
    }

    wxString GetName() const override { return wxT("Simple DirectMedia Layer"); }
    int GetPriority() const override { return 9; }
    bool HasNativeAsyncPlayback() const override { return true; }

    bool IsAvailable() const override
    {
        if (m_initialized)
            return true;

#if SDL_MAJOR_VERSION >= 2
        // SDL2: init audio subsystem (safe even if SDL already initialized elsewhere)
        if (SDL_WasInit(SDL_INIT_AUDIO) != SDL_INIT_AUDIO)
        {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
                return false;
        }
#else
        if (SDL_WasInit(SDL_INIT_AUDIO) != SDL_INIT_AUDIO)
        {
            if (SDL_Init(SDL_INIT_AUDIO) == -1)
                return false;
        }
#endif

        wxConstCast(this, wxSoundBackendSDL)->m_initialized = true;
        wxLogTrace(wxT("sound"), wxT("initialized SDL audio subsystem"));
        return true;
    }

    bool Play(wxSoundData *data, unsigned flags,
              volatile wxSoundPlaybackStatus *WXUNUSED(status)) override
    {
        Stop();

        SDL_AudioFormat format;
        if (data->m_bitsPerSample == 8)
            format = AUDIO_U8;
        else if (data->m_bitsPerSample == 16)
            format = AUDIO_S16SYS; // don’t hardcode endianness
        else
            return false;

        bool needsOpen = true;
        if (m_audioOpen)
        {
            if (format == m_spec.format &&
                m_spec.freq == (int)data->m_samplingRate &&
                m_spec.channels == data->m_channels)
            {
                needsOpen = false;
            }
            else
            {
                CloseAudio();
            }
        }

        if (needsOpen)
        {
            m_spec.format   = format;
            m_spec.freq     = (int)data->m_samplingRate;
            m_spec.channels = (Uint8)data->m_channels;
            m_spec.samples  = 4096;
            m_spec.callback = &wx_sdl_audio_callback;
            m_spec.userdata = this;

            if (!OpenAudio())
                return false;
        }

        SDL_LockAudio();
        wxLogTrace(wxT("sound"), wxT("playing new sound"));
        m_playing = true;
        m_pos = 0;
        m_loop = (flags & wxSOUND_LOOP) != 0;
        m_data = data;
        data->IncRef();
        SDL_UnlockAudio();

        PauseAudio(false);

        if (!(flags & wxSOUND_ASYNC))
        {
            wxLogTrace(wxT("sound"), wxT("waiting for sample to finish"));
            while (m_playing && m_data == data)
            {
#if wxUSE_THREADS
                if (wxThread::IsMain())
                    wxMutexGuiLeave();
#endif
                wxMilliSleep(10);
#if wxUSE_THREADS
                if (wxThread::IsMain())
                    wxMutexGuiEnter();
#endif
            }
            wxLogTrace(wxT("sound"), wxT("sample finished"));
        }

        return true;
    }

    void Stop() override
    {
        SDL_LockAudio();
        PauseAudio(true);
        m_playing = false;

        if (m_data)
        {
            m_data->DecRef();
            m_data = nullptr;
        }

        SDL_UnlockAudio();
    }

    bool IsPlaying() const override { return m_playing; }

    void FillAudioBuffer(Uint8 *stream, int len)
    {
        if (m_playing)
        {
            if (m_pos == m_data->m_dataBytes)
            {
                m_playing = false;

                // notify main thread (don’t tear down audio from callback)
                if (m_evtHandler)
                {
                    wxSoundBackendSDLNotification event;
                    m_evtHandler->AddPendingEvent(event);
                }
            }
            else
            {
                unsigned size = ((unsigned)len + m_pos < m_data->m_dataBytes)
                              ? (unsigned)len
                              : (m_data->m_dataBytes - m_pos);

                memcpy(stream, m_data->m_data + m_pos, size);
                m_pos += size;
                len -= (int)size;
                stream += size;
            }
        }

        if (len > 0)
        {
            if (m_loop)
            {
                m_pos = 0;
                FillAudioBuffer(stream, len);
            }
            else
            {
                // m_spec.silence is valid after open
                memset(stream, m_spec.silence, len);
            }
        }
    }

    void FinishedPlayback()
    {
        if (!m_playing)
            Stop();
    }

private:
    static void wx_sdl_audio_callback(void *userdata, Uint8 *stream, int len)
    {
        static_cast<wxSoundBackendSDL*>(userdata)->FillAudioBuffer(stream, len);
    }

    void PauseAudio(bool pause)
    {
#if SDL_MAJOR_VERSION >= 2
        if (m_device != 0)
            SDL_PauseAudioDevice(m_device, pause ? 1 : 0);
#else
        SDL_PauseAudio(pause ? 1 : 0);
#endif
    }

    bool OpenAudio()
    {
        if (m_audioOpen)
            return true;

        if (!m_evtHandler)
            m_evtHandler = new wxSoundBackendSDLEvtHandler(this);

#if SDL_MAJOR_VERSION >= 2
        SDL_AudioSpec obtained;
        wxZeroMemory(obtained);

        wxLogTrace(wxT("sound"), wxT("opening SDL2 audio device..."));
        m_device = SDL_OpenAudioDevice(nullptr, 0, &m_spec, &obtained, 0);
        if (m_device == 0)
        {
            wxString err(SDL_GetError(), wxConvLocal);
            wxLogError(_("Couldn't open audio: %s"), err);
            return false;
        }

        // Use obtained spec (SDL may adjust samples/freq/etc)
        m_spec = obtained;
        m_audioOpen = true;
        return true;
#else
        wxLogTrace(wxT("sound"), wxT("opening SDL audio..."));
        if (SDL_OpenAudio(&m_spec, nullptr) >= 0)
        {
            m_audioOpen = true;
            return true;
        }
        else
        {
            wxString err(SDL_GetError(), wxConvLocal);
            wxLogError(_("Couldn't open audio: %s"), err);
            return false;
        }
#endif
    }

    void CloseAudio()
    {
        if (!m_audioOpen)
            return;

#if SDL_MAJOR_VERSION >= 2
        if (m_device != 0)
        {
            SDL_CloseAudioDevice(m_device);
            m_device = 0;
        }
#else
        SDL_CloseAudio();
#endif
        wxLogTrace(wxT("sound"), wxT("closed audio"));
        m_audioOpen = false;
    }

private:
    bool                        m_initialized;
    bool                        m_playing;
    bool                        m_audioOpen;

    wxSoundData                *m_data;
    unsigned                    m_pos;
    SDL_AudioSpec               m_spec;
    bool                        m_loop;

    wxSoundBackendSDLEvtHandler *m_evtHandler;

#if SDL_MAJOR_VERSION >= 2
    SDL_AudioDeviceID           m_device;
#endif

    friend class wxSoundBackendSDLEvtHandler;
};

void wxSoundBackendSDLEvtHandler::OnNotify(wxSoundBackendSDLNotification& WXUNUSED(event))
{
    wxLogTrace(wxT("sound"), wxT("received playback status change notification"));
    m_backend->FinishedPlayback();
}

extern "C" wxSoundBackend *wxCreateSoundBackendSDL()
{
    return new wxSoundBackendSDL();
}

#endif // wxUSE_SOUND && wxUSE_LIBSDL

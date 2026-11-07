/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/mediactrl.cpp
// Purpose:     wxMediaCtrl backend for Haiku using libVLC
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"
#include "wx/mediactrl.h"
#include "wx/log.h"
#include "wx/timer.h"
#include "wx/filefn.h"
#include "wx/filename.h"
#include <View.h>
#include <Window.h>
#include <Bitmap.h>
#include <Locker.h>
#include <vlc/vlc.h>
#include <atomic>
#include <memory>

// VLC decoder threads only fill the bitmap. Native drawing occurs on the
// window looper, and wx notifications occur on the serialized GUI timer.
class wxHaikuVLCView : public BView
{
public:
    wxHaikuVLCView(BBitmap* bitmap, BLocker* lock)
        : BView(BRect(0, 0, 1, 1), "wxVLCView", B_FOLLOW_ALL,
                B_WILL_DRAW), m_bitmap(bitmap), m_lock(lock)
    { SetViewColor(0, 0, 0); }
    void Draw(BRect) override
    {
        if (m_bitmap && m_lock->Lock())
        {
            DrawBitmap(m_bitmap, m_bitmap->Bounds(), Bounds());
            m_lock->Unlock();
        }
    }
private:
    BBitmap* m_bitmap;
    BLocker* m_lock;
};

class WXDLLIMPEXP_MEDIA wxHAIKUMediaBackend : public wxMediaBackendCommonBase
{
public:
    wxHAIKUMediaBackend();
    ~wxHAIKUMediaBackend() override;
    using wxMediaBackend::Load;
    bool CreateControl(wxControl*, wxWindow*, wxWindowID, const wxPoint&,
                       const wxSize&, long, const wxValidator&, const wxString&) override;
    bool Load(const wxString& file) override;
    bool Load(const wxURI& uri) override;
    bool Play() override;
    bool Pause() override;
    bool Stop() override;
    wxMediaState GetState() override;
    bool SetPosition(wxLongLong where) override;
    wxLongLong GetPosition() override;
    wxLongLong GetDuration() override;
    bool SetVolume(double volume) override;
    double GetVolume() override { return m_volume; }
    bool SetPlaybackRate(double rate) override;
    double GetPlaybackRate() override { return m_rate; }
    void Move(int, int, int w, int h) override;
    wxSize GetVideoSize() const override;
private:
    class PollTimer : public wxTimer
    {
    public:
        explicit PollTimer(wxHAIKUMediaBackend* backend) : m_backend(backend) {}
        void Notify() override { m_backend->Poll(); }
    private:
        wxHAIKUMediaBackend* m_backend;
    };
    void Poll();
    bool OpenMedia(const wxString& source, bool local);
    void CloseMedia();
    static void* Lock(void* opaque, void** planes);
    static void Unlock(void* opaque, void*, void* const*);
    static void Display(void* opaque, void*);
    libvlc_instance_t* m_vlc = nullptr;
    libvlc_media_t* m_media = nullptr;
    libvlc_media_player_t* m_player = nullptr;
    BView* m_view = nullptr;
    BBitmap* m_bitmap = nullptr;
    BLocker m_bitmapLock;
    PollTimer m_timer;
    std::atomic<bool> m_frameReady{false};
    bool m_loading = false;
    bool m_errorReported = false;
    wxMediaState m_eventState = wxMEDIASTATE_STOPPED;
    double m_volume = 1.0;
    double m_rate = 1.0;
    int m_width = 640;
    int m_height = 480;
    wxDECLARE_DYNAMIC_CLASS(wxHAIKUMediaBackend);
};
wxIMPLEMENT_DYNAMIC_CLASS(wxHAIKUMediaBackend, wxMediaBackend);

wxHAIKUMediaBackend::wxHAIKUMediaBackend() : m_timer(this)
{
    m_ctrl = nullptr;
    m_vlc = libvlc_new(0, nullptr);
}
wxHAIKUMediaBackend::~wxHAIKUMediaBackend()
{
    CloseMedia(); // joins VLC workers before touching their bitmap/view
    if (m_view)
    {
        BWindow* window = m_view->Window();
        if (!window || window->LockLooper())
        {
            m_view->RemoveSelf();
            delete m_view;
            if (window) window->UnlockLooper();
        }
    }
    delete m_bitmap;
    if (m_vlc) libvlc_release(m_vlc);
}
bool wxHAIKUMediaBackend::CreateControl(wxControl* ctrl, wxWindow* parent,
        wxWindowID id, const wxPoint& pos, const wxSize& size, long style,
        const wxValidator& validator, const wxString& name)
{
    if (!m_vlc)
    {
        wxLogError("libVLC initialization failed");
        return false;
    }
    m_ctrl = dynamic_cast<wxMediaCtrl*>(ctrl);
    if (!m_ctrl || !ctrl->Create(parent, id, pos, size, style, validator, name))
        return false;
    m_bitmap = new BBitmap(BRect(0,0,m_width-1,m_height-1), B_RGB32);
    if (m_bitmap->InitCheck() != B_OK) return false;
    m_bitmapLock.Lock();
    memset(m_bitmap->Bits(), 0, m_bitmap->BitsLength());
    m_bitmapLock.Unlock();
    m_view = new wxHaikuVLCView(m_bitmap, &m_bitmapLock);
    BView* native = ctrl->GetHandle();
    if (!native) return false;
    BWindow* window = native->Window();
    if (window && !window->LockLooper()) return false;
    native->AddChild(m_view);
    m_view->MoveTo(0,0);
    m_view->ResizeTo(native->Bounds().Width(), native->Bounds().Height());
    if (window) window->UnlockLooper();
    return true;
}
void* wxHAIKUMediaBackend::Lock(void* opaque, void** planes)
{
    auto* self = static_cast<wxHAIKUMediaBackend*>(opaque);
    self->m_bitmapLock.Lock();
    *planes = self->m_bitmap->Bits();
    return nullptr;
}
void wxHAIKUMediaBackend::Unlock(void* opaque, void*, void* const*)
{ static_cast<wxHAIKUMediaBackend*>(opaque)->m_bitmapLock.Unlock(); }
void wxHAIKUMediaBackend::Display(void* opaque, void*)
{ static_cast<wxHAIKUMediaBackend*>(opaque)->m_frameReady = true; }

bool wxHAIKUMediaBackend::Load(const wxString& file)
{
    if (!wxFileExists(file)) { CloseMedia(); return false; }
    wxFileName path(file);
    path.MakeAbsolute();
    return OpenMedia(path.GetFullPath(), true);
}
bool wxHAIKUMediaBackend::Load(const wxURI& uri)
{
    if (uri.GetScheme().CmpNoCase("file") == 0)
    {
        if (!uri.GetServer().empty() && uri.GetServer().CmpNoCase("localhost") != 0)
            return false;
        // wxURI normalizes file:/// to file:/; VLC expects a canonical MRL.
        // Decode file URLs and use the path API, preserving spaces and UTF-8.
        return Load(wxURI::Unescape(uri.GetPath()));
    }
    return OpenMedia(uri.BuildURI(), false);
}

bool wxHAIKUMediaBackend::OpenMedia(const wxString& source, bool local)
{
    CloseMedia();
    if (!m_vlc || source.empty()) return false;
    const wxScopedCharBuffer utf8 = source.utf8_str();
    // new_location expects an MRL, not a filesystem path. new_path also
    // handles spaces and non-ASCII filenames without manual URI escaping.
    m_media = local ? libvlc_media_new_path(m_vlc, utf8.data())
                    : libvlc_media_new_location(m_vlc, utf8.data());
    if (!m_media) return false;
    m_player = libvlc_media_player_new_from_media(m_media);
    if (!m_player) { CloseMedia(); return false; }
    libvlc_video_set_callbacks(m_player, Lock, Unlock, Display, this);
    libvlc_video_set_format(m_player, "RV32", m_width, m_height,
                           m_bitmap->BytesPerRow());
    const int flags = local || source.StartsWith("file:")
        ? libvlc_media_parse_local : libvlc_media_parse_network;
    if (libvlc_media_parse_with_options(m_media,
            static_cast<libvlc_media_parse_flag_t>(flags), 5000) != 0)
    { CloseMedia(); return false; }
    m_loading = true;
    m_errorReported = false;
    m_eventState = wxMEDIASTATE_STOPPED;
    if (!m_timer.Start(50)) { CloseMedia(); return false; }
    return true;
}
void wxHAIKUMediaBackend::CloseMedia()
{
    m_timer.Stop();
    m_loading = false;
    if (m_player)
    {
        libvlc_media_player_stop(m_player);
        libvlc_media_player_release(m_player);
        m_player = nullptr;
    }
    if (m_media)
    {
        libvlc_media_parse_stop(m_media);
        libvlc_media_release(m_media);
        m_media = nullptr;
    }
    m_frameReady = false;
    m_eventState = wxMEDIASTATE_STOPPED;
}
void wxHAIKUMediaBackend::Poll()
{
    if (!m_player || !m_media) return;
    if (m_loading)
    {
        const auto parsed = libvlc_media_get_parsed_status(m_media);
        if (parsed == libvlc_media_parsed_status_done)
        {
            m_loading = false;
            m_ctrl->InvalidateBestSize();
            NotifyMovieLoaded();
        }
        else if (parsed == libvlc_media_parsed_status_failed ||
                 parsed == libvlc_media_parsed_status_timeout ||
                 parsed == libvlc_media_parsed_status_skipped)
        {
            // Some stream formats cannot be preparsed but can still play.
            m_loading = false;
            m_ctrl->InvalidateBestSize();
            NotifyMovieLoaded();
        }
    }
    const auto nativeState = libvlc_media_player_get_state(m_player);
    const auto state = GetState();
    if (state != m_eventState)
    {
        m_eventState = state;
        if (state == wxMEDIASTATE_PLAYING) QueuePlayEvent();
        else if (state == wxMEDIASTATE_PAUSED) QueuePauseEvent();
        else if (nativeState == libvlc_Ended)
        {
            if (SendStopEvent()) QueueFinishEvent();
            else { libvlc_media_player_stop(m_player); Play(); }
        }
        else QueueStopEvent();
    }
    if (nativeState == libvlc_Error && !m_errorReported)
    {
        m_errorReported = true;
        wxLogError("libVLC could not open or decode the selected media");
    }
    if (m_view && m_frameReady.exchange(false))
    {
        BWindow* window = m_view->Window();
        if (!window || window->LockLooper())
        {
            m_view->Invalidate();
            if (window) window->UnlockLooper();
        }
    }
}
bool wxHAIKUMediaBackend::Play()
{
    if (!m_player || libvlc_media_player_play(m_player) != 0) return false;
    libvlc_audio_set_volume(m_player, static_cast<int>(m_volume*100));
    libvlc_media_player_set_rate(m_player, static_cast<float>(m_rate));
    return true;
}
bool wxHAIKUMediaBackend::Pause()
{
    if (!m_player || !libvlc_media_player_can_pause(m_player)) return false;
    libvlc_media_player_set_pause(m_player, 1);
    return true;
}
bool wxHAIKUMediaBackend::Stop()
{
    if (!m_player) return false;
    libvlc_media_player_stop(m_player);
    if (m_eventState != wxMEDIASTATE_STOPPED)
    { m_eventState = wxMEDIASTATE_STOPPED; QueueStopEvent(); }
    return true;
}
wxMediaState wxHAIKUMediaBackend::GetState()
{
    if (!m_player) return wxMEDIASTATE_STOPPED;
    switch (libvlc_media_player_get_state(m_player))
    {
        case libvlc_Playing: return wxMEDIASTATE_PLAYING;
        case libvlc_Paused: return wxMEDIASTATE_PAUSED;
        default: return wxMEDIASTATE_STOPPED;
    }
}
bool wxHAIKUMediaBackend::SetPosition(wxLongLong where)
{
    if (!m_player || !libvlc_media_player_is_seekable(m_player) || where < 0)
        return false;
    libvlc_media_player_set_time(m_player, where.GetValue());
    return true;
}
wxLongLong wxHAIKUMediaBackend::GetPosition()
{
    const auto time = m_player ? libvlc_media_player_get_time(m_player) : 0;
    return wxLongLong(time > 0 ? time : 0);
}
wxLongLong wxHAIKUMediaBackend::GetDuration()
{
    const auto duration = m_media ? libvlc_media_get_duration(m_media) : 0;
    return wxLongLong(duration > 0 ? duration : 0);
}
bool wxHAIKUMediaBackend::SetVolume(double volume)
{
    m_volume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
    if (m_player) libvlc_audio_set_volume(m_player, static_cast<int>(m_volume*100));
    return true;
}
bool wxHAIKUMediaBackend::SetPlaybackRate(double rate)
{
    if (rate <= 0) return false;
    if (m_player && libvlc_media_player_set_rate(m_player,
            static_cast<float>(rate)) != 0) return false;
    m_rate = rate;
    return true;
}
void wxHAIKUMediaBackend::Move(int, int, int w, int h)
{
    if (!m_view) return;
    BWindow* window = m_view->Window();
    if (!window || window->LockLooper())
    {
        // The video view is a child of the control, so its origin is local.
        m_view->MoveTo(0,0);
        m_view->ResizeTo(w > 0 ? w-1 : 0, h > 0 ? h-1 : 0);
        if (window) window->UnlockLooper();
    }
}
wxSize wxHAIKUMediaBackend::GetVideoSize() const
{
    // video_get_size reports the scaled callback output, not the source size.
    if (m_media)
    {
        libvlc_media_track_t** tracks = nullptr;
        const unsigned count = libvlc_media_tracks_get(m_media, &tracks);
        wxSize size(m_width, m_height);
        for (unsigned i = 0; i < count; ++i)
        {
            if (tracks[i]->i_type == libvlc_track_video && tracks[i]->video &&
                tracks[i]->video->i_width && tracks[i]->video->i_height)
            {
                size = wxSize(tracks[i]->video->i_width, tracks[i]->video->i_height);
                break;
            }
        }
        libvlc_media_tracks_release(tracks, count);
        return size;
    }
    return wxSize(m_width,m_height);
}
#include "wx/html/forcelnk.h"
FORCE_LINK_ME(basewxmediabackends)



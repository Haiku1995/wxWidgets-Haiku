// Scoped locking for native control access from wxWidgets threads.
#ifndef WX_HAIKU_CONTROL_LOCK_H
#define WX_HAIKU_CONTROL_LOCK_H
#include <View.h>
#include <Looper.h>
class wxHaikuControlLock
{
public:
    explicit wxHaikuControlLock(BView* view)
        : m_looper(view ? view->Looper() : nullptr),
          m_locked(m_looper && m_looper->Lock()) {}
    ~wxHaikuControlLock() { if (m_locked) m_looper->Unlock(); }
    bool IsOk() const { return !m_looper || m_locked; }
    wxHaikuControlLock(const wxHaikuControlLock&) = delete;
    wxHaikuControlLock& operator=(const wxHaikuControlLock&) = delete;
private:
    BLooper* m_looper;
    bool m_locked;
};
#endif

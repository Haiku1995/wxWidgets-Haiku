/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/sockhaiku.cpp
// Purpose:     Socket event handling implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/apptrait.h"
#include "wx/log.h"
#include "wx/private/fdiomanager.h"
#include "wx/app.h"

#include <sys/select.h>
#include <unistd.h>
#include <errno.h>

class wxFDIOHandler;

class HaikuFDIOManager : public wxFDIOManager
{
public:
    virtual int AddInput(wxFDIOHandler* handler, int fd, Direction d) override;
    virtual void RemoveInput(wxFDIOHandler* handler, int fd, Direction d) override;

    // Called by wxApp to check for socket events
    void ProcessEvents();

private:
    struct Entry
    {
        wxFDIOHandler* handler;
        int fd;
        Direction dir;
    };

    wxVector<Entry> m_entries;
};


// ----------------------------------------------------------------------------
// wxHaikuFDIOManager
// ----------------------------------------------------------------------------

int HaikuFDIOManager::AddInput(wxFDIOHandler* handler, int fd, Direction d)
{
    Entry entry;
    entry.handler = handler;
    entry.fd = fd;
    entry.dir = d;

    m_entries.push_back(entry);
    handler->SetRegisteredEvent(d);

    // Notify app that we have socket interest
    wxTheApp->WakeUpIdle();

    return fd;
}

void HaikuFDIOManager::RemoveInput(wxFDIOHandler* handler, int fd, Direction d)
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->handler == handler && it->fd == fd && it->dir == d)
        {
            m_entries.erase(it);
            break;
        }
    }
}

void HaikuFDIOManager::ProcessEvents()
{
    if (m_entries.empty())
        return;

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    int max_fd = 0;

    for (const auto& entry : m_entries)
    {
        if (entry.dir == INPUT)
            FD_SET(entry.fd, &read_fds);
        else
            FD_SET(entry.fd, &write_fds);

        if (entry.fd > max_fd)
            max_fd = entry.fd;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int ret = select(max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);
    if (ret < 0)
    {
        if (errno != EINTR)
            wxLogSysError("select() failed");
        return;
    }

    for (const auto& entry : m_entries)
    {
        bool ready = false;
        if (entry.dir == INPUT && FD_ISSET(entry.fd, &read_fds))
            ready = true;
        else if (entry.dir == OUTPUT && FD_ISSET(entry.fd, &write_fds))
            ready = true;

        if (ready)
        {
            if (entry.dir == INPUT)
                entry.handler->OnReadWaiting();
            else
                entry.handler->OnWriteWaiting();
        }
    }
}
wxFDIOManager *wxGUIAppTraits::GetFDIOManager()
{
    static HaikuFDIOManager s_manager;
    return &s_manager;
}

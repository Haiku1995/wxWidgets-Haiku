/////////////////////////////////////////////////////////////////////////////
// Name:        include/wx/haiku/app.h
// Purpose:     wxApp class for Haiku OS, integrating with BApplication
// Author:      Adapted for Haiku by xAI
// Created:     2025-05-02
// Copyright:   (c) 2025 wxWidgets dev team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_APP_H_
#define _WX_HAIKU_APP_H_


#include <memory>

class BApplication;
class HaikuFDIOManager;

class WXDLLIMPEXP_CORE wxApp : public wxAppBase
{
public:
    wxApp();
    virtual ~wxApp();

    // wxAppBase overrides
    virtual bool Initialize(int& argc, wxChar** argv) override;
    virtual void CleanUp() override;
    virtual void WakeUpIdle() override;

    // Additional overrides needed for full functionality
    virtual int OnExit() override;
    virtual int OnRun() override;
    virtual void ExitMainLoop() override;
    virtual void OnEventLoopEnter(wxEventLoopBase* loop) override;
    virtual void OnEventLoopExit(wxEventLoopBase* loop) override;
    virtual wxAppTraits* CreateTraits() override;
    virtual void DoSetAppName() ;//override;
    virtual void DoSetVendorName() ;//override;
    virtual bool ProcessIdle() override;

    // Haiku-specific
    void ProcessSocketEvents();

    // Accessor for BApplication (optional)
    BApplication* GetBApplication() const { return m_bApp.get(); }

private:
    std::unique_ptr<BApplication> m_bApp;           // ✅ renamed to match .cpp usage
    HaikuFDIOManager* m_fdioManager;              // ✅ declared

    wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxApp);
};

#endif // _WX_HAIKU_APP_H_

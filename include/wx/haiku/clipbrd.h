#ifndef _WX_HAIKU_CLIPBRD_H_
#define _WX_HAIKU_CLIPBRD_H_


#include <Clipboard.h>

class WXDLLIMPEXP_CORE wxClipboard : public wxClipboardBase
{
public:
    wxClipboard() ;
    virtual ~wxClipboard();

    virtual bool Open() override;
    virtual void Close() override;
    virtual bool IsOpened() const override; 
    virtual bool AddData(wxDataObject* data) override;
    virtual bool SetData(wxDataObject* data) override;
    virtual bool IsSupported(const wxDataFormat& format) override;
    virtual bool GetData(wxDataObject& data) override;
    virtual void Clear() override;
    virtual bool IsSupportedAsync(wxEvtHandler* sink) override;

private:
    BClipboard* m_bClipboard;
  bool m_isOpened ;
    wxDECLARE_DYNAMIC_CLASS(wxClipboard);
};

#endif // _WX_HAIKU_CLIPBRD_H_

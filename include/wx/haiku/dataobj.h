// wx/haiku/dataobj.h
#ifndef _WX_HAIKU_DATAOBJ_H_
#define _WX_HAIKU_DATAOBJ_H_

//#include "wx/dataobj.h"
#include <Clipboard.h>  // Haiku's clipboard API
#include <Message.h>    // For BMessage handling
#include <vector>

// Haiku-specific wxDataObject implementation
class BMessage;

class WXDLLIMPEXP_CORE wxDataObject : public wxDataObjectBase
{
public:
    wxDataObject();
    virtual ~wxDataObject();

    bool IsSupportedFormat(const wxDataFormat& format,
                           Direction dir = Get) const ;

    virtual void HaikuAddDataTo(BMessage& msg) const;
    virtual bool HaikuSetDataFrom(const BMessage& msg);

protected:
    static wxString WxFormatToMime(const wxDataFormat& format);
    static wxDataFormat MimeToWxFormat(const char* mime);

private:
    virtual void HaikuSetDataSingleFormat(const BMessage& msg,
                                          const wxDataFormat& format);
};

#endif // _WX_HAIKU_DATAOBJ_H_

// In wx/haiku/private/utils.h
#ifndef _WX_HAIKU_PRIVATE_UTILS_H_
#define _WX_HAIKU_PRIVATE_UTILS_H_

#include <interface/View.h>
#include <map>
#include "wx/window.h"

void wxMissingImplementation(const char fileName[], unsigned lineNumber,
    const char feature[]);

#define wxMISSING_IMPLEMENTATION(feature)\
    wxMissingImplementation(__FILE__, __LINE__, feature)

#define wxMISSING_FUNCTION() \
    wxMISSING_IMPLEMENTATION(__func__)

class wxWindowHaikuUtils
{
public:
    static void HaikuStoreWindowPointer(BView *view, wxWindow *window)
    {
        if (view && window)
            s_viewWindowMap[view] = window;
    }
    
    static wxWindow* HaikuRetrieveWindowPointer(const BView *view)
    {
        if (view)
        {
            auto it = s_viewWindowMap.find(const_cast<BView*>(view));
            if (it != s_viewWindowMap.end())
                return it->second;
        }
        return nullptr;
    }
    
    static void HaikuRemoveWindowPointer(BView *view)
    {
        if (view)
            s_viewWindowMap.erase(view);
    }

private:
    // DECLARATION ONLY - definition goes in .cpp file
    static std::map<BView*, wxWindow*> s_viewWindowMap;
};

// REMOVE THIS LINE - it causes multiple definitions!
// std::map<BView*, wxWindow*> wxWindowHaikuUtils::s_viewWindowMap;

#endif // _WX_HAIKU_PRIVATE_UTILS_H_

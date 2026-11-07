// Shared ordering for sorted native item controls.
// Licence: wxWindows licence
#ifndef _WX_HAIKU_CONTROL_SORT_H_
#define _WX_HAIKU_CONTROL_SORT_H_
#include "wx/string.h"
inline int wxHaikuCompareItemLabels(const wxString& left, const wxString& right)
{
    const int folded = left.CmpNoCase(right);
    return folded ? folded : left.Cmp(right);
}
#endif

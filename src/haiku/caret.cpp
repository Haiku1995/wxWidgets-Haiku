///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/caret.cpp
// Purpose:     Haiku OS implementation of wxCaret
// Author:      Ported by User
// Created:     2026-01-26
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx//caret.h"
#include "wx/window.h"
#include "wx/log.h"
#include "InterfaceDefs.h"

#if wxUSE_CARET

void wxCaret::OnSetFocus()
{
    if ( m_countVisible > 0 )
    {
        DoShow();
    }
}

void wxCaret::OnKillFocus()
{
    DoHide();
}

void wxCaret::DoShow()
{
    if ( !GetWindow() )
        return;

    m_visible = true;
    // In Haiku, caret is drawn manually by Invalidate() + Draw()
    GetWindow()->Refresh(); 
}

void wxCaret::DoHide()
{
    if ( !GetWindow() )
        return;

    m_visible = false;
    GetWindow()->Refresh();
}

void wxCaret::DoMove()
{
    if ( !GetWindow() )
        return;

    // The position is updated in the window; redraw
    if ( m_visible )
        GetWindow()->Refresh();
}

void wxCaret::DoSize()
{
    if ( !GetWindow() )
        return;

    // Just force redraw
    if ( m_visible )
        GetWindow()->Refresh();
}



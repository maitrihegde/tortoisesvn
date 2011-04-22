// TortoiseMerge - a Diff/Patch program

// Copyright (C) 2006-2011 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "StdAfx.h"
#include "Resource.h"
#include "AppUtils.h"

#include "RightView.h"
#include "BottomView.h"

IMPLEMENT_DYNCREATE(CRightView, CBaseView)

CRightView::CRightView(void)
{
    m_pwndRight = this;
    m_pState = &m_AllState.right;
    m_nStatusBarID = ID_INDICATOR_RIGHTVIEW;
}

CRightView::~CRightView(void)
{
}

void CRightView::UseBothLeftFirst()
{
    if (!HasSelection())
        return;

    int nFirstViewLine = m_Screen2View[m_nSelBlockStart]; // first view line in selection
    int nLastViewLine = m_Screen2View[m_nSelBlockEnd]; // last view line in selection
    int nNextViewLine = nLastViewLine + 1; // first view line after selected block

    CUndo::GetInstance().BeginGrouping();

    // right original become added
    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        if (!IsStateEmpty(GetViewState(viewLine)))
        {
            SetViewState(viewLine, DIFFSTATE_YOURSADDED);
        }
    }
    SaveUndoStep();

    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        viewdata line = m_pwndLeft->GetViewData(viewLine);
        if (IsStateEmpty(line.state))
        {
            line.state = DIFFSTATE_EMPTY;
        }
        else
        {
            line.state = DIFFSTATE_THEIRSADDED;
        }
        InsertViewData(viewLine, line);
    }

    // now insert an empty block in left view
    int nCount = nLastViewLine - nFirstViewLine + 1;
    m_pwndLeft->InsertViewEmptyLines(nNextViewLine, nCount);
    SaveUndoStep();
    CleanEmptyLines();
    SaveUndoStep();
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    BuildAllScreen2ViewVector();
    RecalcAllVertScrollBars();
    m_pwndLeft->SetModified();
    SetModified();
    RefreshViews();
}

void CRightView::UseBothRightFirst()
{
    if (!HasSelection())
        return;

    int nFirstViewLine = m_Screen2View[m_nSelBlockStart]; // first view line in selection
    int nLastViewLine = m_Screen2View[m_nSelBlockEnd]; // last view line in selection
    int nNextViewLine = nLastViewLine + 1; // first view line after selected block

    CUndo::GetInstance().BeginGrouping();

    // right original become added
    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        if (!IsStateEmpty(GetViewState(viewLine)))
        {
            SetViewState(viewLine, DIFFSTATE_ADDED);
        }
    }
    SaveUndoStep();

    // your block is done, now insert their block
    int viewindex = nNextViewLine;
    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        viewdata line = m_pwndLeft->GetViewData(viewLine);
        if (IsStateEmpty(line.state))
        {
            line.state = DIFFSTATE_EMPTY;
        }
        else
        {
            line.state = DIFFSTATE_THEIRSADDED;
        }
        InsertViewData(viewindex++, line);
    }
    SaveUndoStep();

    // now insert an empty block in left view
    int nCount = nLastViewLine - nFirstViewLine + 1;
    m_pwndLeft->InsertViewEmptyLines(nFirstViewLine, nCount);
    SaveUndoStep();
    CleanEmptyLines();
    SaveUndoStep();
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    BuildAllScreen2ViewVector();
    RecalcAllVertScrollBars();
    m_pwndLeft->SetModified();
    SetModified();
    RefreshViews();
}

void CRightView::UseLeftBlock()
{
    if (!HasSelection())
        return;

    int nFirstViewLine = m_Screen2View[m_nSelBlockStart];
    int nLastViewLine = m_Screen2View[m_nSelBlockEnd];

    CUndo::GetInstance().BeginGrouping();

    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        viewdata line = m_pwndLeft->GetViewData(viewLine);
        line.ending = lineendings;
        switch (line.state)
        {
        case DIFFSTATE_CONFLICTEMPTY:
        case DIFFSTATE_UNKNOWN:
        case DIFFSTATE_EMPTY:
            line.state = DIFFSTATE_EMPTY;
            break;
        case DIFFSTATE_ADDED:
        case DIFFSTATE_MOVED_TO:
        case DIFFSTATE_MOVED_FROM:
        case DIFFSTATE_CONFLICTADDED:
        case DIFFSTATE_CONFLICTED:
        case DIFFSTATE_CONFLICTED_IGNORED:
        case DIFFSTATE_IDENTICALADDED:
        case DIFFSTATE_NORMAL:
        case DIFFSTATE_THEIRSADDED:
        case DIFFSTATE_YOURSADDED:
            break;
        case DIFFSTATE_IDENTICALREMOVED:
        case DIFFSTATE_REMOVED:
        case DIFFSTATE_THEIRSREMOVED:
        case DIFFSTATE_YOURSREMOVED:
            line.state = DIFFSTATE_ADDED;
            break;
        default:
            break;
        }
        SetViewData(viewLine, line);
    }

    CleanEmptyLines();
    SaveUndoStep();
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    SetModified();
    m_pwndLeft->SetModified();
    BuildAllScreen2ViewVector();
    RecalcAllVertScrollBars();
    if (m_pwndLocator)
        m_pwndLocator->DocumentUpdated();
    RefreshViews();
}

void CRightView::UseLeftFile()
{
    CUndo::GetInstance().BeginGrouping();

    int nFirstViewLine = 0;
    int nLastViewLine = GetViewCount()-1;

    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        viewdata line = m_pwndLeft->GetViewData(viewLine);
        switch (line.state)
        {
        case DIFFSTATE_CONFLICTEMPTY:
        case DIFFSTATE_UNKNOWN:
        case DIFFSTATE_EMPTY:
            line.state = DIFFSTATE_EMPTY;
            break;
        case DIFFSTATE_YOURSADDED:
        case DIFFSTATE_IDENTICALADDED:
        case DIFFSTATE_NORMAL:
        case DIFFSTATE_THEIRSADDED:
        case DIFFSTATE_ADDED:
        case DIFFSTATE_MOVED_TO:
        case DIFFSTATE_MOVED_FROM:
        case DIFFSTATE_CONFLICTADDED:
        case DIFFSTATE_CONFLICTED:
        case DIFFSTATE_CONFLICTED_IGNORED:
        case DIFFSTATE_IDENTICALREMOVED:
        case DIFFSTATE_REMOVED:
        case DIFFSTATE_THEIRSREMOVED:
        case DIFFSTATE_YOURSREMOVED:
            line.state = DIFFSTATE_NORMAL;
            m_pwndLeft->SetViewState(viewLine, DIFFSTATE_NORMAL);
            break;
        default:
            break;
        }
        SetViewData(viewLine, line);
    }
    SaveUndoStep();

    CleanEmptyLines();
    SaveUndoStep();
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    SetModified();
    m_pwndLeft->SetModified();
    BuildAllScreen2ViewVector();
    RecalcAllVertScrollBars();
    if (m_pwndLocator)
        m_pwndLocator->DocumentUpdated();
    RefreshViews();
}


void CRightView::AddContextItems(CMenu& popup, DiffStates state)
{
    const UINT uFlags = GetMenuFlags( state );

    CString temp;

    if (IsBottomViewGood())
    {
        temp.LoadString(IDS_VIEWCONTEXTMENU_USETHISBLOCK);
        popup.AppendMenu(uFlags, POPUPCOMMAND_USEYOURBLOCK, temp);
        temp.LoadString(IDS_VIEWCONTEXTMENU_USETHISFILE);
        popup.AppendMenu(MF_STRING | MF_ENABLED, POPUPCOMMAND_USEYOURFILE, temp);
        temp.LoadString(IDS_VIEWCONTEXTMENU_USEYOURANDTHEIRBLOCK);
        popup.AppendMenu(uFlags, POPUPCOMMAND_USEYOURANDTHEIRBLOCK, temp);
        temp.LoadString(IDS_VIEWCONTEXTMENU_USETHEIRANDYOURBLOCK);
        popup.AppendMenu(uFlags, POPUPCOMMAND_USETHEIRANDYOURBLOCK, temp);
    }
    else
    {
        temp.LoadString(IDS_VIEWCONTEXTMENU_USEOTHERBLOCK);
        popup.AppendMenu(uFlags, POPUPCOMMAND_USELEFTBLOCK, temp);
        temp.LoadString(IDS_VIEWCONTEXTMENU_USEOTHERFILE);
        popup.AppendMenu(MF_STRING | MF_ENABLED, POPUPCOMMAND_USELEFTFILE, temp);
        temp.LoadString(IDS_VIEWCONTEXTMENU_USEBOTHTHISFIRST);
        popup.AppendMenu(uFlags, POPUPCOMMAND_USEBOTHRIGHTFIRST, temp);
        temp.LoadString(IDS_VIEWCONTEXTMENU_USEBOTHTHISLAST);
        popup.AppendMenu(uFlags, POPUPCOMMAND_USEBOTHLEFTFIRST, temp);
    }

    CBaseView::AddContextItems(popup, state);
}


void CRightView::CleanEmptyLines()
{
    int nFirstViewLine = 0;
    int nLastViewLine = GetViewCount()-1;

    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; )
    {
        DiffStates rightState = m_pwndRight->GetViewState(viewLine);
        DiffStates leftState = m_pwndLeft->GetViewState(viewLine);
        if (IsStateEmpty(leftState) && IsStateEmpty(rightState))
        {
            m_pwndRight->RemoveViewData(viewLine);
            m_pwndLeft->RemoveViewData(viewLine);
            if (CUndo::GetInstance().IsGrouping()) // if use group undo -> ensure back adding goes in right (reversed) order
            {
                SaveUndoStep();
            }
            nLastViewLine--;
            continue;
        }
        viewLine++;
    }
}

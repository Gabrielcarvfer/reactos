/*
 * PROJECT:     ReactOS Common Dialogs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows 11 style layout of the Open and Save dialogs: the
 *              navigation pane, back/forward and the search box
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * The dialog itself is the Explorer-style one of filedlg.c, with another
 * template (NEWFILEOPENELEVENORD). It is used like Windows uses its modern
 * dialog: when the program does not customize it (no hook, no template).
 *
 *  [<][>][^] [ path of the current folder           v] [ Search      ]
 *  +------------+--------------------------------------------------+
 *  | Home       |                                                  |
 *  | Desktop    |            the current folder                    |
 *  | Downloads  |                                                  |
 *  | Documents  |                                                  |
 *  | ...        |                                                  |
 *  | > My Comp. |                                                  |
 *  | > Network  |                                                  |
 *  +------------+--------------------------------------------------+
 *                 File name: [                      ] [ Type      v]
 *                                                    [ Open ][Cancel]
 */

#include "precomp.h"
#include <commctrl.h>
#include <uxtheme.h>

WINE_DEFAULT_DEBUG_CHANNEL(commdlg);

/* The pinned folders, after Home (only those that exist are shown).
 * 0: Downloads, which has no CSIDL */
static const int PinnedFolders[] =
{
    CSIDL_DESKTOPDIRECTORY,
    0,
    CSIDL_PERSONAL,
    CSIDL_MYPICTURES,
    CSIDL_MYMUSIC,
    CSIDL_MYVIDEO,
};

static HRESULT GetDownloadsFolder(LPITEMIDLIST *ppidl)
{
    typedef HRESULT (WINAPI *PFN_GETKNOWNFOLDERIDLIST)(REFKNOWNFOLDERID, DWORD, HANDLE, LPITEMIDLIST *);
    PFN_GETKNOWNFOLDERIDLIST pfn;

    WCHAR szPath[MAX_PATH];

    /* (exported by the shell32 of the Vista+ builds) */
    pfn = (PFN_GETKNOWNFOLDERIDLIST)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHGetKnownFolderIDList");
    if (pfn && SUCCEEDED(pfn(&FOLDERID_Downloads, 0, NULL, ppidl)))
        return S_OK;

    /* otherwise the usual place, if it is there */
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, szPath)) ||
        !PathAppendW(szPath, L"Downloads") || !PathIsDirectoryW(szPath))
    {
        return E_FAIL;
    }
    *ppidl = ILCreateFromPathW(szPath);
    return *ppidl ? S_OK : E_FAIL;
}

BOOL FILEDLGNAV_IsEnabled(const FileOpenDlgInfos *fodInfos)
{
    DWORD value = 0, size = sizeof(value);

    if (!(fodInfos->ofnInfos->Flags & OFN_EXPLORER) ||
        (fodInfos->ofnInfos->Flags & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE)))
    {
        return FALSE;
    }

    /* ClassicFileDialog = 1: the dialog of older Windows versions */
    if (SHGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32",
                    L"ClassicFileDialog", NULL, &value, &size) == ERROR_SUCCESS && value)
    {
        return FALSE;
    }
    return TRUE;
}

/* NAVIGATION PANE ********************************************************/

/* Each item keeps the absolute PIDL of its folder in lParam */
static HTREEITEM NavPane_AddItem(HWND hwndTree, HTREEITEM hParent, LPITEMIDLIST pidl,
                                 LPCWSTR pszName, BOOL bExpandable)
{
    TVINSERTSTRUCTW tvis = { 0 };
    SHFILEINFOW sfi = { 0 }, sfiOpen = { 0 };

    SHGetFileInfoW((LPCWSTR)pidl, 0, &sfi, sizeof(sfi),
                   SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_DISPLAYNAME);
    SHGetFileInfoW((LPCWSTR)pidl, 0, &sfiOpen, sizeof(sfiOpen),
                   SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_OPENICON);

    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    tvis.item.pszText = (LPWSTR)(pszName ? pszName : sfi.szDisplayName);
    tvis.item.iImage = sfi.iIcon;
    tvis.item.iSelectedImage = sfiOpen.iIcon ? sfiOpen.iIcon : sfi.iIcon;
    tvis.item.cChildren = bExpandable ? 1 : 0;
    tvis.item.lParam = (LPARAM)pidl;
    return (HTREEITEM)SendMessageW(hwndTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

static LPITEMIDLIST NavPane_GetPidl(HWND hwndTree, HTREEITEM hItem)
{
    TVITEMW item = { 0 };

    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    if (!hItem || !SendMessageW(hwndTree, TVM_GETITEMW, 0, (LPARAM)&item))
        return NULL;
    return (LPITEMIDLIST)item.lParam;
}

/* The subfolders of an item, when it is expanded the first time */
static void NavPane_FillChildren(HWND hwndTree, HTREEITEM hParent)
{
    LPITEMIDLIST pidlParent = NavPane_GetPidl(hwndTree, hParent);
    IShellFolder *psfDesktop = NULL, *psfParent = NULL;
    IEnumIDList *penum = NULL;
    LPITEMIDLIST pidlChild;
    TVITEMW item = { 0 };
    BOOL bSort, bAny = FALSE;

    if (!pidlParent || FAILED(SHGetDesktopFolder(&psfDesktop)))
        return;

    if (ILIsEmpty(pidlParent))
        IShellFolder_AddRef(psfParent = psfDesktop);
    else if (FAILED(IShellFolder_BindToObject(psfDesktop, pidlParent, NULL, &IID_IShellFolder, (void **)&psfParent)))
        psfParent = NULL;

    if (psfParent &&
        IShellFolder_EnumObjects(psfParent, GetParent(hwndTree), SHCONTF_FOLDERS, &penum) == S_OK && penum)
    {
        while (IEnumIDList_Next(penum, 1, &pidlChild, NULL) == S_OK)
        {
            SFGAOF attrs = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR | SFGAO_HIDDEN;
            STRRET str;
            WCHAR szName[MAX_PATH];

            IShellFolder_GetAttributesOf(psfParent, 1, (LPCITEMIDLIST *)&pidlChild, &attrs);
            /* Folders that can hold files only (no Control Panel...), like the view */
            if ((attrs & SFGAO_FOLDER) && !(attrs & SFGAO_HIDDEN) &&
                (attrs & (SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR)) &&
                SUCCEEDED(IShellFolder_GetDisplayNameOf(psfParent, pidlChild, SHGDN_NORMAL | SHGDN_INFOLDER, &str)) &&
                SUCCEEDED(StrRetToBufW(&str, pidlChild, szName, ARRAY_SIZE(szName))))
            {
                NavPane_AddItem(hwndTree, hParent, ILCombine(pidlParent, pidlChild), szName,
                                (attrs & SFGAO_HASSUBFOLDER) != 0);
                bAny = TRUE;
            }
            ILFree(pidlChild);
        }
        IEnumIDList_Release(penum);
    }
    if (psfParent)
        IShellFolder_Release(psfParent);
    IShellFolder_Release(psfDesktop);

    /* The drives of My Computer keep their order; folders are sorted by name */
    bSort = TRUE;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidlChild)))
    {
        bSort = !ILIsEqual(pidlParent, pidlChild);
        ILFree(pidlChild);
    }
    if (bAny && bSort)
        SendMessageW(hwndTree, TVM_SORTCHILDREN, 0, (LPARAM)hParent);

    /* No subfolders after all: no expand button */
    if (!bAny)
    {
        item.mask = TVIF_CHILDREN;
        item.hItem = hParent;
        item.cChildren = 0;
        SendMessageW(hwndTree, TVM_SETITEMW, 0, (LPARAM)&item);
    }
}

static void NavPane_Fill(HWND hwnd, FileOpenDlgInfos *fodInfos)
{
    HWND hwndTree = fodInfos->Nav.hwndNavPane;
    LPITEMIDLIST pidl;
    WCHAR szHome[64];
    UINT i;

    /* Home: the recently used files */
    if (SUCCEEDED(SHGetSpecialFolderLocation(hwnd, CSIDL_RECENT, &pidl)))
    {
        if (!LoadStringW(COMDLG32_hInstance, IDS_NAVHOME, szHome, ARRAY_SIZE(szHome)))
            lstrcpyW(szHome, L"Home");
        NavPane_AddItem(hwndTree, TVI_ROOT, pidl, szHome, FALSE);
    }

    /* The pinned folders */
    for (i = 0; i < ARRAY_SIZE(PinnedFolders); i++)
    {
        HRESULT hr = PinnedFolders[i] ? SHGetSpecialFolderLocation(hwnd, PinnedFolders[i], &pidl)
                                      : GetDownloadsFolder(&pidl);
        if (SUCCEEDED(hr) && pidl)
            NavPane_AddItem(hwndTree, TVI_ROOT, pidl, NULL, TRUE);
    }

    /* This PC and the network, as trees */
    if (SUCCEEDED(SHGetSpecialFolderLocation(hwnd, CSIDL_DRIVES, &pidl)))
        NavPane_AddItem(hwndTree, TVI_ROOT, pidl, NULL, TRUE);
    if (SUCCEEDED(SHGetSpecialFolderLocation(hwnd, CSIDL_NETWORK, &pidl)))
        NavPane_AddItem(hwndTree, TVI_ROOT, pidl, NULL, TRUE);
}

/* The same folder? "My Documents" under the desktop and its path are two
 * different PIDLs of the same place */
static BOOL IsSameFolder(LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2, LPCWSTR pszPath2)
{
    WCHAR szPath1[MAX_PATH];

    if (ILIsEqual(pidl1, pidl2))
        return TRUE;
    return pszPath2[0] && SHGetPathFromIDListW(pidl1, szPath1) && !lstrcmpiW(szPath1, pszPath2);
}

/* Select the item of the current folder, if the tree shows it */
static HTREEITEM NavPane_Find(HWND hwndTree, HTREEITEM hFirst, LPCITEMIDLIST pidl, LPCWSTR pszPath)
{
    HTREEITEM hItem, hFound;

    for (hItem = hFirst; hItem;
         hItem = (HTREEITEM)SendMessageW(hwndTree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)hItem))
    {
        LPITEMIDLIST pidlItem = NavPane_GetPidl(hwndTree, hItem);

        if (pidlItem && IsSameFolder(pidlItem, pidl, pszPath))
            return hItem;
        /* only in the parts that are already expanded */
        if (SendMessageW(hwndTree, TVM_GETITEMSTATE, (WPARAM)hItem, TVIS_EXPANDED) & TVIS_EXPANDED)
        {
            hFound = NavPane_Find(hwndTree, (HTREEITEM)SendMessageW(hwndTree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)hItem),
                                  pidl, pszPath);
            if (hFound)
                return hFound;
        }
    }
    return NULL;
}

static void NavPane_Sync(FileOpenDlgInfos *fodInfos)
{
    HWND hwndTree = fodInfos->Nav.hwndNavPane;
    HTREEITEM hItem;
    WCHAR szPath[MAX_PATH];

    if (!SHGetPathFromIDListW(fodInfos->ShellInfos.pidlAbsCurrent, szPath))
        szPath[0] = UNICODE_NULL;
    hItem = NavPane_Find(hwndTree, (HTREEITEM)SendMessageW(hwndTree, TVM_GETNEXTITEM, TVGN_ROOT, 0),
                         fodInfos->ShellInfos.pidlAbsCurrent, szPath);
    fodInfos->Nav.bSyncingTree = TRUE;
    SendMessageW(hwndTree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hItem);
    fodInfos->Nav.bSyncingTree = FALSE;
}

static void Browse(HWND hwnd, FileOpenDlgInfos *fodInfos, LPCITEMIDLIST pidl)
{
    IShellBrowser_BrowseObject(fodInfos->Shell.FOIShellBrowser, pidl, SBSP_ABSOLUTE);
    if (fodInfos->ofnInfos->Flags & OFN_EXPLORER)
        SendCustomDlgNotificationMessage(hwnd, CDN_FOLDERCHANGE);
}

/* HISTORY ****************************************************************/

static void History_UpdateButtons(FileOpenDlgInfos *fodInfos)
{
    HWND hwndTB = fodInfos->DlgInfos.hwndTB;

    SendMessageW(hwndTB, TB_ENABLEBUTTON, FCIDM_TB_BACK, MAKELONG(fodInfos->Nav.iHistory > 0, 0));
    SendMessageW(hwndTB, TB_ENABLEBUTTON, FCIDM_TB_FORWARD,
                 MAKELONG(fodInfos->Nav.iHistory < fodInfos->Nav.nHistory - 1, 0));
}

static void History_Add(FileOpenDlgInfos *fodInfos, LPCITEMIDLIST pidl)
{
    int i;

    if (fodInfos->Nav.nHistory &&
        ILIsEqual(fodInfos->Nav.history[fodInfos->Nav.iHistory], pidl))
        return;

    /* A new place: the folders after the current one are forgotten */
    for (i = fodInfos->Nav.iHistory + 1; i < fodInfos->Nav.nHistory; i++)
        ILFree(fodInfos->Nav.history[i]);
    fodInfos->Nav.nHistory = fodInfos->Nav.nHistory ? fodInfos->Nav.iHistory + 1 : 0;

    /* full: forget the oldest one */
    if (fodInfos->Nav.nHistory == ARRAY_SIZE(fodInfos->Nav.history))
    {
        ILFree(fodInfos->Nav.history[0]);
        memmove(&fodInfos->Nav.history[0], &fodInfos->Nav.history[1],
                (ARRAY_SIZE(fodInfos->Nav.history) - 1) * sizeof(fodInfos->Nav.history[0]));
        fodInfos->Nav.nHistory--;
    }
    fodInfos->Nav.history[fodInfos->Nav.nHistory] = ILClone(pidl);
    fodInfos->Nav.iHistory = fodInfos->Nav.nHistory++;
}

static void History_Move(HWND hwnd, FileOpenDlgInfos *fodInfos, int delta)
{
    int i = fodInfos->Nav.iHistory + delta;

    if (i < 0 || i >= fodInfos->Nav.nHistory)
        return;
    fodInfos->Nav.iHistory = i;
    fodInfos->Nav.bHistoryMove = TRUE;
    Browse(hwnd, fodInfos, fodInfos->Nav.history[i]);
    fodInfos->Nav.bHistoryMove = FALSE;
    History_UpdateButtons(fodInfos);
}

/* SEARCH *****************************************************************/

BOOL FILEDLGNAV_IncludeName(const FileOpenDlgInfos *fodInfos, LPCWSTR pszName)
{
    if (!fodInfos->Nav.lpstrSearch)
        return TRUE;
    return StrStrIW(pszName, fodInfos->Nav.lpstrSearch) != NULL;
}

static void Search_Update(FileOpenDlgInfos *fodInfos)
{
    int len = GetWindowTextLengthW(fodInfos->Nav.hwndSearch);

    heap_free(fodInfos->Nav.lpstrSearch);
    fodInfos->Nav.lpstrSearch = NULL;
    if (len > 0 && (fodInfos->Nav.lpstrSearch = heap_alloc((len + 1) * sizeof(WCHAR))))
        GetWindowTextW(fodInfos->Nav.hwndSearch, fodInfos->Nav.lpstrSearch, len + 1);

    /* show only what matches */
    if (fodInfos->Shell.FOIShellView)
        IShellView_Refresh(fodInfos->Shell.FOIShellView);
}

/* THE DIALOG *************************************************************/

void FILEDLGNAV_InitControls(HWND hwnd, FileOpenDlgInfos *fodInfos)
{
    HIMAGELIST himl;
    SHFILEINFOW sfi = { 0 };
    WCHAR szCue[64];

    fodInfos->Nav.hwndNavPane = GetDlgItem(hwnd, IDC_NAVPANE);
    fodInfos->Nav.hwndSearch = GetDlgItem(hwnd, IDC_SEARCH);

    /* The look of the navigation pane of Explorer: no lines, full rows */
    SetWindowTheme(fodInfos->Nav.hwndNavPane, L"Explorer", NULL);
    himl = (HIMAGELIST)SHGetFileInfoW(L"", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    SendMessageW(fodInfos->Nav.hwndNavPane, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)himl);
    SendMessageW(fodInfos->Nav.hwndNavPane, TVM_SETITEMHEIGHT, GetSystemMetrics(SM_CYSMICON) + 10, 0);
    NavPane_Fill(hwnd, fodInfos);

    if (!LoadStringW(COMDLG32_hInstance, IDS_SEARCHCUE, szCue, ARRAY_SIZE(szCue)))
        lstrcpyW(szCue, L"Search");
    SendMessageW(fodInfos->Nav.hwndSearch, EM_SETCUEBANNER, TRUE, (LPARAM)szCue);

    History_UpdateButtons(fodInfos);
}

BOOL FILEDLGNAV_OnNotify(HWND hwnd, LPNMHDR pnmh, LRESULT *pResult)
{
    FileOpenDlgInfos *fodInfos;
    LPNMTREEVIEWW pnmtv = (LPNMTREEVIEWW)pnmh;

    if (pnmh->idFrom != IDC_NAVPANE)
        return FALSE;

    /* (also while the dialog is being destroyed) */
    if (pnmh->code == TVN_DELETEITEMW)
    {
        ILFree((LPITEMIDLIST)pnmtv->itemOld.lParam);
        *pResult = 0;
        return TRUE;
    }

    fodInfos = get_filedlg_infoptr(hwnd);
    if (!fodInfos || !fodInfos->Nav.bEnabled)
        return FALSE;

    switch (pnmh->code)
    {
        case TVN_ITEMEXPANDINGW:
            /* the subfolders are read when the item is first opened */
            if (pnmtv->action == TVE_EXPAND &&
                !SendMessageW(pnmh->hwndFrom, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)pnmtv->itemNew.hItem))
            {
                NavPane_FillChildren(pnmh->hwndFrom, pnmtv->itemNew.hItem);
            }
            *pResult = FALSE;
            return TRUE;

        case TVN_SELCHANGEDW:
            if (!fodInfos->Nav.bSyncingTree && pnmtv->itemNew.lParam)
                Browse(hwnd, fodInfos, (LPCITEMIDLIST)pnmtv->itemNew.lParam);
            *pResult = 0;
            return TRUE;

    }
    return FALSE;
}

BOOL FILEDLGNAV_OnCommand(HWND hwnd, WORD wID, WORD wNotifyCode)
{
    FileOpenDlgInfos *fodInfos = get_filedlg_infoptr(hwnd);

    if (!fodInfos || !fodInfos->Nav.bEnabled)
        return FALSE;

    switch (wID)
    {
        case FCIDM_TB_BACK:
            History_Move(hwnd, fodInfos, -1);
            return TRUE;
        case FCIDM_TB_FORWARD:
            History_Move(hwnd, fodInfos, +1);
            return TRUE;
        case IDC_SEARCH:
            if (wNotifyCode == EN_CHANGE)
                Search_Update(fodInfos);
            return TRUE;
    }
    return FALSE;
}

/* After each change of folder (from filedlgbrowser.c) */
void FILEDLGNAV_FolderChanged(HWND hwnd)
{
    FileOpenDlgInfos *fodInfos = get_filedlg_infoptr(hwnd);

    if (!fodInfos || !fodInfos->Nav.bEnabled || !fodInfos->ShellInfos.pidlAbsCurrent)
        return;

    if (!fodInfos->Nav.bHistoryMove)
        History_Add(fodInfos, fodInfos->ShellInfos.pidlAbsCurrent);
    History_UpdateButtons(fodInfos);
    NavPane_Sync(fodInfos);

    /* the search applies to one folder */
    if (fodInfos->Nav.lpstrSearch)
    {
        heap_free(fodInfos->Nav.lpstrSearch);
        fodInfos->Nav.lpstrSearch = NULL;
        SetWindowTextW(fodInfos->Nav.hwndSearch, L"");
    }
}

void FILEDLGNAV_Clean(HWND hwnd)
{
    FileOpenDlgInfos *fodInfos = get_filedlg_infoptr(hwnd);
    int i;

    if (!fodInfos || !fodInfos->Nav.bEnabled)
        return;
    for (i = 0; i < fodInfos->Nav.nHistory; i++)
        ILFree(fodInfos->Nav.history[i]);
    fodInfos->Nav.nHistory = fodInfos->Nav.iHistory = 0;
    heap_free(fodInfos->Nav.lpstrSearch);
    fodInfos->Nav.lpstrSearch = NULL;
    /* (the tree items free their PIDLs when the tree is destroyed) */
}

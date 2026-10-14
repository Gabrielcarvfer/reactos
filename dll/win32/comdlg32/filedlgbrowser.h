/*
 *  Implementation of IShellBrowser for the File Open common dialog
 *
 * Copyright 1999 Francois Boisvert
 * Copyright 1999, 2000 Juergen Schmied
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef SHBROWSER_H
#define SHBROWSER_H

#ifndef RC_INVOKED
#include <stdarg.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "shlobj.h"
#include "objbase.h"
#include "commdlg.h"

/***********************************************************************
 * Defines and global variables
 */

/* dialog internal property */

#define FODPROP_SAVEDLG 0x0001  /* File dialog is a Save file dialog */
#define FODPROP_USEVIEW 0x0002  /* Indicates the user selection must be taken
				   from the IShellView */

/***********************************************************************
 * Data structure
 */

#ifdef __REACTOS__
typedef struct FileOpenDlgInfos
#else
typedef struct
#endif
{
    LPOPENFILENAMEW ofnInfos;
    BOOL unicode;
    LPWSTR initdir;
    LPWSTR filename;
    LPCWSTR title;
    LPCWSTR defext;
    LPCWSTR filter;
    LPCWSTR customfilter;
    SIZE sizedlg; /* remember the size of the dialog */
    POINT initial_size; /* remember the initial size of the dialog */
    struct {
        IShellBrowser *FOIShellBrowser;
        IShellFolder *FOIShellFolder;
        IShellView *FOIShellView;
	IDataObject *FOIDataObject;
    } Shell;

    struct {
        HWND hwndOwner;
        HWND hwndView;
        FOLDERSETTINGS folderSettings;
        LPITEMIDLIST pidlAbsCurrent;
        LPWSTR lpstrCurrentFilter;
    } ShellInfos;

    struct {
        HWND hwndFileTypeCB;
        HWND hwndLookInCB;
        HWND hwndFileName;
	HWND hwndTB;
	HWND hwndGrip;
        HWND hwndCustomDlg;
	DWORD dwDlgProp;
    } DlgInfos;

    struct {
	UINT fileokstring;
	UINT lbselchstring;
	UINT helpmsgstring;
	UINT sharevistring;
    } HookMsg;

    BOOL ole_initialized;
    LPITEMIDLIST places[5];
#ifdef __REACTOS__
    LPVOID pvCWD; /* ICurrentWorkingDirectory */
    LPVOID pvDropDown; /* IAutoCompleteDropDown */
    LPVOID pvACList; /* IACList2 */
    /* The Windows 11 style layout (filedlgnav.c) */
    struct {
        BOOL bEnabled;
        HWND hwndNavPane;       /* tree: home, pinned folders, this PC, network */
        HWND hwndSearch;        /* search box: filters the current folder */
        LPWSTR lpstrSearch;     /* its text, NULL if empty */
        LPITEMIDLIST history[32]; /* visited folders, for back and forward */
        int nHistory;
        int iHistory;           /* the current one */
        BOOL bHistoryMove;      /* browsing because of back/forward */
        BOOL bSyncingTree;      /* selecting in the tree because the folder changed */
    } Nav;
#endif
} FileOpenDlgInfos;

/***********************************************************************
 * Control IDs
 */
#define IDS_FILENOTFOUND                114
#define IDS_VERIFYFILE                  115
#define IDS_CREATEFILE                  116
#define IDS_OVERWRITEFILE		119
#define IDS_INVALID_FILENAME_TITLE	120
#define IDS_INVALID_FILENAME		121
#define IDS_PATHNOTEXISTING		122
#define IDS_FILENOTEXISTING		123
#define IDS_INVALID_FOLDERNAME		124

/* File Dialog Tooltips string IDs */

#define IDS_UPFOLDER                    150
#define IDS_NEWFOLDER                   151
#define IDS_LISTVIEW                    152
#define IDS_REPORTVIEW                  153
#define IDS_TODESKTOP                   154
#ifdef __REACTOS__
#define IDS_BACK                        160
#define IDS_FORWARD                     161
#define IDS_SEARCHCUE                   162
#define IDS_NAVHOME                     163
#endif

#define IDC_OPENREADONLY                chx1

#define IDC_TOOLBARSTATIC		stc1
#define IDC_FILETYPESTATIC              stc2
#define IDC_FILENAMESTATIC              stc3
#define IDC_LOOKINSTATIC                stc4

#define IDC_SHELLSTATIC                 lst1

#define IDC_FILETYPE                    cmb1
#define IDC_LOOKIN                      cmb2

#define IDC_FILENAME                    edt1

#define IDC_TOOLBAR			1
#define IDC_TOOLBARPLACES               ctl1
#ifdef __REACTOS__
#define IDC_NAVPANE                     (ctl1 + 1) /* Windows 11 layout: navigation tree */
#define IDC_SEARCH                      edt2    /* Windows 11 layout: search box */
#define FCIDM_TB_BACK                   0xA020
#define FCIDM_TB_FORWARD                0xA021
#endif

/***********************************************************************
 * Prototypes for the methods of the IShellBrowserImpl class
 */
/* Constructor */
IShellBrowser * IShellBrowserImpl_Construct(HWND hwndOwner);


LPITEMIDLIST GetPidlFromDataObject ( IDataObject *doSelected, UINT nPidlIndex);

/* Functions used by the EDIT box */
void FILEDLG95_FILENAME_FillFromSelection (HWND hwnd);

/**************************************************************************
*   External Prototypes
*/
extern FileOpenDlgInfos *get_filedlg_infoptr(HWND hwnd);

extern IShellFolder*    GetShellFolderFromPidl(LPITEMIDLIST pidlAbs);
extern LPITEMIDLIST     GetParentPidl(LPITEMIDLIST pidl);

extern int     FILEDLG95_LOOKIN_SelectItem(HWND hwnd,LPITEMIDLIST pidl);

#ifdef __REACTOS__
/* The Windows 11 style layout (filedlgnav.c) */
#define NEWFILEOPENELEVENORD 1560   /* its dialog template */
BOOL FILEDLGNAV_IsEnabled(const FileOpenDlgInfos *fodInfos);
void FILEDLGNAV_InitControls(HWND hwnd, FileOpenDlgInfos *fodInfos);
BOOL FILEDLGNAV_OnNotify(HWND hwnd, LPNMHDR pnmh, LRESULT *pResult);
BOOL FILEDLGNAV_OnCommand(HWND hwnd, WORD wID, WORD wNotifyCode);
void FILEDLGNAV_FolderChanged(HWND hwnd);
BOOL FILEDLGNAV_IncludeName(const FileOpenDlgInfos *fodInfos, LPCWSTR pszName);
void FILEDLGNAV_Clean(HWND hwnd);
#endif
extern LRESULT SendCustomDlgNotificationMessage(HWND hwndParentDlg, UINT uCode);

#endif /*SHBROWSER_H*/

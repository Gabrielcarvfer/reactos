/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for the ExplorerBrowser and the NamespaceTreeControl
 *              (the folder view and navigation pane of IFileOpenDialog)
 */

#include "shelltest.h"
#include <shobjidl.h>
#include <shellutils.h>

static HWND create_parent(void)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"ExplorerBrowserTestParent";
    RegisterClassW(&wc);
    return CreateWindowW(wc.lpszClassName, L"ExplorerBrowser test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         0, 0, 640, 480, NULL, NULL, wc.hInstance, NULL);
}

static BOOL CALLBACK find_class(HWND hwnd, LPARAM lParam)
{
    WCHAR cls[64];
    GetClassNameW(hwnd, cls, _countof(cls));
    if (!lstrcmpW(cls, L"SHELLDLL_DefView") && IsWindowVisible(hwnd))
    {
        *(BOOL *)lParam = TRUE;
        return FALSE;
    }
    return TRUE;
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static void test_ExplorerBrowser(void)
{
    CComPtr<IExplorerBrowser> peb;
    CComPtr<IShellView> psv;
    CComPtr<IFolderView> pfv;
    CComPtr<IPersistFolder2> ppf2;
    PIDLIST_ABSOLUTE pidlSys = NULL, pidlCur = NULL;
    WCHAR szSys[MAX_PATH];
    FOLDERSETTINGS fs = { FVM_DETAILS, 0 };
    RECT rc = { 0, 0, 600, 400 };
    BOOL bView = FALSE;
    HRESULT hr;
    HWND hwnd;

    hr = CoCreateInstance(CLSID_ExplorerBrowser, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IExplorerBrowser, &peb));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    hwnd = create_parent();
    ok(hwnd != NULL, "no parent window\n");

    hr = peb->SetOptions((EXPLORER_BROWSER_OPTIONS)(EBO_SHOWFRAMES | EBO_NOBORDER));
    ok_hr(hr, S_OK);
    hr = peb->Initialize(hwnd, &rc, &fs);
    ok_hr(hr, S_OK);

    /* No view before the first navigation */
    hr = peb->GetCurrentView(IID_PPV_ARG(IShellView, &psv));
    ok(FAILED(hr), "GetCurrentView before browsing: 0x%lx\n", hr);
    psv.Release();

    GetSystemDirectoryW(szSys, _countof(szSys));
    hr = SHParseDisplayName(szSys, NULL, &pidlSys, 0, NULL);
    ok_hr(hr, S_OK);
    hr = peb->BrowseToIDList(pidlSys, SBSP_ABSOLUTE);
    ok_hr(hr, S_OK);
    pump();

    hr = peb->GetCurrentView(IID_PPV_ARG(IShellView, &psv));
    ok_hr(hr, S_OK);
    if (psv)
    {
        hr = psv->QueryInterface(IID_PPV_ARG(IFolderView, &pfv));
        ok_hr(hr, S_OK);
        if (pfv)
        {
            int count = 0;
            hr = pfv->ItemCount(SVGIO_ALLVIEW, &count);
            ok_hr(hr, S_OK);
            ok(count > 10, "only %d items in %S\n", count, szSys);

            hr = pfv->GetFolder(IID_PPV_ARG(IPersistFolder2, &ppf2));
            ok_hr(hr, S_OK);
            if (ppf2)
            {
                hr = ppf2->GetCurFolder(&pidlCur);
                ok_hr(hr, S_OK);
                ok(pidlCur && ILIsEqual(pidlCur, pidlSys), "the view shows another folder\n");
                ILFree(pidlCur);
            }
        }
    }

    EnumChildWindows(hwnd, find_class, (LPARAM)&bView);
    ok(bView, "no visible SHELLDLL_DefView in the browser\n");

    /* Back to the parent folder and back again */
    hr = peb->BrowseToIDList(NULL, SBSP_PARENT);
    ok_hr(hr, S_OK);
    hr = peb->BrowseToIDList(NULL, SBSP_NAVIGATEBACK);
    ok_hr(hr, S_OK);

    pfv.Release();
    ppf2.Release();
    psv.Release();
    hr = peb->Destroy();
    ok_hr(hr, S_OK);
    ILFree(pidlSys);
    DestroyWindow(hwnd);
}

static void test_NamespaceTreeControl(void)
{
    CComPtr<INameSpaceTreeControl> pnstc;
    CComPtr<IShellItem> psi;
    RECT rc = { 0, 0, 200, 400 };
    HRESULT hr;
    HWND hwnd;

    hr = CoCreateInstance(CLSID_NamespaceTreeControl, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARG(INameSpaceTreeControl, &pnstc));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    hwnd = create_parent();
    hr = pnstc->Initialize(hwnd, &rc, NSTCS_HASEXPANDOS);
    ok_hr(hr, S_OK);
    hr = SHCreateItemFromParsingName(L"C:\\", NULL, IID_PPV_ARG(IShellItem, &psi));
    ok_hr(hr, S_OK);
    if (psi)
    {
        hr = pnstc->AppendRoot(psi, SHCONTF_FOLDERS, NSTCRS_EXPANDED, NULL);
        ok_hr(hr, S_OK);
    }
    psi.Release();
    pnstc.Release();
    DestroyWindow(hwnd);
}

/* IFileOpenDialog: a full path typed in "File name" and Open must end the
 * dialog with that file (VS Code, VLC: nothing happened) */
static WCHAR s_szTarget[MAX_PATH];
static BOOL s_bDialogView;

static DWORD WINAPI dialog_thread(LPVOID param)
{
    HWND hDlg = NULL, hEdit = NULL;
    for (int i = 0; i < 100 && !hDlg; i++)
    {
        Sleep(100);
        hDlg = FindWindowW(L"#32770", L"ExplorerBrowser Open test");
        if (hDlg && !IsWindowVisible(hDlg))
            hDlg = NULL;
    }
    if (!hDlg)
        return 0;
    Sleep(500);
    EnumChildWindows(hDlg, find_class, (LPARAM)&s_bDialogView);
    hEdit = FindWindowExW(hDlg, NULL, L"Edit", NULL);
    if (hEdit)
        SendMessageW(hEdit, WM_SETTEXT, 0, (LPARAM)s_szTarget);
    SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)GetDlgItem(hDlg, IDOK));
    Sleep(2000);
    if (IsWindow(hDlg))
        PostMessageW(hDlg, WM_COMMAND, IDCANCEL, 0);
    return 0;
}

static void test_FileOpenDialog(void)
{
    CComPtr<IFileOpenDialog> pfd;
    CComPtr<IShellItem> psi;
    HRESULT hr;
    HANDLE hThread;

    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IFileOpenDialog, &pfd));
    if (FAILED(hr))
    {
        skip("No FileOpenDialog: 0x%lx\n", hr);
        return;
    }
    GetSystemDirectoryW(s_szTarget, _countof(s_szTarget));
    lstrcatW(s_szTarget, L"\\notepad.exe");
    pfd->SetTitle(L"ExplorerBrowser Open test");
    hThread = CreateThread(NULL, 0, dialog_thread, NULL, 0, NULL);
    hr = pfd->Show(NULL);
    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);
    ok(s_bDialogView, "the dialog shows no folder view\n");
    ok_hr(hr, S_OK);
    if (hr != S_OK)
        return;
    hr = pfd->GetResult(&psi);
    ok_hr(hr, S_OK);
    if (psi)
    {
        LPWSTR pszName = NULL;
        hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszName);
        ok_hr(hr, S_OK);
        ok(pszName && !lstrcmpiW(pszName, s_szTarget), "result %S, expected %S\n", pszName, s_szTarget);
        CoTaskMemFree(pszName);
    }
}

START_TEST(ExplorerBrowser)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok_hr(hr, S_OK);
    test_ExplorerBrowser();
    test_NamespaceTreeControl();
    test_FileOpenDialog();
    CoUninitialize();
}

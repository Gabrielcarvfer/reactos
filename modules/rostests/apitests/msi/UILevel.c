/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for the reduced UI level and wizard dialogs
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * The reduced UI (msiexec /qr) shows the authored UI without its wizard
 * (modal) dialogs. Apache OpenOffice's setup runs msiexec with /qr when it
 * is installed silently, and ReactOS showed its whole wizard.
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msiquery.h>

#define PID_TEMPLATE 7
#define PID_REVNUMBER 9
#define PID_PAGECOUNT 14
#define PID_WORDCOUNT 15

static CHAR MsiPath[MAX_PATH];

static UINT RunQuery(MSIHANDLE hDb, LPCSTR Query)
{
    MSIHANDLE hView;
    UINT r = MsiDatabaseOpenViewA(hDb, Query, &hView);
    if (r == ERROR_SUCCESS)
    {
        r = MsiViewExecute(hView, 0);
        MsiCloseHandle(hView);
    }
    ok(r == ERROR_SUCCESS, "'%s' failed: %u\n", Query, r);
    return r;
}

static BOOL CreatePackage(void)
{
    MSIHANDLE hDb, hSi;
    UINT r;

    GetTempPathA(MAX_PATH, MsiPath);
    lstrcatA(MsiPath, "msi_uileveltest.msi");
    DeleteFileA(MsiPath);
    r = MsiOpenDatabaseA(MsiPath, MSIDBOPEN_CREATE, &hDb);
    ok(r == ERROR_SUCCESS, "MsiOpenDatabase failed: %u\n", r);
    if (r != ERROR_SUCCESS)
        return FALSE;

    RunQuery(hDb, "CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL LOCALIZABLE PRIMARY KEY `Property`)");
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{3E2DA3E7-6C64-4F43-9B3A-9C8B4C3E8A11}')");
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')");
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0')");
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'ReactOS')");
    RunQuery(hDb, "CREATE TABLE `Dialog` (`Dialog` CHAR(72) NOT NULL, `HCentering` SHORT NOT NULL, `VCentering` SHORT NOT NULL, "
                  "`Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG, `Title` CHAR(128) LOCALIZABLE, "
                  "`Control_First` CHAR(50) NOT NULL, `Control_Default` CHAR(50), `Control_Cancel` CHAR(50) PRIMARY KEY `Dialog`)");
    RunQuery(hDb, "CREATE TABLE `Control` (`Dialog_` CHAR(72) NOT NULL, `Control` CHAR(50) NOT NULL, `Type` CHAR(20) NOT NULL, "
                  "`X` SHORT NOT NULL, `Y` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG, "
                  "`Property` CHAR(50), `Text` CHAR(0) LOCALIZABLE, `Control_Next` CHAR(50), `Help` CHAR(50) LOCALIZABLE "
                  "PRIMARY KEY `Dialog_`, `Control`)");
    RunQuery(hDb, "CREATE TABLE `ControlEvent` (`Dialog_` CHAR(72) NOT NULL, `Control_` CHAR(50) NOT NULL, `Event` CHAR(50) NOT NULL, "
                  "`Argument` CHAR(255) NOT NULL, `Condition` CHAR(255), `Ordering` SHORT "
                  "PRIMARY KEY `Dialog_`, `Control_`, `Event`, `Argument`, `Condition`)");
    /* A wizard page: visible (1) and modal (2) */
    RunQuery(hDb, "INSERT INTO `Dialog` (`Dialog`, `HCentering`, `VCentering`, `Width`, `Height`, `Attributes`, `Title`, "
                  "`Control_First`, `Control_Default`, `Control_Cancel`) VALUES ('WizardDlg', 50, 50, 200, 100, 3, 'msi_apitest wizard', 'Next', 'Next', 'Next')");
    RunQuery(hDb, "INSERT INTO `Control` (`Dialog_`, `Control`, `Type`, `X`, `Y`, `Width`, `Height`, `Attributes`, `Property`, "
                  "`Text`, `Control_Next`, `Help`) VALUES ('WizardDlg', 'Next', 'PushButton', 10, 10, 50, 17, 3, '', 'Next', '', '')");
    RunQuery(hDb, "INSERT INTO `ControlEvent` (`Dialog_`, `Control_`, `Event`, `Argument`, `Condition`, `Ordering`) "
                  "VALUES ('WizardDlg', 'Next', 'EndDialog', 'Return', '1', 1)");

    r = MsiGetSummaryInformationA(hDb, NULL, 5, &hSi);
    ok(r == ERROR_SUCCESS, "MsiGetSummaryInformation failed: %u\n", r);
#ifdef _WIN64
    MsiSummaryInfoSetPropertyA(hSi, PID_TEMPLATE, VT_LPSTR, 0, NULL, "x64;1033");
#else
    MsiSummaryInfoSetPropertyA(hSi, PID_TEMPLATE, VT_LPSTR, 0, NULL, "Intel;1033");
#endif
    MsiSummaryInfoSetPropertyA(hSi, PID_REVNUMBER, VT_LPSTR, 0, NULL, "{3E2DA3E7-6C64-4F43-9B3A-9C8B4C3E8A12}");
    MsiSummaryInfoSetPropertyA(hSi, PID_PAGECOUNT, VT_I4, 200, NULL, NULL);
    MsiSummaryInfoSetPropertyA(hSi, PID_WORDCOUNT, VT_I4, 2, NULL, NULL);
    MsiSummaryInfoPersist(hSi);
    MsiCloseHandle(hSi);

    r = MsiDatabaseCommit(hDb);
    ok(r == ERROR_SUCCESS, "MsiDatabaseCommit failed: %u\n", r);
    MsiCloseHandle(hDb);
    return r == ERROR_SUCCESS;
}

typedef struct
{
    INSTALLUILEVEL Level;
    UINT Result;
} DIALOG_THREAD;

static DWORD WINAPI DialogThread(LPVOID Parameter)
{
    DIALOG_THREAD *Data = Parameter;
    MSIHANDLE hPkg;

    MsiSetInternalUI(Data->Level, NULL);
    Data->Result = MsiOpenPackageA(MsiPath, &hPkg);
    if (Data->Result == ERROR_SUCCESS)
    {
        Data->Result = MsiDoActionA(hPkg, "WizardDlg");
        MsiCloseHandle(hPkg);
    }
    return 0;
}

/* Whether the wizard dialog showed up; it is closed then */
static BOOL ShowDialog(INSTALLUILEVEL Level, UINT *Result)
{
    DIALOG_THREAD Data = { Level, 0xdeadbeef };
    HANDLE Thread = CreateThread(NULL, 0, DialogThread, &Data, 0, NULL);
    BOOL Shown = FALSE;
    ULONG i;

    for (i = 0; i < 100 && WaitForSingleObject(Thread, 100) == WAIT_TIMEOUT; i++)
    {
        HWND hwnd = FindWindowA(NULL, "msi_apitest wizard");
        if (hwnd)
        {
            Shown = TRUE;
            /* Press its Next button (the default one) */
            PostMessageA(hwnd, WM_KEYDOWN, VK_RETURN, 0);
            PostMessageA(hwnd, WM_KEYUP, VK_RETURN, 0);
            PostMessageA(hwnd, WM_COMMAND, IDOK, 0);
        }
    }
    ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "The dialog thread hangs\n");
    CloseHandle(Thread);
    *Result = Data.Result;
    return Shown;
}

START_TEST(UILevel)
{
    UINT Result;
    BOOL Shown;

    if (!CreatePackage())
        return;

    /* Reduced: no wizard dialog; the action is done */
    Shown = ShowDialog(INSTALLUILEVEL_REDUCED, &Result);
    ok(!Shown, "The wizard dialog was shown with the reduced UI\n");
    ok(Result == ERROR_FUNCTION_NOT_CALLED, "Reduced: %u\n", Result);

    /* Full: the wizard */
    Shown = ShowDialog(INSTALLUILEVEL_FULL, &Result);
    ok(Shown, "The wizard dialog wasn't shown with the full UI\n");

    DeleteFileA(MsiPath);
}

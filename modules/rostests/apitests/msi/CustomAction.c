/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for exceptions in DLL custom actions
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msiquery.h>

/* Summary information properties */
#define PID_TEMPLATE 7
#define PID_REVNUMBER 9
#define PID_PAGECOUNT 14
#define PID_WORDCOUNT 15

static CHAR DllPath[MAX_PATH], MsiPath[MAX_PATH];

static BOOL ExtractDll(void)
{
    HRSRC Res = FindResourceA(NULL, MAKEINTRESOURCEA(101), (LPCSTR)RT_RCDATA);
    DWORD Size, Written;
    HANDLE File;
    BOOL Ok;

    if (!Res)
        return FALSE;
    Size = SizeofResource(NULL, Res);
    GetTempPathA(MAX_PATH, DllPath);
    lstrcatA(DllPath, "msi_ca_dll.dll");
    File = CreateFileA(DllPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ok = WriteFile(File, LockResource(LoadResource(NULL, Res)), Size, &Written, NULL);
    CloseHandle(File);
    return Ok && Written == Size;
}

static UINT RunQuery(MSIHANDLE hDb, LPCSTR Query, MSIHANDLE hRec)
{
    MSIHANDLE hView;
    UINT r = MsiDatabaseOpenViewA(hDb, Query, &hView);
    if (r == ERROR_SUCCESS)
    {
        r = MsiViewExecute(hView, hRec);
        MsiCloseHandle(hView);
    }
    ok(r == ERROR_SUCCESS, "'%s' failed: %u\n", Query, r);
    return r;
}

static BOOL CreatePackage(void)
{
    static const struct { LPCSTR Name; int Type; LPCSTR Function; } Actions[] =
    {
        { "ok", 1, "CA_Ok" },
        { "fail", 1, "CA_Fail" },
        { "throw", 1, "CA_Throw" },
        { "throw_continue", 1 | 0x40, "CA_Throw" },
        { "av", 1, "CA_AccessViolation" },
    };
    MSIHANDLE hDb, hRec, hSi;
    CHAR Query[256];
    UINT r, i;

    GetTempPathA(MAX_PATH, MsiPath);
    lstrcatA(MsiPath, "msi_catest.msi");
    DeleteFileA(MsiPath);
    r = MsiOpenDatabaseA(MsiPath, MSIDBOPEN_CREATE, &hDb);
    ok(r == ERROR_SUCCESS, "MsiOpenDatabase failed: %u\n", r);
    if (r != ERROR_SUCCESS)
        return FALSE;

    RunQuery(hDb, "CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL LOCALIZABLE PRIMARY KEY `Property`)", 0);
    RunQuery(hDb, "CREATE TABLE `Binary` (`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`)", 0);
    RunQuery(hDb, "CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`)", 0);
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{7DF88A48-996F-4EC8-A022-BF956F9B2CBB}')", 0);
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')", 0);
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0')", 0);
    RunQuery(hDb, "INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'ReactOS')", 0);
    for (i = 0; i < _countof(Actions); i++)
    {
        wsprintfA(Query, "INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('%s', %d, 'cadll', '%s')",
                  Actions[i].Name, Actions[i].Type, Actions[i].Function);
        RunQuery(hDb, Query, 0);
    }
    hRec = MsiCreateRecord(1);
    r = MsiRecordSetStreamA(hRec, 1, DllPath);
    ok(r == ERROR_SUCCESS, "MsiRecordSetStream failed: %u\n", r);
    RunQuery(hDb, "INSERT INTO `Binary` (`Name`, `Data`) VALUES ('cadll', ?)", hRec);
    MsiCloseHandle(hRec);

    r = MsiGetSummaryInformationA(hDb, NULL, 5, &hSi);
    ok(r == ERROR_SUCCESS, "MsiGetSummaryInformation failed: %u\n", r);
#ifdef _WIN64
    MsiSummaryInfoSetPropertyA(hSi, PID_TEMPLATE, VT_LPSTR, 0, NULL, "x64;1033");
#else
    MsiSummaryInfoSetPropertyA(hSi, PID_TEMPLATE, VT_LPSTR, 0, NULL, "Intel;1033");
#endif
    MsiSummaryInfoSetPropertyA(hSi, PID_REVNUMBER, VT_LPSTR, 0, NULL, "{7DF88A48-996F-4EC8-A022-BF956F9B2CBC}");
    MsiSummaryInfoSetPropertyA(hSi, PID_PAGECOUNT, VT_I4, 200, NULL, NULL);
    MsiSummaryInfoSetPropertyA(hSi, PID_WORDCOUNT, VT_I4, 2, NULL, NULL);
    MsiSummaryInfoPersist(hSi);
    MsiCloseHandle(hSi);

    r = MsiDatabaseCommit(hDb);
    ok(r == ERROR_SUCCESS, "MsiDatabaseCommit failed: %u\n", r);
    MsiCloseHandle(hDb);
    return r == ERROR_SUCCESS;
}

static void TestAction(LPCSTR Action, UINT Expected, LPCSTR ExpectedBefore)
{
    MSIHANDLE hPkg;
    CHAR Value[8] = "";
    DWORD Size = sizeof(Value);
    UINT r;

    r = MsiOpenPackageA(MsiPath, &hPkg);
    ok(r == ERROR_SUCCESS, "MsiOpenPackage failed: %u\n", r);
    if (r != ERROR_SUCCESS)
        return;
    r = MsiDoActionA(hPkg, Action);
    ok(r == Expected, "%s: expected %u, got %u\n", Action, Expected, r);
    MsiGetPropertyA(hPkg, "BEFORE_THROW", Value, &Size);
    ok(!lstrcmpA(Value, ExpectedBefore), "%s: BEFORE_THROW is '%s'\n", Action, Value);
    Size = sizeof(Value);
    MsiGetPropertyA(hPkg, "AFTER_THROW", Value, &Size);
    ok(!lstrcmpA(Value, ""), "%s: AFTER_THROW is '%s'\n", Action, Value);
    MsiCloseHandle(hPkg);
}

START_TEST(CustomAction)
{
    if (!ExtractDll())
    {
        skip("Cannot extract the custom action DLL\n");
        return;
    }
    if (CreatePackage())
    {
        MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
        TestAction("ok", ERROR_SUCCESS, "");
        TestAction("fail", ERROR_INSTALL_FAILURE, "");
        /* An exception in a custom action fails the action; it doesn't end
         * the custom action server (or msiexec) */
        TestAction("throw", ERROR_INSTALL_FAILURE, "1");
        /* ... unless the action's return value is ignored */
        TestAction("throw_continue", ERROR_SUCCESS, "1");
        TestAction("av", ERROR_INSTALL_FAILURE, "1");
        DeleteFileA(MsiPath);
    }
    DeleteFileA(DllPath);
}

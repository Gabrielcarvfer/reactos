/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the 32-bit and 64-bit registry views (KEY_WOW64_32KEY / KEY_WOW64_64KEY)
 *
 * - The keys of a 32-bit program go to the 32-bit view (Wow6432Node), and
 *   KEY_WOW64_32KEY / KEY_WOW64_64KEY reach the other view.
 * - msi (Wireshark's Visual C++ runtime, a 32-bit WiX Burn bundle) creates
 *   HKLM\...\Installer\UserData\...\Features in the 64-bit view in one call:
 *   the missing parent keys must be created in that view too.
 * - The program folders of 32-bit and 64-bit programs (environment and
 *   ProgramFilesDir) as installers read them.
 * On 32-bit Windows the view flags are ignored. The test keys live under
 * HKLM\Software and are deleted at the end.
 */

#include "precomp.h"

#define TEST_KEY L"Software\\ReactOS_RegWow64Views_Test"
#define TEST_KEY_PARENTS L"Software\\ReactOS_RegWow64Views_Test2"
#define TEST_KEY_CLASSES L"Software\\Classes\\ReactOS_RegWow64Views_Test3"
#define TEST_CLSID L"{7B1A1E9C-5D3A-4E8B-9C2F-52454143544F}"

typedef HRESULT (WINAPI *PFN_SHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR);

static BOOL Is32Bit, IsOs64;

static void DeleteTree(HKEY Root, PCWSTR Path, REGSAM View)
{
    HKEY Key;
    WCHAR Name[MAX_PATH];

    if (RegOpenKeyExW(Root, Path, 0, KEY_ALL_ACCESS | View, &Key))
        return;
    while (!RegEnumKeyW(Key, 0, Name, _countof(Name)))
    {
        DeleteTree(Key, Name, View);
        if (RegDeleteKeyExW(Key, Name, View, 0))
            break;
    }
    RegCloseKey(Key);
    RegDeleteKeyExW(Root, Path, View, 0);
}

static BOOL KeyExists(HKEY Root, PCWSTR Path, REGSAM View)
{
    HKEY Key;

    if (RegOpenKeyExW(Root, Path, 0, KEY_READ | View, &Key))
        return FALSE;
    RegCloseKey(Key);
    return TRUE;
}

static BOOL ReadString(HKEY Root, PCWSTR Path, REGSAM View, PCWSTR Name, PWSTR Buffer, DWORD Size)
{
    HKEY Key;
    DWORD Type, Bytes = Size - sizeof(WCHAR);
    LONG Ret;

    Buffer[0] = UNICODE_NULL;
    if (RegOpenKeyExW(Root, Path, 0, KEY_READ | View, &Key))
        return FALSE;
    ZeroMemory(Buffer, Size);
    Ret = RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)Buffer, &Bytes);
    RegCloseKey(Key);
    return Ret == ERROR_SUCCESS && (Type == REG_SZ || Type == REG_EXPAND_SZ);
}

static LONG CreateWithValue(HKEY Root, PCWSTR Path, REGSAM View, PCWSTR Value)
{
    HKEY Key;
    LONG Ret;

    Ret = RegCreateKeyExW(Root, Path, 0, NULL, 0, KEY_ALL_ACCESS | View, NULL, &Key, NULL);
    if (Ret)
        return Ret;
    Ret = RegSetValueExW(Key, L"Bits", 0, REG_SZ, (const BYTE *)Value, (DWORD)(wcslen(Value) + 1) * sizeof(WCHAR));
    RegCloseKey(Key);
    return Ret;
}

static void TestViews(void)
{
    WCHAR Buffer[MAX_PATH];
    HKEY Key;
    LONG Ret;

    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY, KEY_WOW64_32KEY);
    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY, KEY_WOW64_64KEY);

    Ret = CreateWithValue(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Sub", KEY_WOW64_32KEY, L"32");
    if (Ret == ERROR_ACCESS_DENIED)
    {
        skip("No write access to HKLM\\Software\n");
        return;
    }
    ok(Ret == ERROR_SUCCESS, "Creating the key in the 32-bit view: %ld\n", Ret);
    Ret = CreateWithValue(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Sub", KEY_WOW64_64KEY, L"64");
    ok(Ret == ERROR_SUCCESS, "Creating the key in the 64-bit view: %ld\n", Ret);

    ReadString(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Sub", KEY_WOW64_32KEY, L"Bits", Buffer, sizeof(Buffer));
    ok(!wcscmp(Buffer, IsOs64 ? L"32" : L"64"), "KEY_WOW64_32KEY reads '%ls'\n", Buffer);
    ReadString(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Sub", KEY_WOW64_64KEY, L"Bits", Buffer, sizeof(Buffer));
    ok(!wcscmp(Buffer, L"64"), "KEY_WOW64_64KEY reads '%ls'\n", Buffer);
    ReadString(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Sub", 0, L"Bits", Buffer, sizeof(Buffer));
    ok(!wcscmp(Buffer, (IsOs64 && Is32Bit) ? L"32" : L"64"), "The default view reads '%ls'\n", Buffer);

    if (IsOs64)
    {
        /* The 32-bit view is Wow6432Node of the 64-bit one */
        ReadString(HKEY_LOCAL_MACHINE, L"Software\\Wow6432Node\\ReactOS_RegWow64Views_Test\\Sub",
                   KEY_WOW64_64KEY, L"Bits", Buffer, sizeof(Buffer));
        ok(!wcscmp(Buffer, L"32"), "Wow6432Node reads '%ls'\n", Buffer);

        /* Relative to a key opened in a view */
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software", 0, KEY_READ | KEY_WOW64_32KEY, &Key))
        {
            ReadString(Key, L"ReactOS_RegWow64Views_Test\\Sub", 0, L"Bits", Buffer, sizeof(Buffer));
            ok(!wcscmp(Buffer, L"32"), "Relative to HKLM\\Software (32-bit view): '%ls'\n", Buffer);
            RegCloseKey(Key);
        }
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software", 0, KEY_READ | KEY_WOW64_64KEY, &Key))
        {
            ReadString(Key, L"ReactOS_RegWow64Views_Test\\Sub", 0, L"Bits", Buffer, sizeof(Buffer));
            ok(!wcscmp(Buffer, L"64"), "Relative to HKLM\\Software (64-bit view): '%ls'\n", Buffer);
            RegCloseKey(Key);
        }

        /* Below Software, a key keeps the view it was opened in: its subkeys
         * are opened and created there without a flag (TeamViewer's
         * installer: SOFTWARE\TVInstallTemp, then RestoreInfo) */
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, TEST_KEY, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &Key))
        {
            HKEY Sub;

            ReadString(Key, L"Sub", 0, L"Bits", Buffer, sizeof(Buffer));
            ok(!wcscmp(Buffer, L"64"), "Relative to a 64-bit view key: '%ls'\n", Buffer);
            Ret = RegCreateKeyExW(Key, L"Rel64", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Sub, NULL);
            ok(Ret == ERROR_SUCCESS, "Creating a subkey of a 64-bit view key: %ld\n", Ret);
            if (!Ret) RegCloseKey(Sub);
            ok(KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Rel64", KEY_WOW64_64KEY),
               "The subkey is not in the 64-bit view\n");
            ok(!KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Rel64", KEY_WOW64_32KEY),
               "The subkey went to the 32-bit view\n");
            RegDeleteKeyExW(HKEY_LOCAL_MACHINE, TEST_KEY L"\\Rel64", KEY_WOW64_64KEY, 0);
            RegCloseKey(Key);
        }
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, TEST_KEY, 0, KEY_READ | KEY_WOW64_32KEY, &Key))
        {
            ReadString(Key, L"Sub", 0, L"Bits", Buffer, sizeof(Buffer));
            ok(!wcscmp(Buffer, L"32"), "Relative to a 32-bit view key: '%ls'\n", Buffer);
            RegCloseKey(Key);
        }
    }

    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY, KEY_WOW64_32KEY);
    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY, KEY_WOW64_64KEY);
    ok(!KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY, KEY_WOW64_32KEY), "Test key left in the 32-bit view\n");
    ok(!KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY, KEY_WOW64_64KEY), "Test key left in the 64-bit view\n");
}

/* msi: missing parents of a new key go to the view asked for */
static void TestParents(void)
{
    LONG Ret;

    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS, KEY_WOW64_32KEY);
    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS, KEY_WOW64_64KEY);

    Ret = CreateWithValue(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS L"\\Products\\Test\\Features", KEY_WOW64_64KEY, L"64");
    if (Ret == ERROR_ACCESS_DENIED)
    {
        skip("No write access to HKLM\\Software\n");
        return;
    }
    ok(Ret == ERROR_SUCCESS, "Creating a key and its parents in the 64-bit view: %ld\n", Ret);
    ok(KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS L"\\Products\\Test\\Features", KEY_WOW64_64KEY),
       "The key is not in the 64-bit view\n");
    ok(KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS L"\\Products", KEY_WOW64_64KEY),
       "The parent is not in the 64-bit view\n");
    if (IsOs64)
        ok(!KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS, KEY_WOW64_32KEY), "A parent went to the 32-bit view\n");

    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS, KEY_WOW64_64KEY);
    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY_PARENTS, KEY_WOW64_32KEY);

    /* Under Software\Classes */
    Ret = CreateWithValue(HKEY_LOCAL_MACHINE, TEST_KEY_CLASSES L"\\Sub", KEY_WOW64_64KEY, L"64");
    ok(Ret == ERROR_SUCCESS, "Creating a key under Software\\Classes in the 64-bit view: %ld\n", Ret);
    ok(KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY_CLASSES L"\\Sub", KEY_WOW64_64KEY),
       "The Classes key is not in the 64-bit view\n");
    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY_CLASSES, KEY_WOW64_64KEY);
    DeleteTree(HKEY_LOCAL_MACHINE, TEST_KEY_CLASSES, KEY_WOW64_32KEY);
    ok(!KeyExists(HKEY_LOCAL_MACHINE, TEST_KEY_CLASSES, KEY_WOW64_64KEY), "Classes test key left\n");
}

/* Relative opens below the classes: COM (CoCreateInstance) reads CLSID\{...}
 * relative to HKCR, and a 32-bit program must find its 32-bit registration
 * (Google's updater: REGDB_E_CLASSNOTREG). Windows keeps the view a key was
 * opened in, so the same Classes key behaves differently when opened with
 * KEY_WOW64_64KEY. */
static void ReadRelative(HKEY Root, PCWSTR RootPath, REGSAM RootView, PCWSTR Name, PCWSTR Expected, BOOL Todo)
{
    WCHAR Buffer[MAX_PATH] = L"";
    HKEY Key = Root;

    if (RootPath && RegOpenKeyExW(Root, RootPath, 0, KEY_READ | RootView, &Key))
    {
        ok(0, "Cannot open %ls (view 0x%lx)\n", RootPath, RootView);
        return;
    }
    ReadString(Key, Name, 0, L"Bits", Buffer, sizeof(Buffer));
    todo_ros_if(Todo)
    ok(!wcscmp(Buffer, Expected), "%ls (view 0x%lx) + %ls: '%ls', expected '%ls'\n",
       RootPath ? RootPath : L"(root)", RootView, Name, Buffer, Expected);
    if (RootPath) RegCloseKey(Key);
}

static void TestClassesRelative(void)
{
    static const WCHAR Clsid[] = L"Software\\Classes\\CLSID\\" TEST_CLSID;
    PCWSTR Mine = Is32Bit ? L"32" : L"64";
    LONG Ret;

    if (!IsOs64)
    {
        skip("32-bit Windows: one view of the classes\n");
        return;
    }

    DeleteTree(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_32KEY);
    DeleteTree(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_64KEY);
    Ret = CreateWithValue(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_32KEY, L"32");
    if (Ret == ERROR_ACCESS_DENIED)
    {
        skip("No write access to HKLM\\Software\\Classes\n");
        return;
    }
    ok(Ret == ERROR_SUCCESS, "Creating the 32-bit CLSID: %ld\n", Ret);
    Ret = CreateWithValue(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_64KEY, L"64");
    ok(Ret == ERROR_SUCCESS, "Creating the 64-bit CLSID: %ld\n", Ret);

    ReadRelative(HKEY_CLASSES_ROOT, NULL, 0, L"CLSID\\" TEST_CLSID, Mine, FALSE);
    ReadRelative(HKEY_LOCAL_MACHINE, L"Software\\Classes", 0, L"CLSID\\" TEST_CLSID, Mine, FALSE);
    /* (ReactOS knows the view of a root key from its path only: a key that is
       the same in both views counts as opened in the view of the program) */
    ReadRelative(HKEY_LOCAL_MACHINE, L"Software\\Classes", KEY_WOW64_32KEY, L"CLSID\\" TEST_CLSID, L"32", !Is32Bit);
    ReadRelative(HKEY_LOCAL_MACHINE, L"Software\\Classes", KEY_WOW64_64KEY, L"CLSID\\" TEST_CLSID, L"64", Is32Bit);
    ReadRelative(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID", 0, TEST_CLSID, Mine, FALSE);
    ReadRelative(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID", KEY_WOW64_32KEY, TEST_CLSID, L"32", FALSE);
    ReadRelative(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID", KEY_WOW64_64KEY, TEST_CLSID, L"64", FALSE);

    DeleteTree(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_32KEY);
    DeleteTree(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_64KEY);
    ok(!KeyExists(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_32KEY), "Test CLSID left in the 32-bit view\n");
    ok(!KeyExists(HKEY_LOCAL_MACHINE, Clsid, KEY_WOW64_64KEY), "Test CLSID left in the 64-bit view\n");
}

static void TestProgramFolders(void)
{
    static const WCHAR CurrentVersion[] = L"Software\\Microsoft\\Windows\\CurrentVersion";
    WCHAR Env[MAX_PATH], EnvX86[MAX_PATH], EnvW6432[MAX_PATH], Buffer[MAX_PATH];
    HMODULE Shell32;
    DWORD Attributes;

    if (!IsOs64)
    {
        skip("32-bit Windows: no 32-bit program folders\n");
        return;
    }

    ok(GetEnvironmentVariableW(L"ProgramFiles", Env, _countof(Env)) != 0, "No %%ProgramFiles%%\n");
    ok(GetEnvironmentVariableW(L"ProgramFiles(x86)", EnvX86, _countof(EnvX86)) != 0, "No %%ProgramFiles(x86)%%\n");
    ok(GetEnvironmentVariableW(L"ProgramW6432", EnvW6432, _countof(EnvW6432)) != 0, "No %%ProgramW6432%%\n");
    ok(_wcsicmp(EnvX86, EnvW6432) != 0, "The 32-bit and 64-bit program folders are the same: %ls\n", EnvX86);
    ok(!_wcsicmp(Env, Is32Bit ? EnvX86 : EnvW6432), "%%ProgramFiles%% is %ls (x86 %ls, W6432 %ls)\n",
       Env, EnvX86, EnvW6432);
    Attributes = GetFileAttributesW(EnvX86);
    ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY),
       "%ls is not a folder: 0x%lx\n", EnvX86, Attributes);

    ok(ReadString(HKEY_LOCAL_MACHINE, CurrentVersion, 0, L"ProgramFilesDir", Buffer, sizeof(Buffer)),
       "No ProgramFilesDir\n");
    ok(!_wcsicmp(Buffer, Env), "ProgramFilesDir is %ls, %%ProgramFiles%% %ls\n", Buffer, Env);
    ok(ReadString(HKEY_LOCAL_MACHINE, CurrentVersion, KEY_WOW64_64KEY, L"ProgramFilesDir", Buffer, sizeof(Buffer)),
       "No ProgramFilesDir in the 64-bit view\n");
    ok(!_wcsicmp(Buffer, EnvW6432), "64-bit ProgramFilesDir is %ls, %%ProgramW6432%% %ls\n", Buffer, EnvW6432);
    ok(ReadString(HKEY_LOCAL_MACHINE, CurrentVersion, KEY_WOW64_32KEY, L"ProgramFilesDir", Buffer, sizeof(Buffer)),
       "No ProgramFilesDir in the 32-bit view\n");
    ok(!_wcsicmp(Buffer, EnvX86), "32-bit ProgramFilesDir is %ls, %%ProgramFiles(x86)%% %ls\n", Buffer, EnvX86);

    Shell32 = LoadLibraryW(L"shell32.dll");
    if (Shell32)
    {
        PFN_SHGetFolderPathW pSHGetFolderPathW = (PFN_SHGetFolderPathW)GetProcAddress(Shell32, "SHGetFolderPathW");
        if (pSHGetFolderPathW)
        {
            Buffer[0] = UNICODE_NULL;
            ok(pSHGetFolderPathW(NULL, 0x26 /* CSIDL_PROGRAM_FILES */, NULL, 0, Buffer) == S_OK,
               "SHGetFolderPath (CSIDL_PROGRAM_FILES) failed\n");
            ok(!_wcsicmp(Buffer, Env), "CSIDL_PROGRAM_FILES is %ls, %%ProgramFiles%% %ls\n", Buffer, Env);
        }
        FreeLibrary(Shell32);
    }
}

START_TEST(RegWow64Views)
{
    BOOL Wow64 = FALSE;

    Is32Bit = (sizeof(PVOID) == 4);
    IsWow64Process(GetCurrentProcess(), &Wow64);
    IsOs64 = !Is32Bit || Wow64;
    trace("%u-bit process, %s Windows\n", Is32Bit ? 32 : 64, IsOs64 ? "64-bit" : "32-bit");

    TestViews();
    TestParents();
    TestClassesRelative();
    TestProgramFolders();
}

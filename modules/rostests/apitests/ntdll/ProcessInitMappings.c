/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     A new process maps no PE file as data before kernel32.dll
 *
 * Windows builds the activation context of a new process outside of it
 * (CSRSS), so the process does not map the PE files of its private
 * assemblies as data. Programs rely on that: Firefox's NtMapViewOfSection
 * hook, set up in its browser process before it runs, takes every PE file
 * mapped before kernel32.dll for an image and reads its exports. (Windows
 * maps other data files, e.g. NLS tables, before kernel32.dll.)
 *
 * The test starts a copy of itself, suspended, with an external manifest
 * that depends on a private assembly (initmapasm.dll next to it, a DLL with
 * the assembly's manifest), hooks NtMapViewOfSection in the child (the hook
 * is this file's HookMapView, at the same place in the child's image) and
 * lets it run: the child counts the PE files mapped as data until
 * kernel32.dll is mapped.
 */

#include "precomp.h"

#ifdef _M_AMD64

typedef NTSTATUS (NTAPI *PFN_MAP_VIEW)(HANDLE, HANDLE, PVOID *, ULONG_PTR, SIZE_T,
                                       PLARGE_INTEGER, PSIZE_T, SECTION_INHERIT, ULONG, ULONG);
typedef NTSTATUS (NTAPI *PFN_QUERY_VM)(HANDLE, PVOID, MEMORY_INFORMATION_CLASS, PVOID, SIZE_T, PSIZE_T);

/* Set by the parent in the child (they only use these and no import: the
   hook runs before the child's imports are resolved) */
static volatile PFN_MAP_VIEW g_OriginalMapView;
static volatile PFN_QUERY_VM g_QueryVirtualMemory;
static volatile LONG g_DataViews;
static volatile LONG g_Kernel32Mapped;

static BOOLEAN IsKernel32(PVOID Base)
{
    PIMAGE_DOS_HEADER Dos = Base;
    PIMAGE_NT_HEADERS64 Nt;
    PIMAGE_EXPORT_DIRECTORY Exports;
    const char *Name, *Want = "kernel32.dll";
    ULONG i;

    if (Dos->e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    Nt = (PIMAGE_NT_HEADERS64)((PUCHAR)Base + Dos->e_lfanew);
    if (Nt->Signature != IMAGE_NT_SIGNATURE ||
        Nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        Nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT ||
        !Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress)
    {
        return FALSE;
    }
    Exports = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)Base +
              Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    Name = (const char *)Base + Exports->Name;
    for (i = 0; Want[i]; i++)
    {
        char c = Name[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != Want[i])
            return FALSE;
    }
    return Name[i] == 0;
}

static NTSTATUS NTAPI
HookMapView(HANDLE SectionHandle, HANDLE ProcessHandle, PVOID *BaseAddress, ULONG_PTR ZeroBits,
            SIZE_T CommitSize, PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize,
            SECTION_INHERIT InheritDisposition, ULONG AllocationType, ULONG Win32Protect)
{
    NTSTATUS Status;
    MEMORY_BASIC_INFORMATION Info;

    Status = g_OriginalMapView(SectionHandle, ProcessHandle, BaseAddress, ZeroBits, CommitSize,
                               SectionOffset, ViewSize, InheritDisposition, AllocationType, Win32Protect);
    if (NT_SUCCESS(Status) && ProcessHandle == NtCurrentProcess() && !g_Kernel32Mapped &&
        NT_SUCCESS(g_QueryVirtualMemory(NtCurrentProcess(), *BaseAddress, MemoryBasicInformation,
                                        &Info, sizeof(Info), NULL)))
    {
        if (Info.Type != MEM_IMAGE)
        {
            /* A PE file mapped as data (Firefox's hook looks for the exports
               of what it takes for an image) */
            PIMAGE_DOS_HEADER Dos = *BaseAddress;
            if (*ViewSize >= sizeof(*Dos) && Dos->e_magic == IMAGE_DOS_SIGNATURE &&
                Dos->e_lfanew > 0 && (SIZE_T)Dos->e_lfanew + sizeof(ULONG) <= *ViewSize &&
                *(PULONG)((PUCHAR)Dos + Dos->e_lfanew) == IMAGE_NT_SIGNATURE)
            {
                g_DataViews++;
            }
        }
        else if (IsKernel32(*BaseAddress))
            g_Kernel32Mapped = 1;
    }
    return Status;
}

static BOOL WriteFileText(PCWSTR Path, PCSTR Text)
{
    HANDLE File;
    DWORD Written;
    BOOL Ret;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL);
    CloseHandle(File);
    return Ret;
}

/* initmapasm.dll is resource 103 of this program (testdata.rc) */
static BOOL ExtractDll(PCWSTR Path)
{
    HRSRC Res = FindResourceW(NULL, (LPCWSTR)(ULONG_PTR)103, (LPCWSTR)(ULONG_PTR)10 /* RT_RCDATA */);
    HGLOBAL Data;
    HANDLE File;
    DWORD Size, Written;
    BOOL Ret;

    if (!Res || !(Data = LoadResource(NULL, Res)))
        return FALSE;
    Size = SizeofResource(NULL, Res);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(File, LockResource(Data), Size, &Written, NULL) && Written == Size;
    CloseHandle(File);
    return Ret;
}

static BOOL WriteChild(HANDLE Process, PVOID ParentAddress, LONG_PTR Delta, PVOID Value, SIZE_T Size)
{
    return WriteProcessMemory(Process, (PUCHAR)ParentAddress + Delta, Value, Size, NULL);
}

static void RunChild(void)
{
    WCHAR Dir[MAX_PATH], Exe[MAX_PATH], Path[MAX_PATH], Self[MAX_PATH], CmdLine[MAX_PATH * 2];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    PROCESS_BASIC_INFORMATION Basic;
    PVOID ChildImage = NULL, Tramp;
    PUCHAR MapView;
    LONG_PTR Delta;
    UCHAR Stub[12], Patch[12], TrampCode[11];
    PVOID Value;
    DWORD OldProtect, ExitCode = 0xdead;
    NTSTATUS Status;
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    GetTempPathW(_countof(Dir), Dir);
    StringCchPrintfW(Dir + wcslen(Dir), _countof(Dir) - wcslen(Dir), L"initmap%lu", GetCurrentProcessId());
    CreateDirectoryW(Dir, NULL);
    GetModuleFileNameW(NULL, Self, _countof(Self));
    StringCchPrintfW(Exe, _countof(Exe), L"%s\\initmap.exe", Dir);
    ok(CopyFileW(Self, Exe, FALSE), "CopyFile failed: %lu\n", GetLastError());

    StringCchPrintfW(Path, _countof(Path), L"%s\\initmap.exe.manifest", Dir);
    ok(WriteFileText(Path,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
        "<assemblyIdentity type=\"win32\" name=\"initmap\" version=\"1.0.0.0\" processorArchitecture=\"amd64\"/>\r\n"
        "<dependency><dependentAssembly>\r\n"
        "<assemblyIdentity type=\"win32\" name=\"initmapasm\" version=\"1.0.0.0\" processorArchitecture=\"amd64\"/>\r\n"
        "</dependentAssembly></dependency>\r\n"
        "</assembly>\r\n"), "writing the manifest failed\n");
    StringCchPrintfW(Path, _countof(Path), L"%s\\initmapasm.dll", Dir);
    ok(ExtractDll(Path), "extracting initmapasm.dll failed: %lu\n", GetLastError());

    /* NtMapViewOfSection starts with "mov r10, rcx; mov eax, <number>" */
    MapView = (PUCHAR)GetProcAddress(Ntdll, "NtMapViewOfSection");
    memcpy(Stub, MapView, sizeof(Stub));
    if (Stub[0] != 0x4C || Stub[1] != 0x8B || Stub[2] != 0xD1 || Stub[3] != 0xB8)
    {
        skip("Unexpected NtMapViewOfSection stub %02x %02x %02x %02x\n", Stub[0], Stub[1], Stub[2], Stub[3]);
        goto Cleanup;
    }

    StringCchPrintfW(CmdLine, _countof(CmdLine), L"\"%s\" ProcessInitMappings child", Exe);
    if (!CreateProcessW(Exe, CmdLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        ok(0, "CreateProcess failed: %lu\n", GetLastError());
        goto Cleanup;
    }

    /* Where the child's image is (the hook and its variables move with it) */
    Status = NtQueryInformationProcess(pi.hProcess, ProcessBasicInformation, &Basic, sizeof(Basic), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(ReadProcessMemory(pi.hProcess, &Basic.PebBaseAddress->ImageBaseAddress, &ChildImage, sizeof(ChildImage), NULL),
       "ReadProcessMemory failed: %lu\n", GetLastError());
    Delta = (PUCHAR)ChildImage - (PUCHAR)GetModuleHandleW(NULL);

    /* A copy of the system call stub that the hook calls */
    Tramp = VirtualAllocEx(pi.hProcess, NULL, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    ok(Tramp != NULL, "VirtualAllocEx failed: %lu\n", GetLastError());
    memcpy(TrampCode, Stub, 8);
    TrampCode[8] = 0x0F; TrampCode[9] = 0x05; /* syscall */
    TrampCode[10] = 0xC3;                     /* ret */
    ok(WriteProcessMemory(pi.hProcess, Tramp, TrampCode, sizeof(TrampCode), NULL), "writing the stub failed\n");

    Value = Tramp;
    ok(WriteChild(pi.hProcess, (PVOID)&g_OriginalMapView, Delta, &Value, sizeof(Value)), "write failed\n");
    Value = (PVOID)GetProcAddress(Ntdll, "NtQueryVirtualMemory");
    ok(WriteChild(pi.hProcess, (PVOID)&g_QueryVirtualMemory, Delta, &Value, sizeof(Value)), "write failed\n");

    /* mov rax, HookMapView; jmp rax (ntdll is at the same address in the child) */
    Value = (PUCHAR)HookMapView + Delta;
    Patch[0] = 0x48; Patch[1] = 0xB8;
    memcpy(&Patch[2], &Value, sizeof(Value));
    Patch[10] = 0xFF; Patch[11] = 0xE0;
    ok(VirtualProtectEx(pi.hProcess, MapView, sizeof(Patch), PAGE_EXECUTE_READWRITE, &OldProtect),
       "VirtualProtectEx failed: %lu\n", GetLastError());
    ok(WriteProcessMemory(pi.hProcess, MapView, Patch, sizeof(Patch), NULL), "patching failed: %lu\n", GetLastError());
    VirtualProtectEx(pi.hProcess, MapView, sizeof(Patch), OldProtect, &OldProtect);
    FlushInstructionCache(pi.hProcess, MapView, sizeof(Patch));

    ResumeThread(pi.hThread);
    ok(WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0, "the child did not finish\n");
    GetExitCodeProcess(pi.hProcess, &ExitCode);
    ok((ExitCode & 0x100) != 0, "the child did not see kernel32.dll mapped (exit code 0x%lx)\n", ExitCode);
    ok((ExitCode & 0xFF) == 0, "%lu data views were mapped before kernel32.dll\n", ExitCode & 0xFF);
    if (ExitCode == 0xdead || ExitCode == STILL_ACTIVE)
        TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

Cleanup:
    StringCchPrintfW(Path, _countof(Path), L"%s\\initmapasm.dll", Dir);
    DeleteFileW(Path);
    StringCchPrintfW(Path, _countof(Path), L"%s\\initmap.exe.manifest", Dir);
    DeleteFileW(Path);
    DeleteFileW(Exe);
    RemoveDirectoryW(Dir);
}

START_TEST(ProcessInitMappings)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);

    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        /* The hook stays in place, but counts nothing after kernel32.dll */
        ExitProcess((g_Kernel32Mapped ? 0x100 : 0) | (g_DataViews > 0xFF ? 0xFF : g_DataViews));
    }

    RunChild();
}

#else

START_TEST(ProcessInitMappings)
{
    skip("This test is for x64\n");
}

#endif

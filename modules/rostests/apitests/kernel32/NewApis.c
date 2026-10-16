/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for kernel32 functions added for Windows 7/10 programs
 *
 * FindStringOrdinal, the XState context functions, GetVolumeInformationByHandleW
 * and the WER registrations, which programs built for Windows 10 import
 * (Qt 6, Chromium, installers) and call at startup.
 */

#include "precomp.h"

#ifndef FIND_STARTSWITH
#define FIND_STARTSWITH 0x00100000
#define FIND_ENDSWITH   0x00200000
#define FIND_FROMSTART  0x00400000
#define FIND_FROMEND    0x00800000
#endif

typedef INT (WINAPI *FN_FindStringOrdinal)(DWORD, LPCWSTR, INT, LPCWSTR, INT, BOOL);
typedef DWORD64 (WINAPI *FN_GetEnabledXStateFeatures)(VOID);
typedef BOOL (WINAPI *FN_InitializeContext)(PVOID, DWORD, PCONTEXT *, PDWORD);
typedef BOOL (WINAPI *FN_CopyContext)(PCONTEXT, DWORD, PCONTEXT);
typedef PVOID (WINAPI *FN_LocateXStateFeature)(PCONTEXT, DWORD, PDWORD);
typedef BOOL (WINAPI *FN_SetXStateFeaturesMask)(PCONTEXT, DWORD64);
typedef BOOL (WINAPI *FN_GetXStateFeaturesMask)(PCONTEXT, DWORD64 *);
typedef BOOL (WINAPI *FN_GetVolumeInformationByHandleW)(HANDLE, LPWSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPWSTR, DWORD);
typedef HRESULT (WINAPI *FN_WerSetFlags)(DWORD);
typedef HRESULT (WINAPI *FN_WerRegisterMemoryBlock)(PVOID, DWORD);
typedef HRESULT (WINAPI *FN_WerUnregisterMemoryBlock)(PVOID);

static HMODULE Kernel32;
static LONG MemoryBlock;

static
PVOID
GetExport(PCSTR Name)
{
    PVOID Function = (PVOID)GetProcAddress(Kernel32, Name);
    ok(Function != NULL, "kernel32 does not export %s\n", Name);
    return Function;
}

static
VOID
Test_FindStringOrdinal(VOID)
{
    FN_FindStringOrdinal pFindStringOrdinal = (FN_FindStringOrdinal)GetExport("FindStringOrdinal");
    INT Ret;

    if (!pFindStringOrdinal)
    {
        skip("No FindStringOrdinal\n");
        return;
    }

    Ret = pFindStringOrdinal(FIND_FROMSTART, L"abcabc", -1, L"bc", -1, FALSE);
    ok(Ret == 1, "FIND_FROMSTART returned %d, expected 1\n", Ret);
    Ret = pFindStringOrdinal(FIND_FROMEND, L"abcabc", -1, L"bc", -1, FALSE);
    ok(Ret == 4, "FIND_FROMEND returned %d, expected 4\n", Ret);
    Ret = pFindStringOrdinal(FIND_STARTSWITH, L"abcabc", -1, L"AB", -1, TRUE);
    ok(Ret == 0, "FIND_STARTSWITH ignoring case returned %d, expected 0\n", Ret);
    Ret = pFindStringOrdinal(FIND_STARTSWITH, L"abcabc", -1, L"AB", -1, FALSE);
    ok(Ret == -1, "FIND_STARTSWITH with case returned %d, expected -1\n", Ret);
    Ret = pFindStringOrdinal(FIND_ENDSWITH, L"abcabc", 6, L"abc", 3, FALSE);
    ok(Ret == 3, "FIND_ENDSWITH returned %d, expected 3\n", Ret);
    Ret = pFindStringOrdinal(FIND_FROMSTART, L"ab", -1, L"abc", -1, FALSE);
    ok(Ret == -1, "Value longer than the source returned %d, expected -1\n", Ret);
    SetLastError(0xdeadbeef);
    Ret = pFindStringOrdinal(0, L"ab", -1, L"a", -1, FALSE);
    ok(Ret == -1, "Invalid flags returned %d, expected -1\n", Ret);
    ok(GetLastError() == ERROR_INVALID_FLAGS, "Error %lu, expected ERROR_INVALID_FLAGS\n", GetLastError());
}

static
VOID
Test_XState(VOID)
{
    FN_GetEnabledXStateFeatures pGetEnabledXStateFeatures = (FN_GetEnabledXStateFeatures)GetExport("GetEnabledXStateFeatures");
    FN_InitializeContext pInitializeContext = (FN_InitializeContext)GetExport("InitializeContext");
    FN_CopyContext pCopyContext = (FN_CopyContext)GetExport("CopyContext");
    FN_LocateXStateFeature pLocateXStateFeature = (FN_LocateXStateFeature)GetExport("LocateXStateFeature");
    FN_SetXStateFeaturesMask pSetXStateFeaturesMask = (FN_SetXStateFeaturesMask)GetExport("SetXStateFeaturesMask");
    FN_GetXStateFeaturesMask pGetXStateFeaturesMask = (FN_GetXStateFeaturesMask)GetExport("GetXStateFeaturesMask");
    DWORD64 Features, Mask = 0;
    DWORD Length = 0, FeatureLength = 0;
    PCONTEXT Context = NULL, Context2 = NULL;
    PVOID Buffer, Buffer2;
    BOOL Ret;

    if (!pGetEnabledXStateFeatures || !pInitializeContext || !pCopyContext || !pLocateXStateFeature ||
        !pSetXStateFeaturesMask || !pGetXStateFeaturesMask)
    {
        skip("XState functions missing\n");
        return;
    }

    Features = pGetEnabledXStateFeatures();
    ok((Features & 3) == 3, "Enabled XState features 0x%I64x lack x87 and SSE\n", Features);

    SetLastError(0xdeadbeef);
    Ret = pInitializeContext(NULL, CONTEXT_FULL, NULL, &Length);
    ok(!Ret, "InitializeContext size query succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Error %lu, expected ERROR_INSUFFICIENT_BUFFER\n", GetLastError());
    ok(Length >= sizeof(CONTEXT), "Length %lu is smaller than CONTEXT (%u)\n", Length, (UINT)sizeof(CONTEXT));
    if (Length < sizeof(CONTEXT))
        return;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    Buffer2 = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    if (!Buffer || !Buffer2)
    {
        skip("Out of memory\n");
        goto Cleanup;
    }

    Ret = pInitializeContext(Buffer, CONTEXT_FULL, &Context, &Length);
    ok(Ret, "InitializeContext failed with %lu\n", GetLastError());
    ok(Context != NULL && Context->ContextFlags == CONTEXT_FULL, "Context %p, flags 0x%lx\n",
       Context, Context ? Context->ContextFlags : 0);
    Ret = pInitializeContext(Buffer2, CONTEXT_ALL, &Context2, &Length);
    ok(Ret, "InitializeContext(CONTEXT_ALL) failed with %lu\n", GetLastError());
    if (!Context || !Context2)
        goto Cleanup;

    ok(((ULONG_PTR)Context & (sizeof(PVOID) == 8 ? 15 : 3)) == 0, "Context %p is not aligned\n", Context);
    RtlCaptureContext(Context2);
    Context2->ContextFlags = CONTEXT_ALL;
    ok(pCopyContext(Context, CONTEXT_CONTROL, Context2), "CopyContext failed with %lu\n", GetLastError());
#ifdef _M_AMD64
    ok(Context->Rip == Context2->Rip, "Rip %p, expected %p\n", (PVOID)Context->Rip, (PVOID)Context2->Rip);
    ok(Context->Rax == 0, "CopyContext copied Rax (0x%I64x) with CONTEXT_CONTROL\n", Context->Rax);
#elif defined(_M_IX86)
    ok(Context->Eip == Context2->Eip, "Eip 0x%lx, expected 0x%lx\n", Context->Eip, Context2->Eip);
    ok(Context->Eax == 0, "CopyContext copied Eax (0x%lx) with CONTEXT_CONTROL\n", Context->Eax);
#endif

    ok(pLocateXStateFeature(Context2, 1, &FeatureLength) != NULL, "LocateXStateFeature(SSE) returned NULL\n");
    ok(FeatureLength > 0, "SSE feature length is 0\n");
    ok(pLocateXStateFeature(Context2, 2, NULL) == NULL, "LocateXStateFeature(AVX) found AVX in a context without XState\n");
    ok(pSetXStateFeaturesMask(Context2, 3), "SetXStateFeaturesMask(legacy) failed with %lu\n", GetLastError());
    ok(!pSetXStateFeaturesMask(Context2, 4), "SetXStateFeaturesMask(AVX) succeeded on a context without XState\n");
    ok(pGetXStateFeaturesMask(Context2, &Mask), "GetXStateFeaturesMask failed with %lu\n", GetLastError());
    ok(Mask == 3, "Mask is 0x%I64x, expected 3\n", Mask);

Cleanup:
    if (Buffer)
        HeapFree(GetProcessHeap(), 0, Buffer);
    if (Buffer2)
        HeapFree(GetProcessHeap(), 0, Buffer2);
}

static
VOID
Test_GetVolumeInformationByHandleW(VOID)
{
    FN_GetVolumeInformationByHandleW pGetVolumeInformationByHandleW =
        (FN_GetVolumeInformationByHandleW)GetExport("GetVolumeInformationByHandleW");
    WCHAR Path[MAX_PATH], Root[4], FileSystem[64] = L"", Label[64] = L"";
    DWORD Serial = 0, MaxLength = 0, Flags = 0, Serial2 = 0;
    HANDLE File;

    if (!pGetVolumeInformationByHandleW)
    {
        skip("No GetVolumeInformationByHandleW\n");
        return;
    }

    GetSystemDirectoryW(Path, _countof(Path));
    StringCchCopyNW(Root, _countof(Root), Path, 3);
    StringCchCatW(Path, _countof(Path), L"\\kernel32.dll");
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %ls failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    ok(pGetVolumeInformationByHandleW(File, Label, _countof(Label), &Serial, &MaxLength, &Flags,
                                      FileSystem, _countof(FileSystem)),
       "GetVolumeInformationByHandleW failed with %lu\n", GetLastError());
    ok(GetVolumeInformationW(Root, NULL, 0, &Serial2, NULL, NULL, NULL, 0),
       "GetVolumeInformationW(%ls) failed with %lu\n", Root, GetLastError());
    ok(Serial == Serial2, "Serial %08lx, by path %08lx\n", Serial, Serial2);
    ok(FileSystem[0] != UNICODE_NULL, "No file system name\n");
    ok(MaxLength > 0, "Maximum name length is 0\n");
    CloseHandle(File);
}

static
VOID
Test_Wer(VOID)
{
    FN_WerSetFlags pWerSetFlags = (FN_WerSetFlags)GetExport("WerSetFlags");
    FN_WerRegisterMemoryBlock pWerRegisterMemoryBlock = (FN_WerRegisterMemoryBlock)GetExport("WerRegisterMemoryBlock");
    FN_WerUnregisterMemoryBlock pWerUnregisterMemoryBlock =
        (FN_WerUnregisterMemoryBlock)GetProcAddress(Kernel32, "WerUnregisterMemoryBlock");
    HRESULT hr;

    if (pWerSetFlags)
    {
        hr = pWerSetFlags(0);
        ok(hr == S_OK, "WerSetFlags returned 0x%lx\n", hr);
    }
    if (pWerRegisterMemoryBlock)
    {
        hr = pWerRegisterMemoryBlock(&MemoryBlock, sizeof(MemoryBlock));
        ok(hr == S_OK, "WerRegisterMemoryBlock returned 0x%lx\n", hr);
        if (pWerUnregisterMemoryBlock)
            pWerUnregisterMemoryBlock(&MemoryBlock);
    }
}

START_TEST(NewApis)
{
    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Test_FindStringOrdinal();
    Test_XState();
    Test_GetVolumeInformationByHandleW();
    Test_Wer();
}

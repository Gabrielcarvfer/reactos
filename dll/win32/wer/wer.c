/*
 * PROJECT:     ReactOS Windows Error Reporting
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The wer.dll functions that programs call
 *
 * ReactOS has no Windows Error Reporting service. Programs that exclude
 * themselves from it, or that file their own reports, still need these
 * functions to load (LibreOffice imports WerAddExcludedApplication). They
 * succeed, and a report that is submitted gets "WER is disabled".
 */

#include <windef.h>
#include <winbase.h>
#include <werapi.h>

/* The handle of a report: nothing is kept in it */
#define WER_REPORT_MAGIC 0x52455752 /* 'WERR' */

HRESULT
WINAPI
WerAddExcludedApplication(
    _In_ PCWSTR pwzExeName,
    _In_ BOOL bAllUsers)
{
    if (!pwzExeName || !*pwzExeName)
        return E_INVALIDARG;
    UNREFERENCED_PARAMETER(bAllUsers);
    return S_OK;
}

HRESULT
WINAPI
WerRemoveExcludedApplication(
    _In_ PCWSTR pwzExeName,
    _In_ BOOL bAllUsers)
{
    if (!pwzExeName || !*pwzExeName)
        return E_INVALIDARG;
    UNREFERENCED_PARAMETER(bAllUsers);
    return S_OK;
}

HRESULT
WINAPI
WerReportCreate(
    _In_ PCWSTR pwzEventType,
    _In_ WER_REPORT_TYPE repType,
    _In_opt_ PWER_REPORT_INFORMATION pReportInformation,
    _Out_ HREPORT *phReportHandle)
{
    PDWORD Report;

    UNREFERENCED_PARAMETER(pReportInformation);
    if (!pwzEventType || !*pwzEventType || !phReportHandle || repType >= WerReportInvalid)
        return E_INVALIDARG;

    Report = HeapAlloc(GetProcessHeap(), 0, sizeof(*Report));
    if (!Report)
        return E_OUTOFMEMORY;
    *Report = WER_REPORT_MAGIC;
    *phReportHandle = (HREPORT)Report;
    return S_OK;
}

static
BOOL
IsReport(
    _In_ HREPORT hReportHandle)
{
    return hReportHandle && *(PDWORD)hReportHandle == WER_REPORT_MAGIC;
}

HRESULT
WINAPI
WerReportCloseHandle(
    _In_ HREPORT hReportHandle)
{
    if (!IsReport(hReportHandle))
        return E_INVALIDARG;
    *(PDWORD)hReportHandle = 0;
    HeapFree(GetProcessHeap(), 0, hReportHandle);
    return S_OK;
}

HRESULT
WINAPI
WerReportSetParameter(
    _In_ HREPORT hReportHandle,
    _In_ DWORD dwparamID,
    _In_opt_ PCWSTR pwzName,
    _In_ PCWSTR pwzValue)
{
    UNREFERENCED_PARAMETER(dwparamID);
    UNREFERENCED_PARAMETER(pwzName);
    if (!IsReport(hReportHandle) || !pwzValue)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
WINAPI
WerReportAddFile(
    _In_ HREPORT hReportHandle,
    _In_ PCWSTR pwzPath,
    _In_ DWORD repFileType,
    _In_ DWORD dwFileFlags)
{
    UNREFERENCED_PARAMETER(repFileType);
    UNREFERENCED_PARAMETER(dwFileFlags);
    if (!IsReport(hReportHandle) || !pwzPath)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
WINAPI
WerReportAddDump(
    _In_ HREPORT hReportHandle,
    _In_ HANDLE hProcess,
    _In_opt_ HANDLE hThread,
    _In_ DWORD dumpType,
    _In_opt_ PVOID pExceptionParam,
    _In_opt_ PVOID pDumpCustomOptions,
    _In_ DWORD dwFlags)
{
    UNREFERENCED_PARAMETER(hProcess);
    UNREFERENCED_PARAMETER(hThread);
    UNREFERENCED_PARAMETER(dumpType);
    UNREFERENCED_PARAMETER(pExceptionParam);
    UNREFERENCED_PARAMETER(pDumpCustomOptions);
    UNREFERENCED_PARAMETER(dwFlags);
    if (!IsReport(hReportHandle))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
WINAPI
WerReportSetUIOption(
    _In_ HREPORT hReportHandle,
    _In_ DWORD repUITypeID,
    _In_ PCWSTR pwzValue)
{
    UNREFERENCED_PARAMETER(repUITypeID);
    if (!IsReport(hReportHandle) || !pwzValue)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
WINAPI
WerReportSubmit(
    _In_ HREPORT hReportHandle,
    _In_ WER_CONSENT consent,
    _In_ DWORD dwFlags,
    _Out_opt_ PWER_SUBMIT_RESULT pSubmitResult)
{
    UNREFERENCED_PARAMETER(consent);
    UNREFERENCED_PARAMETER(dwFlags);
    if (!IsReport(hReportHandle))
        return E_INVALIDARG;
    if (pSubmitResult)
        *pSubmitResult = WerDisabled;
    return S_OK;
}

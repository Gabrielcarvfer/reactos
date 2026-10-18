
#define STANDALONE
#include <apitest.h>

extern void func_ActCtxWithXmlNamespaces(void);
extern void func_ConsoleCP(void);
extern void func_ConsoleCtrlEvent(void);
extern void func_ConsoleVT(void);
extern void func_CreateEventEx(void);
extern void func_CreateProcess(void);
extern void func_DefaultActCtx(void);
extern void func_DeviceIoControl(void);
extern void func_dosdev(void);
extern void func_EnumSystemCodePages(void);
extern void func_FastFail(void);
extern void func_FatStress(void);
extern void func_FindActCtxSectionStringW(void);
extern void func_FindFiles(void);
extern void func_FindFirstFileEx(void);
extern void func_FindNLSString(void);
extern void func_FLS(void);
extern void func_FormatMessage(void);
extern void func_GetComputerNameEx(void);
extern void func_GetCPInfo(void);
extern void func_GetCurrentDirectory(void);
extern void func_GetDriveType(void);
extern void func_GetDynamicTimeZoneInformation(void);
extern void func_GetEnvironmentVariable(void);
extern void func_GetFinalPathNameByHandle(void);
extern void func_GetLocaleInfo(void);
extern void func_GetModuleFileName(void);
extern void func_GetVolumeInformation(void);
extern void func_HeapCreateExecute(void);
extern void func_InheritHandleList(void);
extern void func_InitOnce(void);
extern void func_interlck(void);
extern void func_IsDBCSLeadByteEx(void);
extern void func_JapaneseCalendar(void);
extern void func_JobObject(void);
extern void func_LCMapString(void);
extern void func_LoadLibraryExW(void);
extern void func_LockStress(void);
extern void func_LocaleNameToLCID(void);
extern void func_LockFileEx(void);
extern void func_LockedReadWrite(void);
extern void func_lstrcpynW(void);
extern void func_lstrlen(void);
extern void func_MailslotRead(void);
extern void func_MultiByteToWideChar(void);
extern void func_NestedJobs(void);
extern void func_NewApis(void);
extern void func_PackageNames(void);
extern void func_PipeCompletionPort(void);
extern void func_PipeProcessIds(void);
extern void func_Pipes(void);
extern void func_PrivMoveFileIdentityW(void);
extern void func_ProcThreadAttributeList(void);
extern void func_QueryLimitedInformation(void);
extern void func_QueryPerformanceCounter(void);
extern void func_QueueUserAPC(void);
extern void func_ReadDirectoryChanges(void);
extern void func_ReadOnlySection(void);
extern void func_SecurityLabel(void);
extern void func_SetComputerNameExW(void);
extern void func_SetConsoleWindowInfo(void);
extern void func_SetCurrentDirectory(void);
extern void func_SetUnhandledExceptionFilter(void);
extern void func_StackGuard(void);
extern void func_SuspendedChildPeb(void);
extern void func_SystemFirmware(void);
extern void func_TerminateProcess(void);
extern void func_Toolhelp32(void);
extern void func_TunnelCache(void);
extern void func_UEFIFirmware(void);
extern void func_UnknownCsrThread(void);
extern void func_UnregisterWaitEx(void);
extern void func_VirtualAlloc2(void);
extern void func_WideCharToMultiByte(void);
extern void func_Wow64Basics(void);
extern void func_Wow64FsRedirection(void);
extern void func_WriteProcessMemoryCow(void);

const struct test winetest_testlist[] =
{
    { "ActCtxWithXmlNamespaces",     func_ActCtxWithXmlNamespaces },
    { "ConsoleCP",                   func_ConsoleCP },
    { "ConsoleCtrlEvent",            func_ConsoleCtrlEvent },
    { "ConsoleVT",                   func_ConsoleVT },
    { "CreateEventEx",               func_CreateEventEx },
    { "CreateProcess",               func_CreateProcess },
    { "DefaultActCtx",               func_DefaultActCtx },
    { "DeviceIoControl",             func_DeviceIoControl },
    { "dosdev",                      func_dosdev },
    { "EnumSystemCodePages",         func_EnumSystemCodePages },
    { "FastFail",                    func_FastFail },
    { "FatStress",                   func_FatStress },
    { "FindActCtxSectionStringW",    func_FindActCtxSectionStringW },
    { "FindFiles",                   func_FindFiles },
    { "FindFirstFileEx", func_FindFirstFileEx },
    { "FindNLSString",               func_FindNLSString },
    { "FLS",                         func_FLS },
    { "FormatMessage",               func_FormatMessage },
    { "GetComputerNameEx",           func_GetComputerNameEx },
    { "GetCPInfo",                   func_GetCPInfo },
    { "GetCurrentDirectory",         func_GetCurrentDirectory },
    { "GetDriveType",                func_GetDriveType },
    { "GetDynamicTimeZoneInformation", func_GetDynamicTimeZoneInformation },
    { "GetEnvironmentVariable",      func_GetEnvironmentVariable },
    { "GetFinalPathNameByHandle",    func_GetFinalPathNameByHandle },
    { "GetLocaleInfo",               func_GetLocaleInfo },
    { "GetModuleFileName",           func_GetModuleFileName },
    { "GetVolumeInformation",        func_GetVolumeInformation },
    { "HeapCreateExecute",           func_HeapCreateExecute },
    { "InheritHandleList",           func_InheritHandleList },
    { "InitOnce",                    func_InitOnce },
    { "interlck",                    func_interlck },
    { "IsDBCSLeadByteEx",            func_IsDBCSLeadByteEx },
    { "JapaneseCalendar",            func_JapaneseCalendar },
    { "JobObject",                   func_JobObject },
    { "LCMapString",                 func_LCMapString },
    { "LoadLibraryExW",              func_LoadLibraryExW },
    { "LockStress",                  func_LockStress },
    { "LocaleNameToLCID",            func_LocaleNameToLCID },
    { "LockFileEx",                  func_LockFileEx },
    { "LockedReadWrite",             func_LockedReadWrite },
    { "lstrcpynW",                   func_lstrcpynW },
    { "lstrlen",                     func_lstrlen },
    { "MailslotRead",                func_MailslotRead },
    { "MultiByteToWideChar",         func_MultiByteToWideChar },
    { "NestedJobs",                  func_NestedJobs },
    { "NewApis",                     func_NewApis },
    { "PackageNames",                func_PackageNames },
    { "PipeCompletionPort",          func_PipeCompletionPort },
    { "PipeProcessIds",              func_PipeProcessIds },
    { "Pipes",                       func_Pipes },
    { "PrivMoveFileIdentityW",       func_PrivMoveFileIdentityW },
    { "ProcThreadAttributeList", func_ProcThreadAttributeList },
    { "QueryLimitedInformation",     func_QueryLimitedInformation },
    { "QueryPerformanceCounter",     func_QueryPerformanceCounter },
    { "QueueUserAPC",                func_QueueUserAPC },
    { "ReadDirectoryChanges",        func_ReadDirectoryChanges },
    { "ReadOnlySection",             func_ReadOnlySection },
    { "SecurityLabel", func_SecurityLabel },
    { "SetComputerNameExW",          func_SetComputerNameExW },
    { "SetConsoleWindowInfo",        func_SetConsoleWindowInfo },
    { "SetCurrentDirectory",         func_SetCurrentDirectory },
    { "SetUnhandledExceptionFilter", func_SetUnhandledExceptionFilter },
    { "StackGuard", func_StackGuard },
    { "SuspendedChildPeb",           func_SuspendedChildPeb },
    { "SystemFirmware",              func_SystemFirmware },
    { "TerminateProcess",            func_TerminateProcess },
    { "Toolhelp32",                  func_Toolhelp32 },
    { "TunnelCache",                 func_TunnelCache },
    { "UEFIFirmware",                func_UEFIFirmware },
    { "UnknownCsrThread",            func_UnknownCsrThread },
    { "UnregisterWaitEx",            func_UnregisterWaitEx },
    { "VirtualAlloc2",               func_VirtualAlloc2 },
    { "WideCharToMultiByte",         func_WideCharToMultiByte },
    { "Wow64Basics",                 func_Wow64Basics },
    { "Wow64FsRedirection",          func_Wow64FsRedirection },
    { "WriteProcessMemoryCow",       func_WriteProcessMemoryCow },
    { 0, 0 }
};

/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ReactOS system calls that Wine's wow64.dll has no thunk for
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wow64_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wow);

#define UNIMPLEMENTED_SYSCALL(name) \
    NTSTATUS WINAPI wow64_ ## name( UINT *args ) \
    { \
        FIXME( "%s: not implemented\n", #name ); \
        return STATUS_NOT_IMPLEMENTED; \
    }

UNIMPLEMENTED_SYSCALL(NtAccessCheckByType)
UNIMPLEMENTED_SYSCALL(NtAccessCheckByTypeAndAuditAlarm)
UNIMPLEMENTED_SYSCALL(NtAccessCheckByTypeResultList)
UNIMPLEMENTED_SYSCALL(NtAccessCheckByTypeResultListAndAuditAlarm)
UNIMPLEMENTED_SYSCALL(NtAccessCheckByTypeResultListAndAuditAlarmByHandle)
UNIMPLEMENTED_SYSCALL(NtAddBootEntry)
UNIMPLEMENTED_SYSCALL(NtAddDriverEntry)
UNIMPLEMENTED_SYSCALL(NtAllocateUserPhysicalPages)
UNIMPLEMENTED_SYSCALL(NtApphelpCacheControl)
UNIMPLEMENTED_SYSCALL(NtCancelDeviceWakeupRequest)
UNIMPLEMENTED_SYSCALL(NtCloseObjectAuditAlarm)
UNIMPLEMENTED_SYSCALL(NtCompactKeys)
UNIMPLEMENTED_SYSCALL(NtCompareTokens)
UNIMPLEMENTED_SYSCALL(NtCompressKey)
UNIMPLEMENTED_SYSCALL(NtCreateEventPair)
UNIMPLEMENTED_SYSCALL(NtCreateJobSet)
UNIMPLEMENTED_SYSCALL(NtCreateProfile)
UNIMPLEMENTED_SYSCALL(NtCreateWaitablePort)
UNIMPLEMENTED_SYSCALL(NtDeleteBootEntry)
UNIMPLEMENTED_SYSCALL(NtDeleteDriverEntry)
UNIMPLEMENTED_SYSCALL(NtDeleteObjectAuditAlarm)
UNIMPLEMENTED_SYSCALL(NtEnumerateBootEntries)
UNIMPLEMENTED_SYSCALL(NtEnumerateDriverEntries)
UNIMPLEMENTED_SYSCALL(NtEnumerateSystemEnvironmentValuesEx)
UNIMPLEMENTED_SYSCALL(NtExtendSection)
UNIMPLEMENTED_SYSCALL(NtFlushWriteBuffer)
UNIMPLEMENTED_SYSCALL(NtFreeUserPhysicalPages)
UNIMPLEMENTED_SYSCALL(NtGetDevicePowerState)
UNIMPLEMENTED_SYSCALL(NtGetPlugPlayEvent)
UNIMPLEMENTED_SYSCALL(NtImpersonateClientOfPort)
UNIMPLEMENTED_SYSCALL(NtImpersonateThread)
UNIMPLEMENTED_SYSCALL(NtInitializeRegistry)
UNIMPLEMENTED_SYSCALL(NtIsSystemResumeAutomatic)
UNIMPLEMENTED_SYSCALL(NtLockProductActivationKeys)
UNIMPLEMENTED_SYSCALL(NtLockRegistryKey)
UNIMPLEMENTED_SYSCALL(NtMakePermanentObject)
UNIMPLEMENTED_SYSCALL(NtMapUserPhysicalPages)
UNIMPLEMENTED_SYSCALL(NtMapUserPhysicalPagesScatter)
UNIMPLEMENTED_SYSCALL(NtModifyBootEntry)
UNIMPLEMENTED_SYSCALL(NtModifyDriverEntry)
UNIMPLEMENTED_SYSCALL(NtOpenEventPair)
UNIMPLEMENTED_SYSCALL(NtOpenObjectAuditAlarm)
UNIMPLEMENTED_SYSCALL(NtPlugPlayControl)
UNIMPLEMENTED_SYSCALL(NtPrivilegeObjectAuditAlarm)
UNIMPLEMENTED_SYSCALL(NtPrivilegedServiceAuditAlarm)
UNIMPLEMENTED_SYSCALL(NtQueryBootEntryOrder)
UNIMPLEMENTED_SYSCALL(NtQueryBootOptions)
UNIMPLEMENTED_SYSCALL(NtQueryDriverEntryOrder)
UNIMPLEMENTED_SYSCALL(NtQueryInformationPort)
UNIMPLEMENTED_SYSCALL(NtQueryIntervalProfile)
UNIMPLEMENTED_SYSCALL(NtQueryOpenSubKeys)
UNIMPLEMENTED_SYSCALL(NtQueryOpenSubKeysEx)
UNIMPLEMENTED_SYSCALL(NtQueryQuotaInformationFile)
UNIMPLEMENTED_SYSCALL(NtReadRequestData)
UNIMPLEMENTED_SYSCALL(NtReplyPort)
UNIMPLEMENTED_SYSCALL(NtReplyWaitReceivePortEx)
UNIMPLEMENTED_SYSCALL(NtReplyWaitReplyPort)
UNIMPLEMENTED_SYSCALL(NtRequestDeviceWakeup)
UNIMPLEMENTED_SYSCALL(NtRequestPort)
UNIMPLEMENTED_SYSCALL(NtRequestWakeupLatency)
UNIMPLEMENTED_SYSCALL(NtSaveKeyEx)
UNIMPLEMENTED_SYSCALL(NtSaveMergedKeys)
UNIMPLEMENTED_SYSCALL(NtSetBootEntryOrder)
UNIMPLEMENTED_SYSCALL(NtSetBootOptions)
UNIMPLEMENTED_SYSCALL(NtSetDefaultHardErrorPort)
UNIMPLEMENTED_SYSCALL(NtSetDriverEntryOrder)
UNIMPLEMENTED_SYSCALL(NtSetEventBoostPriority)
UNIMPLEMENTED_SYSCALL(NtSetHighEventPair)
UNIMPLEMENTED_SYSCALL(NtSetHighWaitLowEventPair)
UNIMPLEMENTED_SYSCALL(NtSetLowEventPair)
UNIMPLEMENTED_SYSCALL(NtSetLowWaitHighEventPair)
UNIMPLEMENTED_SYSCALL(NtSetQuotaInformationFile)
UNIMPLEMENTED_SYSCALL(NtSetSystemEnvironmentValue)
UNIMPLEMENTED_SYSCALL(NtSetSystemEnvironmentValueEx)
UNIMPLEMENTED_SYSCALL(NtSetSystemPowerState)
UNIMPLEMENTED_SYSCALL(NtSetUuidSeed)
UNIMPLEMENTED_SYSCALL(NtStartProfile)
UNIMPLEMENTED_SYSCALL(NtStopProfile)
UNIMPLEMENTED_SYSCALL(NtTraceEvent)
UNIMPLEMENTED_SYSCALL(NtTranslateFilePath)
UNIMPLEMENTED_SYSCALL(NtUnloadKey2)
UNIMPLEMENTED_SYSCALL(NtUnloadKeyEx)
UNIMPLEMENTED_SYSCALL(NtVdmControl)
UNIMPLEMENTED_SYSCALL(NtWaitHighEventPair)
UNIMPLEMENTED_SYSCALL(NtWaitLowEventPair)
UNIMPLEMENTED_SYSCALL(NtWriteRequestData)
UNIMPLEMENTED_SYSCALL(NtQueryPortInformationProcess)
UNIMPLEMENTED_SYSCALL(NtWaitForMultipleObjects32)

/* Only numbers: nothing to convert */

NTSTATUS WINAPI wow64_NtQueryDebugFilterState( UINT *args )
{
    ULONG component_id = get_ulong( &args );
    ULONG level = get_ulong( &args );

    return NtQueryDebugFilterState( component_id, level );
}

NTSYSAPI NTSTATUS WINAPI NtCreateProcess( HANDLE *handle, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr,
                                          HANDLE parent, BOOLEAN inherit, HANDLE section, HANDLE debug_port,
                                          HANDLE exception_port );
NTSYSAPI NTSTATUS WINAPI NtCreateProcessEx( HANDLE *handle, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr,
                                            HANDLE parent, ULONG flags, HANDLE section, HANDLE debug_port,
                                            HANDLE exception_port, BOOLEAN in_job );

/* Handles and numbers: the 32-bit kernel32 then fills the new process */

/* The kernel gives the PEB of the new process in the 64-bit TEB
 * (ArbitraryUserPointer), the 32-bit kernel32 reads it in the 32-bit TEB:
 * for a 32-bit process, give it the 32-bit PEB */
static void put_new_process_peb( HANDLE process )
{
    ULONG_PTR peb = (ULONG_PTR)NtCurrentTeb()->Tib.ArbitraryUserPointer;
    ULONG_PTR wow64 = 0;

    NtQueryInformationProcess( process, ProcessWow64Information, &wow64, sizeof(wow64), NULL );
    if (wow64) peb += 0x1000;
    else
    {
        /* A 64-bit process: a page for the 32-bit PEB fields (see wow64_init_child64) */
        ULONG peb32 = wow64_init_child64( process );
        if (peb32) peb = peb32;
    }
    get_teb32()->Tib.ArbitraryUserPointer = (ULONG)peb;
}

NTSTATUS WINAPI wow64_NtCreateProcess( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE parent = get_handle( &args );
    BOOLEAN inherit = get_ulong( &args );
    HANDLE section = get_handle( &args );
    HANDLE debug_port = get_handle( &args );
    HANDLE exception_port = get_handle( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtCreateProcess( &handle, access, objattr_32to64( &attr, attr32 ), parent, inherit,
                              section, debug_port, exception_port );
    if (!status) put_new_process_peb( handle );
    put_handle( handle_ptr, handle );
    return status;
}

NTSTATUS WINAPI wow64_NtCreateProcessEx( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE parent = get_handle( &args );
    ULONG flags = get_ulong( &args );
    HANDLE section = get_handle( &args );
    HANDLE debug_port = get_handle( &args );
    HANDLE exception_port = get_handle( &args );
    BOOLEAN in_job = get_ulong( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtCreateProcessEx( &handle, access, objattr_32to64( &attr, attr32 ), parent, flags,
                                section, debug_port, exception_port, in_job );
    if (!status) put_new_process_peb( handle );
    put_handle( handle_ptr, handle );
    return status;
}

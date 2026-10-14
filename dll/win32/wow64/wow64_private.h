/*
 * WoW64 private definitions
 *
 * Copyright 2021 Alexandre Julliard
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

#ifndef __WOW64_PRIVATE_H
#define __WOW64_PRIVATE_H

#ifdef __REACTOS__
/* Generated from ntoskrnl/sysfuncs.lst: the 32-bit and the 64-bit ReactOS
 * kernels number their system calls the same way */
#include "ntsyscalls.h"
#else
#include "../ntdll/ntsyscalls.h"
#endif
#include "struct32.h"

#define SYSCALL_ENTRY(id,name,_args) extern NTSTATUS WINAPI wow64_ ## name( UINT *args );
ALL_SYSCALLS32
#undef SYSCALL_ENTRY

extern void init_image_mapping( HMODULE module );
extern void init_file_redirects(void);
extern BOOL get_file_redirect( OBJECT_ATTRIBUTES *attr );
#ifdef __REACTOS__
extern BOOL get_key_redirect( OBJECT_ATTRIBUTES *attr, ACCESS_MASK access, WCHAR *buffer, SIZE_T size,
                              UNICODE_STRING *name );
#endif

extern USHORT native_machine;
extern USHORT current_machine;
extern ULONG_PTR args_alignment;
extern ULONG_PTR highest_user_address;
extern ULONG_PTR default_zero_bits;
extern SYSTEM_DLL_INIT_BLOCK *pLdrSystemDllInitBlock;

extern void (WINAPI *pBTCpuNotifyFlushInstructionCache2)( const void *, SIZE_T );
extern void (WINAPI *pBTCpuNotifyMapViewOfSection)( void * );
extern void (WINAPI *pBTCpuNotifyMemoryAlloc)( void *, SIZE_T, ULONG, ULONG );
extern void (WINAPI *pBTCpuNotifyMemoryDirty)( void *, SIZE_T );
extern void (WINAPI *pBTCpuNotifyMemoryFree)( void *, SIZE_T, ULONG );
extern void (WINAPI *pBTCpuNotifyMemoryProtect)( void *, SIZE_T, ULONG );
extern void (WINAPI *pBTCpuNotifyUnmapViewOfSection)( void * );
extern void (WINAPI *pBTCpuUpdateProcessorInformation)( SYSTEM_CPU_INFORMATION * );
extern void (WINAPI *pBTCpuThreadTerm)( HANDLE );

struct object_attr64
{
    OBJECT_ATTRIBUTES   attr;
    UNICODE_STRING      str;
    SECURITY_DESCRIPTOR sd;
};

/* cf. GetSystemWow64Directory2 */
#ifdef __REACTOS__
#ifndef IMAGE_FILE_MACHINE_TARGET_HOST
#define IMAGE_FILE_MACHINE_TARGET_HOST 0x0001
#endif
#ifndef RTL_CONSTANT_STRING
#define RTL_CONSTANT_STRING(s) { sizeof(s) - sizeof((s)[0]), sizeof(s), (void *)(s) }
#endif
#ifndef REG_SZ
#define REG_SZ 1
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif
#define wcsnicmp _wcsnicmp
#define GetCurrentProcessId() HandleToULong( NtCurrentTeb()->ClientId.UniqueProcess )

/* The cross-process work lists are for software CPUs, which ReactOS does not
 * have; selectors of other threads are not supported yet */
#define RtlOpenCrossProcessEmulatorWorkConnection wow64_open_cross_process_work
#define RtlWow64PopAllCrossProcessWorkFromWorkList wow64_pop_all_cross_process_work
#define RtlWow64PopCrossProcessWorkFromFreeList wow64_pop_cross_process_free
#define RtlWow64PushCrossProcessWorkOntoFreeList wow64_push_cross_process_free
#define RtlWow64PushCrossProcessWorkOntoWorkList wow64_push_cross_process_work
#define RtlWow64GetThreadSelectorEntry wow64_get_thread_selector_entry
extern void WINAPI wow64_open_cross_process_work( HANDLE process, HANDLE *section, void **addr );
extern CROSS_PROCESS_WORK_ENTRY * WINAPI wow64_pop_all_cross_process_work( CROSS_PROCESS_WORK_HDR *list, BOOLEAN *flush );
extern CROSS_PROCESS_WORK_ENTRY * WINAPI wow64_pop_cross_process_free( CROSS_PROCESS_WORK_HDR *list );
extern BOOLEAN WINAPI wow64_push_cross_process_free( CROSS_PROCESS_WORK_HDR *list, CROSS_PROCESS_WORK_ENTRY *entry );
extern BOOLEAN WINAPI wow64_push_cross_process_work( CROSS_PROCESS_WORK_HDR *list, CROSS_PROCESS_WORK_ENTRY *entry, void **unknown );
extern NTSTATUS WINAPI wow64_get_thread_selector_entry( HANDLE thread, THREAD_DESCRIPTOR_INFORMATION *info, ULONG size, ULONG *ret_size );

/* Only for the code of 32-bit ARM, which never runs on ReactOS */
typedef struct
{
    ULONG PagedPoolLimit;
    ULONG NonPagedPoolLimit;
    ULONG MinimumWorkingSetSize;
    ULONG MaximumWorkingSetSize;
    ULONG PagefileLimit;
    LARGE_INTEGER TimeLimit;
} QUOTA_LIMITS32;

typedef struct _ARM_CONTEXT
{
    ULONG ContextFlags;
    ULONG R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12;
    ULONG Sp, Lr, Pc, Cpsr;
    ULONG Fpscr;
    ULONG Padding;
    ULONGLONG D[32];
    ULONG Bvr[8], Bcr[8], Wvr[1], Wcr[1];
    ULONG Padding2[2];
} ARM_CONTEXT;
#define CONTEXT_ARM                 0x200000
#define CONTEXT_ARM_CONTROL         (CONTEXT_ARM | 0x1)
#define CONTEXT_ARM_INTEGER         (CONTEXT_ARM | 0x2)
#define CONTEXT_ARM_FLOATING_POINT  (CONTEXT_ARM | 0x4)
#define CONTEXT_ARM_DEBUG_REGISTERS (CONTEXT_ARM | 0x8)
#define CONTEXT_ARM_FULL (CONTEXT_ARM_CONTROL | CONTEXT_ARM_INTEGER | CONTEXT_ARM_FLOATING_POINT)
#define CONTEXT_ARM_ALL  (CONTEXT_ARM_FULL | CONTEXT_ARM_DEBUG_REGISTERS)

/* The jobs of Wine's Unix side, and what Wine's ntdll has that ReactOS' lacks */
extern const WCHAR *get_machine_wow64_dir( USHORT machine );
extern const WCHAR *wow64_windir(void);
extern void *wow64_find_export( HMODULE module, const char *name );
extern BOOLEAN wow64_is_current_process( HANDLE handle );
extern BOOLEAN wow64_is_processor_feature_present( UINT feature );
extern TEB32 *wow64_get_teb32(void);
extern PEB32 *wow64_get_peb32(void);
extern NTSTATUS wow64_init_process_reactos( SYSTEM_DLL_INIT_BLOCK **block );
extern void wow64_init_thread_reactos( CONTEXT *context64, BOOL first_thread );
NTSYSAPI NTSTATUS WINAPI NtResumeProcess( HANDLE process );
NTSYSAPI NTSTATUS WINAPI NtQueryDebugFilterState( ULONG component_id, ULONG level );
NTSYSAPI NTSTATUS WINAPI NtSuspendProcess( HANDLE process );
/* (helpers of Wine's thunks of system calls that ReactOS does not have) */
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define get_teb32() wow64_get_teb32()
#define RtlFindExportedRoutineByName wow64_find_export
#define RtlIsCurrentProcess wow64_is_current_process
#define RtlIsProcessorFeaturePresent wow64_is_processor_feature_present
#undef GetCurrentProcess
#undef GetCurrentThread
#undef GetProcessHeap
#define GetCurrentProcess() NtCurrentProcess()
#define GetCurrentThread() NtCurrentThread()
#define GetProcessHeap() (NtCurrentTeb()->Peb->ProcessHeap)
#else
static inline const WCHAR *get_machine_wow64_dir( USHORT machine )
{
    switch (machine)
    {
    case IMAGE_FILE_MACHINE_TARGET_HOST: return L"\\??\\C:\\windows\\system32";
    case IMAGE_FILE_MACHINE_I386:        return L"\\??\\C:\\windows\\syswow64";
    case IMAGE_FILE_MACHINE_ARMNT:       return L"\\??\\C:\\windows\\sysarm32";
    default: return NULL;
    }
}
#endif

#ifndef __REACTOS__
#define get_teb32() ((TEB32 *)((char *)NtCurrentTeb() + NtCurrentTeb()->WowTebOffset))
#endif

static inline ULONG get_ulong( UINT **args ) { return *(*args)++; }
static inline HANDLE get_handle( UINT **args ) { return LongToHandle( *(*args)++ ); }
static inline void *get_ptr( UINT **args ) { return ULongToPtr( *(*args)++ ); }

static inline ULONG64 get_ulong64( UINT **args )
{
    ULONG64 ret;

    *args = (UINT *)(((ULONG_PTR)*args + args_alignment - 1) & ~(args_alignment - 1));
    ret = *(ULONG64 *)*args;
    *args += 2;
    return ret;
}

static inline ULONG_PTR get_zero_bits( ULONG_PTR zero_bits )
{
    return zero_bits ? zero_bits : default_zero_bits;
}

static inline void **addr_32to64( void **addr, ULONG *addr32 )
{
    if (!addr32) return NULL;
    *addr = ULongToPtr( *addr32 );
    return addr;
}

static inline SIZE_T *size_32to64( SIZE_T *size, ULONG *size32 )
{
    if (!size32) return NULL;
    *size = *size32;
    return size;
}

static inline void *apc_32to64( ULONG func )
{
    return func ? Wow64ApcRoutine : NULL;
}

static inline void *apc_param_32to64( ULONG func, ULONG context )
{
    if (!func) return ULongToPtr( context );
    return (void *)(ULONG_PTR)(((ULONG64)func << 32) | context);
}

#ifdef __REACTOS__
/* The ReactOS I/O manager completes the IRPs of a 32-bit process like Wine's
 * Unix side: it writes the 32-bit block itself (io->Pointer, then sets that to
 * NULL), and leaves the 64-bit one alone. Other paths (fast I/O...) write the
 * 64-bit one. This value in Information tells which one happened. */
#define IOSB_NOT_WRITTEN ((ULONG_PTR)0xbadb10cbadb10c00ull)
#endif

static inline IO_STATUS_BLOCK *iosb_32to64( IO_STATUS_BLOCK *io, IO_STATUS_BLOCK32 *io32 )
{
    if (!io32) return NULL;
    io->Pointer = io32;
#ifdef __REACTOS__
    io->Information = IOSB_NOT_WRITTEN;
#endif
    return io;
}

static inline UNICODE_STRING *unicode_str_32to64( UNICODE_STRING *str, const UNICODE_STRING32 *str32 )
{
    if (!str32) return NULL;
    str->Length = str32->Length;
    str->MaximumLength = str32->MaximumLength;
    str->Buffer = ULongToPtr( str32->Buffer );
    return str;
}

static inline CLIENT_ID *client_id_32to64( CLIENT_ID *id, const CLIENT_ID32 *id32 )
{
    if (!id32) return NULL;
    id->UniqueProcess = LongToHandle( id32->UniqueProcess );
    id->UniqueThread = LongToHandle( id32->UniqueThread );
    return id;
}

/* A SID or ACL of a 32-bit program, copied to memory with the alignment the
 * kernel asks for: in 32-bit code an ACL is only 2-byte aligned (WiX Burn
 * keeps one on its stack), and NtSetSecurityObject then failed with
 * STATUS_DATATYPE_MISALIGNMENT. Windows' wow64 copies them too. */
static inline void *secdesc_copy_aligned( const void *data, ULONG size )
{
    void *copy;

    if (!data || !size) return (void *)data;
    if (!((ULONG_PTR)data & 3)) return (void *)data;
    copy = Wow64AllocateTemp( size );
    if (copy) memcpy( copy, data, size );
    return copy ? copy : (void *)data;
}

static inline PSID secdesc_sid_32to64( const void *sid )
{
    return sid ? secdesc_copy_aligned( sid, RtlLengthSid( (PSID)sid ) ) : NULL;
}

static inline PACL secdesc_acl_32to64( const void *acl )
{
    return acl ? secdesc_copy_aligned( acl, ((const ACL *)acl)->AclSize ) : NULL;
}

static inline SECURITY_DESCRIPTOR *secdesc_32to64( SECURITY_DESCRIPTOR *out, const SECURITY_DESCRIPTOR *in )
{
    /* relative descr has the same layout for 32 and 64 */
    const SECURITY_DESCRIPTOR_RELATIVE *sd = (const SECURITY_DESCRIPTOR_RELATIVE *)in;

    if (!in) return NULL;
    out->Revision = sd->Revision;
    out->Sbz1     = sd->Sbz1;
    out->Control  = sd->Control & ~SE_SELF_RELATIVE;
    if (sd->Control & SE_SELF_RELATIVE)
    {
        out->Owner = sd->Owner ? secdesc_sid_32to64( (BYTE *)sd + sd->Owner ) : NULL;
        out->Group = sd->Group ? secdesc_sid_32to64( (BYTE *)sd + sd->Group ) : NULL;
        out->Sacl = ((sd->Control & SE_SACL_PRESENT) && sd->Sacl) ? secdesc_acl_32to64( (BYTE *)sd + sd->Sacl ) : NULL;
        out->Dacl = ((sd->Control & SE_DACL_PRESENT) && sd->Dacl) ? secdesc_acl_32to64( (BYTE *)sd + sd->Dacl ) : NULL;
    }
    else
    {
        out->Owner = secdesc_sid_32to64( ULongToPtr( sd->Owner ));
        out->Group = secdesc_sid_32to64( ULongToPtr( sd->Group ));
        out->Sacl = (sd->Control & SE_SACL_PRESENT) ? secdesc_acl_32to64( ULongToPtr( sd->Sacl )) : NULL;
        out->Dacl = (sd->Control & SE_DACL_PRESENT) ? secdesc_acl_32to64( ULongToPtr( sd->Dacl )) : NULL;
    }
    return out;
}

static inline OBJECT_ATTRIBUTES *objattr_32to64( struct object_attr64 *out, const OBJECT_ATTRIBUTES32 *in )
{
    memset( out, 0, sizeof(*out) );
    if (!in) return NULL;
    if (in->Length != sizeof(*in)) return &out->attr;

    out->attr.Length = sizeof(out->attr);
    out->attr.RootDirectory = LongToHandle( in->RootDirectory );
    out->attr.Attributes = in->Attributes;
    out->attr.ObjectName = unicode_str_32to64( &out->str, ULongToPtr( in->ObjectName ));
    out->attr.SecurityQualityOfService = ULongToPtr( in->SecurityQualityOfService );
    out->attr.SecurityDescriptor = secdesc_32to64( &out->sd, ULongToPtr( in->SecurityDescriptor ));
    return &out->attr;
}

static inline OBJECT_ATTRIBUTES *objattr_32to64_redirect( struct object_attr64 *out,
                                                          const OBJECT_ATTRIBUTES32 *in )
{
    OBJECT_ATTRIBUTES *attr = objattr_32to64( out, in );

    if (attr) get_file_redirect( attr );
    return attr;
}

static inline TOKEN_USER *token_user_32to64( TOKEN_USER *out, const TOKEN_USER32 *in )
{
    out->User.Sid = ULongToPtr( in->User.Sid );
    out->User.Attributes = in->User.Attributes;
    return out;
}

static inline TOKEN_OWNER *token_owner_32to64( TOKEN_OWNER *out, const TOKEN_OWNER32 *in )
{
    out->Owner = ULongToPtr( in->Owner );
    return out;
}

static inline TOKEN_PRIMARY_GROUP *token_primary_group_32to64( TOKEN_PRIMARY_GROUP *out, const TOKEN_PRIMARY_GROUP32 *in )
{
    out->PrimaryGroup = ULongToPtr( in->PrimaryGroup );
    return out;
}

static inline TOKEN_DEFAULT_DACL *token_default_dacl_32to64( TOKEN_DEFAULT_DACL *out, const TOKEN_DEFAULT_DACL32 *in )
{
    out->DefaultDacl = ULongToPtr( in->DefaultDacl );
    return out;
}

static inline void put_handle( ULONG *handle32, HANDLE handle )
{
    *handle32 = HandleToULong( handle );
}

static inline void put_addr( ULONG *addr32, void *addr )
{
    if (addr32) *addr32 = PtrToUlong( addr );
}

static inline void put_size( ULONG *size32, SIZE_T size )
{
    if (size32) *size32 = min( size, MAXDWORD );
}

static inline void put_client_id( CLIENT_ID32 *id32, const CLIENT_ID *id )
{
    if (!id32) return;
    id32->UniqueProcess = HandleToLong( id->UniqueProcess );
    id32->UniqueThread = HandleToLong( id->UniqueThread );
}

static inline void put_iosb( IO_STATUS_BLOCK32 *io32, const IO_STATUS_BLOCK *io )
{
    /* sync I/O modifies the 64-bit iosb right away, so in that case we update the 32-bit one */
    /* async I/O leaves the 64-bit one untouched and updates the 32-bit one directly later on */
    if (io32 && io->Pointer != io32
#ifdef __REACTOS__
        && io->Information != IOSB_NOT_WRITTEN
#endif
       )
    {
        io32->Status = io->Status;
        io32->Information = io->Information;
    }
}

extern void put_section_image_info( SECTION_IMAGE_INFORMATION32 *info32,
                                    const SECTION_IMAGE_INFORMATION *info );
extern void put_vm_counters( VM_COUNTERS_EX32 *info32, const VM_COUNTERS_EX *info,
                             ULONG size );

#endif /* __WOW64_PRIVATE_H */

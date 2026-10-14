/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The jobs that Wine's Unix side does for wow64.dll: load the
 *              32-bit ntdll, give it the process parameters, and give each
 *              thread its 32-bit stack and its first 32-bit context
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
#include "wine/asm.h"
#include "wine/exception.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wow);

/* The 32-bit system call stubs of ReactOS' ntdll go through the system call
 * pointer of KUSER_SHARED_DATA:
 *     B8 nn nn nn nn       mov eax, <system call number>
 *     BA 00 03 FE 7F       mov edx, 7FFE0300h
 *     FF 12                call dword ptr [edx]
 * In a 32-bit process they call the WoW64 transition through the 32-bit
 * TEB instead, like the stubs of the 32-bit ntdll of 64-bit Windows:
 *     64 FF 15 C0 00 00 00 call dword ptr fs:[0C0h]
 */
static const BYTE native_stub[] = { 0xBA, 0x00, 0x03, 0xFE, 0x7F, 0xFF, 0x12 };
static const BYTE wow64_stub[] = { 0x64, 0xFF, 0x15, 0xC0, 0x00, 0x00, 0x00 };

#define WOW64_STACK_RESERVE (1024 * 1024)

static SYSTEM_DLL_INIT_BLOCK init_block;
static ULONG exit_thread_stub;

/* "\??\C:\ReactOS\", and the system directories below it */
static WCHAR windir[MAX_PATH], system32_dir[MAX_PATH], syswow64_dir[MAX_PATH];

static void init_dirs(void)
{
    /* KUSER_SHARED_DATA->NtSystemRoot */
    const WCHAR *root = (const WCHAR *)0x7ffe0030;

    if (windir[0]) return;
    swprintf( windir, MAX_PATH, L"\\??\\%s\\", root );
    swprintf( system32_dir, MAX_PATH, L"%ssystem32", windir );
    swprintf( syswow64_dir, MAX_PATH, L"%sSysWOW64", windir );
}

const WCHAR *wow64_windir(void)
{
    init_dirs();
    return windir;
}

const WCHAR *get_machine_wow64_dir( USHORT machine )
{
    init_dirs();
    switch (machine)
    {
    case IMAGE_FILE_MACHINE_TARGET_HOST:
    case IMAGE_FILE_MACHINE_AMD64: return system32_dir;
    case IMAGE_FILE_MACHINE_I386:  return syswow64_dir;
    default: return NULL;
    }
}

extern void wow64_init_csr( HMODULE ntdll32 );

/* What Wine's ntdll exports and ReactOS' does not */

void *wow64_find_export( HMODULE module, const char *name )
{
    const IMAGE_EXPORT_DIRECTORY *exports;
    const DWORD *names, *functions;
    const WORD *ordinals;
    ULONG size, i;

    exports = RtlImageDirectoryEntryToData( module, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &size );
    if (!exports) return NULL;
    names = (const DWORD *)((const char *)module + exports->AddressOfNames);
    functions = (const DWORD *)((const char *)module + exports->AddressOfFunctions);
    ordinals = (const WORD *)((const char *)module + exports->AddressOfNameOrdinals);
    for (i = 0; i < exports->NumberOfNames; i++)
    {
        if (!strcmp( (const char *)module + names[i], name ))
            return (char *)module + functions[ordinals[i]];
    }
    return NULL;
}

BOOLEAN wow64_is_current_process( HANDLE handle )
{
    PROCESS_BASIC_INFORMATION info;

    if (handle == NtCurrentProcess()) return TRUE;
    if (NtQueryInformationProcess( handle, ProcessBasicInformation, &info, sizeof(info), NULL ))
        return FALSE;
    return info.UniqueProcessId == (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
}

BOOLEAN wow64_is_processor_feature_present( UINT feature )
{
    /* KUSER_SHARED_DATA->ProcessorFeatures */
    return feature < 64 && ((const BOOLEAN *)0x7ffe0274)[feature];
}

TEB32 *wow64_get_teb32(void)
{
    /* The kernel puts the 32-bit TEB right after the 64-bit one */
    return (TEB32 *)((char *)NtCurrentTeb() + 0x2000);
}

PEB32 *wow64_get_peb32(void)
{
    return ULongToPtr( wow64_get_teb32()->Peb );
}

/* Make the sections of a 32-bit image writable, or put back their protection.
 * (One call per section: each one is a memory area of its own.) */
void wow64_protect_image32( void *base, BOOL writable )
{
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)RtlImageNtHeader( base );
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION( nt );
    UINT i;

    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        void *addr = (char *)base + sec->VirtualAddress;
        SIZE_T size = max( sec->Misc.VirtualSize, 1 );
        ULONG prot, old_prot;

        if (writable) prot = PAGE_EXECUTE_READWRITE;
        else if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            prot = (sec->Characteristics & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        else
            prot = (sec->Characteristics & IMAGE_SCN_MEM_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
        NtProtectVirtualMemory( NtCurrentProcess(), &addr, &size, prot, &old_prot );
    }
}

/* Apply the base relocations of a 32-bit image mapped away from its base */
static NTSTATUS relocate_image32( void *base )
{
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)RtlImageNtHeader( base );
    IMAGE_BASE_RELOCATION *rel, *end;
    LONG delta = PtrToUlong( base ) - nt->OptionalHeader.ImageBase;
    ULONG rel_size;

    rel = RtlImageDirectoryEntryToData( base, TRUE, IMAGE_DIRECTORY_ENTRY_BASERELOC, &rel_size );
    if (!rel) return STATUS_CONFLICTING_ADDRESSES;
    end = (IMAGE_BASE_RELOCATION *)((char *)rel + rel_size);

    wow64_protect_image32( base, TRUE );
    while (rel < end && rel->SizeOfBlock)
    {
        const USHORT *entry = (const USHORT *)(rel + 1);
        ULONG count = (rel->SizeOfBlock - sizeof(*rel)) / sizeof(USHORT);

        while (count--)
        {
            if ((*entry >> 12) == IMAGE_REL_BASED_HIGHLOW)
                *(ULONG *)((char *)base + rel->VirtualAddress + (*entry & 0xfff)) += delta;
            entry++;
        }
        rel = (IMAGE_BASE_RELOCATION *)((char *)rel + rel->SizeOfBlock);
    }
    wow64_protect_image32( base, FALSE );
    return STATUS_SUCCESS;
}

/* Map the 32-bit ntdll (Wine's Unix side does it) */
static NTSTATUS map_ntdll32( HMODULE *module )
{
    UNICODE_STRING name = RTL_CONSTANT_STRING( L"\\SystemRoot\\SysWOW64\\ntdll.dll" );
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    HANDLE file, section;
    void *base = NULL;
    SIZE_T size = 0;
    NTSTATUS status;

    InitializeObjectAttributes( &attr, &name, OBJ_CASE_INSENSITIVE, 0, NULL );
    status = NtOpenFile( &file, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, &attr, &io,
                         FILE_SHARE_READ | FILE_SHARE_DELETE,
                         FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE );
    if (status)
    {
        ERR( "cannot open %wZ: %lx\n", &name, status );
        return status;
    }
    status = NtCreateSection( &section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY,
                              NULL, NULL, PAGE_EXECUTE, SEC_IMAGE, file );
    NtClose( file );
    if (status) return status;
    status = NtMapViewOfSection( section, NtCurrentProcess(), &base, default_zero_bits, 0, NULL,
                                 &size, ViewShare, 0, PAGE_EXECUTE_WRITECOPY );
    NtClose( section );
    if (status == STATUS_IMAGE_NOT_AT_BASE) status = relocate_image32( base );
    if (!NT_SUCCESS( status )) return status;
    *module = base;
    return STATUS_SUCCESS;
}

/* Make the system call stubs of the 32-bit ntdll call the WoW64 transition */
static void patch_syscall_stubs( HMODULE module )
{
    const IMAGE_EXPORT_DIRECTORY *exports;
    const DWORD *names, *functions;
    const WORD *ordinals;
    ULONG size, i, count = 0;

    exports = RtlImageDirectoryEntryToData( module, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &size );
    if (!exports) return;
    names = (const DWORD *)((const char *)module + exports->AddressOfNames);
    functions = (const DWORD *)((const char *)module + exports->AddressOfFunctions);
    ordinals = (const WORD *)((const char *)module + exports->AddressOfNameOrdinals);

    wow64_protect_image32( module, TRUE );
    for (i = 0; i < exports->NumberOfNames; i++)
    {
        const char *name = (const char *)module + names[i];
        BYTE *code = (BYTE *)module + functions[ordinals[i]];

        if ((name[0] != 'N' || name[1] != 't') && (name[0] != 'Z' || name[1] != 'w')) continue;
        if (code[0] != 0xb8 || memcmp( code + 5, native_stub, sizeof(native_stub) )) continue;
        memcpy( code + 5, wow64_stub, sizeof(wow64_stub) );
        count++;
    }
    wow64_protect_image32( module, FALSE );
    TRACE( "%lu system call stubs\n", count );
}

/* A 32-bit thread that returns from its start routine exits:
 *     50                   push eax
 *     FF 15 xx xx xx xx    call dword ptr [RtlExitUserThread]
 */
static void init_exit_thread_stub( HMODULE ntdll32 )
{
    BYTE *code = NULL;
    SIZE_T size = 0x1000;
    ULONG old_prot;

    if (NtAllocateVirtualMemory( NtCurrentProcess(), (void **)&code, default_zero_bits, &size,
                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ))
        return;
    *(ULONG *)(code + 16) = PtrToUlong( wow64_find_export( ntdll32, "RtlExitUserThread" ));
    code[0] = 0x50;
    code[1] = 0xff;
    code[2] = 0x15;
    *(ULONG *)(code + 3) = PtrToUlong( code + 16 );
    code[7] = 0xcc;
    NtProtectVirtualMemory( NtCurrentProcess(), (void **)&code, &size, PAGE_EXECUTE_READ, &old_prot );
    exit_thread_stub = PtrToUlong( code );
}

/* The 32-bit copy of the process parameters (Wine's Unix side makes it).
 * The strings are copied too, below 2 GB, and the copy is normalized. */
/* Environment variable Name = the value of variable From (if From exists) */
static void set_env_from( const WCHAR *name, const WCHAR *from )
{
    UNICODE_STRING name_str, from_str, value;
    WCHAR buffer[MAX_PATH];

    RtlInitUnicodeString( &name_str, name );
    RtlInitUnicodeString( &from_str, from );
    value.Buffer = buffer;
    value.Length = 0;
    value.MaximumLength = sizeof(buffer) - sizeof(WCHAR);
    if (!RtlQueryEnvironmentVariable_U( NULL, &from_str, &value ))
        RtlSetEnvironmentVariable( NULL, &name_str, &value );
}

static NTSTATUS init_process_params32( PEB32 *peb32 )
{
    RTL_USER_PROCESS_PARAMETERS *params = NtCurrentTeb()->Peb->ProcessParameters;
    RTL_USER_PROCESS_PARAMETERS32 *params32 = NULL;
    const UNICODE_STRING *strings[] =
    {
        &params->CurrentDirectory.DosPath, &params->DllPath, &params->ImagePathName,
        &params->CommandLine, &params->WindowTitle, &params->Desktop, &params->ShellInfo,
        &params->RuntimeInfo
    };
    UNICODE_STRING32 *strings32[ARRAY_SIZE(strings)];
    SIZE_T size = sizeof(*params32), env_size = 0;
    const WCHAR *env = params->Environment;
    char *ptr;
    NTSTATUS status;
    UINT i;

    for (i = 0; i < ARRAY_SIZE(strings); i++) size += (strings[i]->MaximumLength + 3) & ~3;

    /* The environment, if it is out of reach */
    if (env && (ULONG_PTR)env + 0x10000 > 0x80000000)
    {
        const WCHAR *end = env;
        while (*end) end += wcslen( end ) + 1;
        env_size = (end + 1 - env) * sizeof(WCHAR);
        size += env_size;
    }

    status = NtAllocateVirtualMemory( NtCurrentProcess(), (void **)&params32, default_zero_bits, &size,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if (status) return status;

    params32->AllocationSize   = size;
    params32->Size             = sizeof(*params32);
    params32->Flags            = params->Flags | PROCESS_PARAMS_FLAG_NORMALIZED;
    params32->DebugFlags       = params->DebugFlags;
    params32->ConsoleHandle    = HandleToULong( params->ConsoleHandle );
    params32->ConsoleFlags     = params->ConsoleFlags;
    params32->hStdInput        = HandleToULong( params->hStdInput );
    params32->hStdOutput       = HandleToULong( params->hStdOutput );
    params32->hStdError        = HandleToULong( params->hStdError );
    params32->CurrentDirectory.Handle = HandleToULong( params->CurrentDirectory.Handle );
    params32->dwX              = params->dwX;
    params32->dwY              = params->dwY;
    params32->dwXSize          = params->dwXSize;
    params32->dwYSize          = params->dwYSize;
    params32->dwXCountChars    = params->dwXCountChars;
    params32->dwYCountChars    = params->dwYCountChars;
    params32->dwFillAttribute  = params->dwFillAttribute;
    params32->dwFlags          = params->dwFlags;
    params32->wShowWindow      = params->wShowWindow;

    strings32[0] = &params32->CurrentDirectory.DosPath;
    strings32[1] = &params32->DllPath;
    strings32[2] = &params32->ImagePathName;
    strings32[3] = &params32->CommandLine;
    strings32[4] = &params32->WindowTitle;
    strings32[5] = &params32->Desktop;
    strings32[6] = &params32->ShellInfo;
    strings32[7] = &params32->RuntimeInfo;

    ptr = (char *)(params32 + 1);
    for (i = 0; i < ARRAY_SIZE(strings); i++)
    {
        strings32[i]->Length = strings[i]->Length;
        strings32[i]->MaximumLength = strings[i]->MaximumLength;
        if (!strings[i]->Buffer) continue;
        memcpy( ptr, strings[i]->Buffer, strings[i]->MaximumLength );
        strings32[i]->Buffer = PtrToUlong( ptr );
        ptr += (strings[i]->MaximumLength + 3) & ~3;
    }

    if (env_size)
    {
        memcpy( ptr, env, env_size );
        params32->Environment = PtrToUlong( ptr );
    }
    else params32->Environment = PtrToUlong( env );

    peb32->ProcessParameters = PtrToUlong( params32 );
    return STATUS_SUCCESS;
}

/* Called by process_init once wow64cpu.dll is ready */
NTSTATUS wow64_init_process_reactos( SYSTEM_DLL_INIT_BLOCK **block )
{
    HMODULE ntdll32;
    NTSTATUS status;

    status = map_ntdll32( &ntdll32 );
    if (status) return status;

    init_block.ntdll_handle               = (ULONG_PTR)ntdll32;
    init_block.pLdrInitializeThunk        = PtrToUlong( wow64_find_export( ntdll32, "LdrInitializeThunk" ));
    init_block.pKiUserExceptionDispatcher = PtrToUlong( wow64_find_export( ntdll32, "KiUserExceptionDispatcher" ));
    init_block.pKiUserApcDispatcher       = PtrToUlong( wow64_find_export( ntdll32, "KiUserApcDispatcher" ));
    init_block.pKiUserCallbackDispatcher  = PtrToUlong( wow64_find_export( ntdll32, "KiUserCallbackDispatcher" ));
    *block = &init_block;

    patch_syscall_stubs( ntdll32 );
    wow64_init_csr( ntdll32 );
    init_exit_thread_stub( ntdll32 );
    /* A 32-bit program sees the x86 program folders as its own (as on Windows),
     * before its parameters get their 32-bit copy */
    set_env_from( L"ProgramFiles", L"ProgramFiles(x86)" );
    set_env_from( L"CommonProgramFiles", L"CommonProgramFiles(x86)" );
    return init_process_params32( wow64_get_peb32() );
}

/* A thread created by 32-bit code: what its 64-bit side starts with */
#define WOW64_THREAD_START_MAGIC 0x54343657  /* "W64T" */

struct wow64_thread_start
{
    ULONG        magic;
    ULONG        stack_base;
    ULONG        stack_limit;
    ULONG        stack_allocation;
    I386_CONTEXT context;
};

typedef struct
{
    ULONG PreviousStackBase;
    ULONG PreviousStackLimit;
    ULONG StackBase;
    ULONG StackLimit;
    ULONG AllocatedStackBase;
} INITIAL_TEB32;

static void WINAPI wow64_thread_start( void *param )
{
    /* Never runs: Wow64LdrpInitialize takes the thread over before */
}

/**********************************************************************
 *           wow64_NtCreateThread
 *
 * The 32-bit kernel32 gives the 32-bit context and stack of the thread.
 * The thread gets a 64-bit stack and starts in the 64-bit ntdll like any
 * other; its 64-bit start parameter carries the 32-bit context and stack.
 */
NTSTATUS WINAPI wow64_NtCreateThread( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE process = get_handle( &args );
    CLIENT_ID32 *id32 = get_ptr( &args );
    I386_CONTEXT *context = get_ptr( &args );
    INITIAL_TEB32 *initial_teb = get_ptr( &args );
    BOOLEAN suspended = get_ulong( &args );

    struct wow64_thread_start start;
    void *remote = NULL;
    SIZE_T size = sizeof(start);
    HANDLE handle = 0;
    CLIENT_ID id;
    NTSTATUS status;

    (void)access;
    (void)attr32;

    /* The start block goes to the process of the thread (the first thread of
     * a new process, created by its 32-bit parent, ignores it: it starts at
     * the entry point of its image, see wow64_init_thread_reactos) */
    start.magic = WOW64_THREAD_START_MAGIC;
    start.stack_base = initial_teb->StackBase;
    start.stack_limit = initial_teb->StackLimit;
    start.stack_allocation = initial_teb->AllocatedStackBase;
    start.context = *context;
    status = NtAllocateVirtualMemory( process, &remote, 0, &size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if (status) return status;
    status = NtWriteVirtualMemory( process, remote, &start, sizeof(start), NULL );
    if (status) return status;

    status = RtlCreateUserThread( process, NULL, TRUE, 0, 0x40000, 0x4000,
                                  (PRTL_THREAD_START_ROUTINE)wow64_thread_start, remote, &handle, &id );
    if (status)
    {
        size = 0;
        NtFreeVirtualMemory( process, &remote, &size, MEM_RELEASE );
        return status;
    }
    if (!suspended) NtResumeThread( handle, NULL );

    *handle_ptr = HandleToULong( handle );
    if (id32)
    {
        id32->UniqueProcess = HandleToULong( id.UniqueProcess );
        id32->UniqueThread = HandleToULong( id.UniqueThread );
    }
    return STATUS_SUCCESS;
}

/* stack layout when calling the 32-bit KiUserApcDispatcher of ReactOS */
struct apc_stack_layout32
{
    ULONG        func;          /* NormalRoutine */
    ULONG        arg1;          /* NormalContext */
    ULONG        arg2;          /* SystemArgument1 */
    ULONG        arg3;          /* SystemArgument2 */
    I386_CONTEXT context;
    ULONG        seh_frame[2];  /* for KiUserApcDispatcher */
};

/* Set up the 32-bit side of a new thread: its CPU area, its stack, and a
 * context that runs the 32-bit LdrInitializeThunk, then the thread's start
 * routine, like the i386 kernel does for a new thread */
void wow64_init_thread_reactos( CONTEXT *context64, BOOL first_thread )
{
    TEB32 *teb32 = wow64_get_teb32();
    PEB32 *peb32 = wow64_get_peb32();
    WOW64_CPURESERVED *cpu;
    I386_CONTEXT ctx;
    struct apc_stack_layout32 *stack;
    ULONG *top, start, arg = 0;

    cpu = RtlAllocateHeap( GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*cpu) + sizeof(I386_CONTEXT) + 0x100 );
    cpu->Machine = IMAGE_FILE_MACHINE_I386;
    NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED] = cpu;

    if (first_thread && !teb32->Tib.StackBase)
    {
        IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)RtlImageNtHeader( ULongToPtr( peb32->ImageBaseAddress ));
        SIZE_T size = max( nt->OptionalHeader.SizeOfStackReserve, WOW64_STACK_RESERVE );
        void *base = NULL;

        /* FIXME: commit on demand, with a guard page */
        if (NtAllocateVirtualMemory( NtCurrentProcess(), &base, default_zero_bits, &size,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ))
        {
            ERR( "no 32-bit stack\n" );
            NtTerminateThread( NtCurrentThread(), STATUS_NO_MEMORY );
        }
        teb32->Tib.StackBase = PtrToUlong( (char *)base + size );
        teb32->Tib.StackLimit = PtrToUlong( base );
        teb32->DeallocationStack = PtrToUlong( base );
    }

    if (first_thread)
    {
        IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)RtlImageNtHeader( ULongToPtr( peb32->ImageBaseAddress ));
        start = peb32->ImageBaseAddress + nt->OptionalHeader.AddressOfEntryPoint;
    }
    else if (context64->Rip == (ULONG_PTR)wow64_thread_start &&
             ((struct wow64_thread_start *)context64->Rcx)->magic == WOW64_THREAD_START_MAGIC)
    {
        /* A thread created by 32-bit code: its own context and stack */
        struct wow64_thread_start *thread_start = (struct wow64_thread_start *)context64->Rcx;

        teb32->Tib.StackBase = thread_start->stack_base;
        teb32->Tib.StackLimit = thread_start->stack_limit;
        teb32->DeallocationStack = thread_start->stack_allocation;
        void *addr = thread_start;
        SIZE_T size = 0;

        ctx = thread_start->context;
        NtFreeVirtualMemory( NtCurrentProcess(), &addr, &size, MEM_RELEASE );
        goto run;
    }
    else if (context64->Rip <= 0xffffffff)
    {
        /* A thread that 64-bit code created at a 32-bit routine, with
         * RtlCreateUserThread (start routine in Rip, parameter in Rcx): the
         * Ctrl+C handler that the console server starts, a debugger... It
         * gets its own 32-bit stack. */
        SIZE_T size = 0x40000;
        void *base = NULL;

        if (NtAllocateVirtualMemory( NtCurrentProcess(), &base, default_zero_bits, &size,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ))
        {
            ERR( "no 32-bit stack\n" );
            NtTerminateThread( NtCurrentThread(), STATUS_NO_MEMORY );
            return;
        }
        teb32->Tib.StackBase = PtrToUlong( (char *)base + size );
        teb32->Tib.StackLimit = PtrToUlong( base );
        teb32->DeallocationStack = PtrToUlong( base );
        start = (ULONG)context64->Rip;
        arg = (ULONG)context64->Rcx;
    }
    else
    {
        /* (64-bit code, e.g. the 64-bit kernel32's thread thunk: there is no
         * 64-bit kernel32 in a 32-bit process) */
        ERR( "64-bit thread start %p in a 32-bit process\n", (void *)context64->Rip );
        NtTerminateThread( NtCurrentThread(), STATUS_NOT_SUPPORTED );
        return;
    }

    /* The start routine returns into the exit stub */
    top = ULongToPtr( teb32->Tib.StackBase );
    *--top = arg;
    *--top = exit_thread_stub;

    memset( &ctx, 0, sizeof(ctx) );
    ctx.ContextFlags = CONTEXT_I386_FULL;
    ctx.SegCs  = 0x23;
    ctx.SegDs  = ctx.SegEs = ctx.SegSs = ctx.SegGs = 0x2b;
    ctx.SegFs  = 0x53;
    ctx.EFlags = 0x202;
    ctx.Eip    = start;
    ctx.Eax    = start;
    ctx.Ebx    = PtrToUlong( peb32 );
    ctx.Esp    = PtrToUlong( top );

run:
    ctx.ContextFlags |= CONTEXT_I386_FULL;
    stack = (struct apc_stack_layout32 *)ULongToPtr( ctx.Esp & ~3 ) - 1;
    stack->func    = init_block.pLdrInitializeThunk;
    stack->arg1    = 0;
    stack->arg2    = init_block.ntdll_handle;
    stack->arg3    = 0;
    stack->context = ctx;

    ctx.Esp = PtrToUlong( stack );
    ctx.Eip = init_block.pKiUserApcDispatcher;
    RtlWow64SetThreadContext( NtCurrentThread(), &ctx );
}

/* Wine's debug channels, on top of DbgPrint (libwine needs kernel32, which a
 * 32-bit process does not load in 64-bit form) */

unsigned char __wine_dbg_get_channel_flags( struct __wine_debug_channel *channel )
{
    /* Errors and fixmes; WOW64_DEBUG=trace (in the program's environment) also
     * prints every system call, the 32-bit stacks of exceptions... (slow: the
     * debug log goes to a serial port) */
    static LONG flags = -1;

    if (flags == -1)
    {
        UNICODE_STRING name = RTL_CONSTANT_STRING( L"WOW64_DEBUG" );
        UNICODE_STRING value;
        WCHAR buffer[32];
        LONG new_flags = (1 << __WINE_DBCL_ERR) | (1 << __WINE_DBCL_FIXME);

        value.Buffer = buffer;
        value.Length = 0;
        value.MaximumLength = sizeof(buffer) - sizeof(WCHAR);
        if (!RtlQueryEnvironmentVariable_U( NULL, &name, &value ))
        {
            buffer[value.Length / sizeof(WCHAR)] = 0;
            if (wcsstr( buffer, L"trace" )) new_flags |= (1 << __WINE_DBCL_TRACE) | (1 << __WINE_DBCL_WARN);
            if (wcsstr( buffer, L"warn" )) new_flags |= (1 << __WINE_DBCL_WARN);
        }
        flags = new_flags;
    }
    return (unsigned char)flags;
}

int ros_dbg_log( enum __wine_debug_class cls, struct __wine_debug_channel *channel, const char *file,
                 const char *func, const int line, const char *format, ... )
{
    static const char * const classes[] = { "fixme", "err", "warn", "trace" };
    char prefix[128];
    va_list args;
    int ret;

    /* (ReactOS' TRACE() etc. only test the channel's static flags) */
    if (!(__wine_dbg_get_channel_flags( channel ) & (1 << cls))) return 0;

    _snprintf( prefix, sizeof(prefix), "(wow64:%s) %s:%s ",
               cls < ARRAY_SIZE(classes) ? classes[cls] : "", channel->name, func );
    va_start( args, format );
    ret = vDbgPrintExWithPrefix( prefix, -1, 0, format, args );
    va_end( args );
    return ret;
}

static char *get_temp_buffer( size_t size )
{
    static char buffers[32][256];
    static LONG next;
    LONG index = InterlockedIncrement( &next ) % ARRAY_SIZE(buffers);

    return buffers[index];
}

const char *wine_dbg_sprintf( const char *format, ... )
{
    char *buffer = get_temp_buffer( 256 );
    va_list args;

    va_start( args, format );
    _vsnprintf( buffer, 255, format, args );
    va_end( args );
    buffer[255] = 0;
    return buffer;
}

const char *wine_dbgstr_an( const char *str, int n )
{
    if (!str) return "(null)";
    if (!((ULONG_PTR)str >> 16)) return wine_dbg_sprintf( "#%04x", LOWORD(str) );
    if (n < 0) n = strlen( str );
    return wine_dbg_sprintf( "\"%.*s\"", min( n, 200 ), str );
}

const char *wine_dbgstr_wn( const WCHAR *str, int n )
{
    if (!str) return "(null)";
    if (!((ULONG_PTR)str >> 16)) return wine_dbg_sprintf( "#%04x", LOWORD(str) );
    if (n < 0) n = wcslen( str );
    return wine_dbg_sprintf( "L\"%.*S\"", min( n, 200 ), str );
}

/* Stand-ins for what Wine's ntdll has (see wow64_private.h) */

void WINAPI wow64_open_cross_process_work( HANDLE process, HANDLE *section, void **addr )
{
    *section = 0;
    *addr = NULL;
}

CROSS_PROCESS_WORK_ENTRY * WINAPI wow64_pop_all_cross_process_work( CROSS_PROCESS_WORK_HDR *list, BOOLEAN *flush )
{
    *flush = FALSE;
    return NULL;
}

CROSS_PROCESS_WORK_ENTRY * WINAPI wow64_pop_cross_process_free( CROSS_PROCESS_WORK_HDR *list )
{
    return NULL;
}

BOOLEAN WINAPI wow64_push_cross_process_free( CROSS_PROCESS_WORK_HDR *list, CROSS_PROCESS_WORK_ENTRY *entry )
{
    return FALSE;
}

BOOLEAN WINAPI wow64_push_cross_process_work( CROSS_PROCESS_WORK_HDR *list, CROSS_PROCESS_WORK_ENTRY *entry, void **unknown )
{
    return FALSE;
}

NTSTATUS WINAPI wow64_get_thread_selector_entry( HANDLE thread, THREAD_DESCRIPTOR_INFORMATION *info, ULONG size, ULONG *ret_size )
{
    FIXME( "%p\n", thread );
    return STATUS_NOT_IMPLEMENTED;
}

/* Wine's setjmp/longjmp for the user callbacks (as in Wine's 64-bit ntdll) */
__ASM_GLOBAL_FUNC( __wine_setjmpex,
                   "movq %rdx,(%rcx)\n\t"          /* jmp_buf->Frame */
                   "movq %rbx,0x8(%rcx)\n\t"       /* jmp_buf->Rbx */
                   "leaq 0x8(%rsp),%rax\n\t"
                   "movq %rax,0x10(%rcx)\n\t"      /* jmp_buf->Rsp */
                   "movq %rbp,0x18(%rcx)\n\t"      /* jmp_buf->Rbp */
                   "movq %rsi,0x20(%rcx)\n\t"      /* jmp_buf->Rsi */
                   "movq %rdi,0x28(%rcx)\n\t"      /* jmp_buf->Rdi */
                   "movq %r12,0x30(%rcx)\n\t"      /* jmp_buf->R12 */
                   "movq %r13,0x38(%rcx)\n\t"      /* jmp_buf->R13 */
                   "movq %r14,0x40(%rcx)\n\t"      /* jmp_buf->R14 */
                   "movq %r15,0x48(%rcx)\n\t"      /* jmp_buf->R15 */
                   "movq (%rsp),%rax\n\t"
                   "movq %rax,0x50(%rcx)\n\t"      /* jmp_buf->Rip */
                   "stmxcsr 0x58(%rcx)\n\t"        /* jmp_buf->MxCsr */
                   "fnstcw 0x5c(%rcx)\n\t"         /* jmp_buf->FpCsr */
                   "movdqa %xmm6,0x60(%rcx)\n\t"   /* jmp_buf->Xmm6 */
                   "movdqa %xmm7,0x70(%rcx)\n\t"
                   "movdqa %xmm8,0x80(%rcx)\n\t"
                   "movdqa %xmm9,0x90(%rcx)\n\t"
                   "movdqa %xmm10,0xa0(%rcx)\n\t"
                   "movdqa %xmm11,0xb0(%rcx)\n\t"
                   "movdqa %xmm12,0xc0(%rcx)\n\t"
                   "movdqa %xmm13,0xd0(%rcx)\n\t"
                   "movdqa %xmm14,0xe0(%rcx)\n\t"
                   "movdqa %xmm15,0xf0(%rcx)\n\t"
                   "xorq %rax,%rax\n\t"
                   "retq" )

__ASM_GLOBAL_FUNC( __wine_longjmp,
                   "movq %rdx,%rax\n\t"            /* retval */
                   "movq 0x8(%rcx),%rbx\n\t"       /* jmp_buf->Rbx */
                   "movq 0x18(%rcx),%rbp\n\t"      /* jmp_buf->Rbp */
                   "movq 0x20(%rcx),%rsi\n\t"      /* jmp_buf->Rsi */
                   "movq 0x28(%rcx),%rdi\n\t"      /* jmp_buf->Rdi */
                   "movq 0x30(%rcx),%r12\n\t"      /* jmp_buf->R12 */
                   "movq 0x38(%rcx),%r13\n\t"      /* jmp_buf->R13 */
                   "movq 0x40(%rcx),%r14\n\t"      /* jmp_buf->R14 */
                   "movq 0x48(%rcx),%r15\n\t"      /* jmp_buf->R15 */
                   "ldmxcsr 0x58(%rcx)\n\t"        /* jmp_buf->MxCsr */
                   "fnclex\n\t"
                   "fldcw 0x5c(%rcx)\n\t"          /* jmp_buf->FpCsr */
                   "movdqa 0x60(%rcx),%xmm6\n\t"   /* jmp_buf->Xmm6 */
                   "movdqa 0x70(%rcx),%xmm7\n\t"
                   "movdqa 0x80(%rcx),%xmm8\n\t"
                   "movdqa 0x90(%rcx),%xmm9\n\t"
                   "movdqa 0xa0(%rcx),%xmm10\n\t"
                   "movdqa 0xb0(%rcx),%xmm11\n\t"
                   "movdqa 0xc0(%rcx),%xmm12\n\t"
                   "movdqa 0xd0(%rcx),%xmm13\n\t"
                   "movdqa 0xe0(%rcx),%xmm14\n\t"
                   "movdqa 0xf0(%rcx),%xmm15\n\t"
                   "movq 0x50(%rcx),%rdx\n\t"      /* jmp_buf->Rip */
                   "movq 0x10(%rcx),%rsp\n\t"      /* jmp_buf->Rsp */
                   "jmp *%rdx" )

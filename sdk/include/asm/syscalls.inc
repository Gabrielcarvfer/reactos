
#ifdef _M_ARM

#include <ksarm.h>

    GBLA SyscallId

SyscallId SETA 0

	MACRO
	STUB_K $Name
	LCLS ZwFuncName
	LCLS ZwFuncEndName
ZwFuncName    SETS "Zw$Name"
ZwFuncEndName SETS "$ZwFuncName":CC:"_end"
	ALIGN 2
	EXPORT $ZwFuncName [FUNC]
$ZwFuncName
	ROUT
    mov r12, #SyscallId
    svc #1
    bx lr
$ZwFuncEndName
    MEND

	MACRO
	STUB_U $Name
	LCLS NtFuncName
	LCLS NtFuncEndName
	LCLS ZwFuncName
	LCLS ZwFuncEndName
NtFuncName    SETS "Nt$Name"
NtFuncEndName SETS "$NtFuncName":CC:"_end"
ZwFuncName    SETS "Zw$Name"
ZwFuncEndName SETS "$ZwFuncName":CC:"_end"
	ALIGN 2
	EXPORT $NtFuncName [FUNC]
$NtFuncName
	EXPORT $ZwFuncName [FUNC]
$ZwFuncName
	ROUT
    mov r12, #SyscallId
    svc #1
    bx lr
$NtFuncEndName
$ZwFuncEndName
    MEND

#else

#include <asm.inc>

SyscallId = 0

#ifdef _M_IX86
#define KUSER_SHARED_SYSCALL HEX(7ffe0300)
#define KGDT_R0_CODE 8
#ifdef BUILD_WOW6432
/* 32-bit code on 64-bit ReactOS (SysWOW64): system calls go to wow64.dll,
 * through the transition that the 32-bit TEB points to (WOW32Reserved) */
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    StackBytes = 4 * ArgCount
    FPO 0, 0, 0, 0, 0, FRAME_FPO
    mov eax, SyscallId
    call dword ptr fs:[HEX(0C0)]
    ret StackBytes
ENDM
#else
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    StackBytes = 4 * ArgCount
    FPO 0, 0, 0, 0, 0, FRAME_FPO
    mov eax, SyscallId
    mov edx, KUSER_SHARED_SYSCALL
    call dword ptr [edx]
    ret StackBytes
ENDM
#endif
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    StackBytes = 4 * &ArgCount
    FPO 0, 0, 0, 0, 0, FRAME_FPO
    mov eax, SyscallId
    lea edx, [esp + 4]
    pushfd
    push KGDT_R0_CODE
    call _KiSystemService
    ret StackBytes
ENDM
#elif defined(_M_AMD64)
/* The code of Windows 10's stubs, byte for byte, in 32 byte slots:
 * Chromium's sandbox checks it before it hooks some of them in its
 * processes (SBOX_ERROR_CANNOT_SETUP_INTERCEPTION_THUNK otherwise), and
 * writes its jump over the stub. KUSER_SHARED_DATA+0x308 is 0 on
 * ReactOS: the syscall is always taken. */
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    .ENDPROLOG
    .byte HEX(4C), HEX(8B), HEX(D1)              /* mov r10, rcx */
    .byte HEX(B8)                                /* mov eax, SyscallId */
    .long SyscallId
    .byte HEX(F6), HEX(04), HEX(25)              /* test byte ptr [7FFE0308h], 1 */
    .long HEX(7FFE0308)
    .byte 1
    .byte HEX(75), 3                             /* jne (int 2Eh) */
    .byte HEX(0F), HEX(05)                       /* syscall */
    .byte HEX(C3)                                /* ret */
    .byte HEX(CD), HEX(2E)                       /* int 2Eh */
    .byte HEX(C3)                                /* ret */
    .byte HEX(0F), HEX(1F), HEX(84), 0, 0, 0, 0, 0 /* nop dword ptr [rax+rax] */
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    .ENDPROLOG
    EXTERN Nt&Name:PROC
    lea rax, Nt&Name[rip]
    mov r10, ArgCount * 8
    jmp KiZwSystemService
ENDM
#elif defined(_M_PPC)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    stwu 1,-16(1)
    mflr 0
    stw  0,0(1)
    li   0, SyscallId
    sc
    lwz 0,0(1)
    mtlr 0
    addi 1,1,16
    blr
ENDM
#define STUBCODE_K STUBCODE_U
#elif defined(_M_MIPS)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    li $8, KUSER_SHARED_SYSCALL
    lw $8,0($8)
    j $8
    nop
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    j KiSystemService
    nop
ENDM
#else
#error unsupported architecture
#endif

#ifdef _M_IX86
MACRO(MAKE_LABEL, Name, StackBytes)
    PUBLIC _&Name&@&StackBytes
    _&Name&@&StackBytes:
ENDM
MACRO(START_PROC, Name, StackBytes)
    PUBLIC _&Name&@&StackBytes
    .PROC _&Name&@&StackBytes
ENDM
#else
MACRO(MAKE_LABEL, Name, StackBytes)
    PUBLIC &Name
    &Name:
ENDM
MACRO(START_PROC, Name, StackBytes)
    PUBLIC &Name
    .PROC &Name
ENDM
#endif

MACRO(STUB_U, Name, ArgCount)
#ifdef _M_AMD64
    ALIGN 32
#endif
    MAKE_LABEL Zw&Name, %ArgCount * 4
    START_PROC Nt&Name, %ArgCount * 4
    STUBCODE_U Name, SyscallId, %ArgCount
    .ENDP
    SyscallId = SyscallId + 1
ENDM

MACRO(STUB_K, Name, ArgCount)
    START_PROC Zw&Name, %ArgCount * 4
    STUBCODE_K Name, SyscallId, %ArgCount
    .ENDP
    SyscallId = SyscallId + 1
ENDM

#endif

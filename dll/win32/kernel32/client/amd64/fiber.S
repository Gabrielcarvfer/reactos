/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS system libraries
 * FILE:        dll/win32/kernel32/client/amd64/fiber.S
 * PURPOSE:     Fiber context switch code for the amd64 architecture
 * PROGRAMMER:  Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <asm.inc>
#include <ksamd64.inc>

EXTERN BaseThreadStartup:PROC

.code64

/*
 * A new fiber starts here (BaseInitializeContext): RCX = start address,
 * RDX = parameter, RSP = the top of its stack. Like a thread, it never
 * returns: BaseThreadStartup exits the thread when the routine returns.
 */
PUBLIC BaseFiberStartup
FUNC BaseFiberStartup
    .ENDPROLOG
    /* (entered with a jump and RSP 16-byte aligned: home space, then the
       call leaves RSP as a function expects it) */
    sub rsp, 32
    call BaseThreadStartup
    int 3
ENDFUNC

/*
 * VOID BaseSwitchFiberContext(PCONTEXT OldContext, PCONTEXT NewContext)
 * Saves the non-volatile registers of the caller, including the x87 control
 * word and MXCSR (on x64 these are always switched, whatever
 * FIBER_FLAG_FLOAT_SWITCH says, as on Windows), into OldContext and continues where
 * NewContext was saved (or at a new fiber's start). SwitchToFiber has
 * switched the TEB already.
 */
PUBLIC BaseSwitchFiberContext
FUNC BaseSwitchFiberContext
    .ENDPROLOG

    /* Save the old fiber: it resumes as if this function returned */
    mov [rcx + CxRbx], rbx
    mov [rcx + CxRbp], rbp
    mov [rcx + CxRsi], rsi
    mov [rcx + CxRdi], rdi
    mov [rcx + CxR12], r12
    mov [rcx + CxR13], r13
    mov [rcx + CxR14], r14
    mov [rcx + CxR15], r15
    mov rax, [rsp]
    mov [rcx + CxRip], rax
    lea rax, [rsp + 8]
    mov [rcx + CxRsp], rax
    movups [rcx + CxXmm6], xmm6
    movups [rcx + CxXmm7], xmm7
    movups [rcx + CxXmm8], xmm8
    movups [rcx + CxXmm9], xmm9
    movups [rcx + CxXmm10], xmm10
    movups [rcx + CxXmm11], xmm11
    movups [rcx + CxXmm12], xmm12
    movups [rcx + CxXmm13], xmm13
    movups [rcx + CxXmm14], xmm14
    movups [rcx + CxXmm15], xmm15
    stmxcsr [rcx + CxMxCsr]
    fnstcw [rcx + CxFltSave]

    /* Load the new one */
    mov rbx, [rdx + CxRbx]
    mov rbp, [rdx + CxRbp]
    mov rsi, [rdx + CxRsi]
    mov rdi, [rdx + CxRdi]
    mov r12, [rdx + CxR12]
    mov r13, [rdx + CxR13]
    mov r14, [rdx + CxR14]
    mov r15, [rdx + CxR15]
    movups xmm6, [rdx + CxXmm6]
    movups xmm7, [rdx + CxXmm7]
    movups xmm8, [rdx + CxXmm8]
    movups xmm9, [rdx + CxXmm9]
    movups xmm10, [rdx + CxXmm10]
    movups xmm11, [rdx + CxXmm11]
    movups xmm12, [rdx + CxXmm12]
    movups xmm13, [rdx + CxXmm13]
    movups xmm14, [rdx + CxXmm14]
    movups xmm15, [rdx + CxXmm15]
    ldmxcsr [rdx + CxMxCsr]
    fldcw [rdx + CxFltSave]

    /* (RCX and RDX: a new fiber's start address and parameter) */
    mov rsp, [rdx + CxRsp]
    mov rax, [rdx + CxRip]
    mov rcx, [rdx + CxRcx]
    mov rdx, [rdx + CxRdx]
    jmp rax
ENDFUNC

END
/* EOF */

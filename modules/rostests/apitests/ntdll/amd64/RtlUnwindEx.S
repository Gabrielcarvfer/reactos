/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Helper function with an unwind handler for the RtlUnwindEx test
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <asm.inc>
#include <ksamd64.inc>

.code64

EXTERN CollidedTestHandler:PROC

//
// ULONG64
// CallWithUnwindHandler(
//    _In_ VOID (*Callback)(PVOID EstablisherFrame),
//    _Out_ PULONG64 Results);
//
// Calls Callback with its own establisher frame. Returns 0 if Callback
// returns, or 1 when an unwind lands on CollidedLandingPad, which stores
// RAX and RDX (the landing pad's two values, as in C++ landing pads) in
// Results[0] and Results[1].
//
PUBLIC CallWithUnwindHandler
PUBLIC CollidedLandingPad
#ifdef _USE_ML
CallWithUnwindHandler PROC FRAME:CollidedTestHandler
#else
.PROC CallWithUnwindHandler
.seh_handler CollidedTestHandler, @unwind, @except
.seh_handlerdata
.long 0
.seh_code
#endif

    push rbx
    .PUSHREG rbx
    sub rsp, 32
    .ALLOCSTACK 32
    .ENDPROLOG

    /* RBX is nonvolatile: the unwind restores it for the landing pad */
    mov rbx, rdx
    mov rax, rcx
    mov rcx, rsp
    call rax
    nop

    /* The callback returned */
    xor eax, eax
    jmp CallWithUnwindHandlerDone

CollidedLandingPad:
    mov [rbx], rax
    mov [rbx + 8], rdx
    mov eax, 1

CallWithUnwindHandlerDone:
    add rsp, 32
    pop rbx
    ret

#ifdef _USE_ML
CallWithUnwindHandler ENDP
#else
.ENDP
#endif

END

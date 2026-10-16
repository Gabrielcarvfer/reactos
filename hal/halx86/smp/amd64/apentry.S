/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup setup
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *
 * An application processor starts (STARTUP IPI) in real mode at the beginning
 * of the low stub page (below 1 MB), with CS = stub >> 4 and IP = 0.
 * HalpAPEntry16 .. HalpAPEntry16End is copied there and HalStartNextProcessor
 * fills the data part. The code goes real mode -> 32-bit protected mode ->
 * long mode with a temporary page table (identity mapping of the first 2 MB
 * plus the kernel half of the current address space), then jumps to
 * HalpAPEntry64 in the HAL image, which loads the processor state prepared by
 * the kernel and jumps into KiSystemStartup.
 */

#include <asm.inc>

PUBLIC HalpAPEntry16
PUBLIC HalpAPEntry16End
PUBLIC HalpAPEntry64
PUBLIC HalpApTempGdt
PUBLIC HalpApTempGdtr
PUBLIC HalpApEntry32
PUBLIC HalpApEntry64Low
PUBLIC HalpApJump32
PUBLIC HalpApJump64
PUBLIC HalpApPml4
PUBLIC HalpApKernelEntry
PUBLIC HalpApGdtr
PUBLIC HalpApIdtr
PUBLIC HalpApCr0
PUBLIC HalpApCr3
PUBLIC HalpApCr4
PUBLIC HalpApRsp
PUBLIC HalpApRip
PUBLIC HalpApRcx
PUBLIC HalpApStage

/* Offset of a label from the start of the stub */
#define SO(x) (x - HalpAPEntry16)

.code

.align 16
.code16
HalpAPEntry16:
    cli
    cld

    /* DS = CS: the data below is addressed relative to the stub */
    mov ax, cs
    mov ds, ax

    /* EBX = physical address of the stub */
    xor ebx, ebx
    mov bx, ax
    shl ebx, 4

    mov byte ptr ds:[SO(HalpApStage)], 1

    /* Load the temporary GDT (32-bit base) */
    .byte HEX(66)
    lgdt ds:[SO(HalpApTempGdtr)]

    /* Enable protected mode */
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    /* jmp far dword ptr ds:[HalpApJump32] -> HalpApEntry32 with CS = 0x08 */
    .byte HEX(66), HEX(FF), HEX(2E)
    .word SO(HalpApJump32)

.code32
HalpApEntry32:
    /* Flat data segments */
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov byte ptr [ebx + SO(HalpApStage)], 2

    /* Enable PAE (needed for long mode) */
    mov eax, cr4
    or eax, HEX(20)
    mov cr4, eax

    /* Load the temporary PML4 */
    mov eax, [ebx + SO(HalpApPml4)]
    mov cr3, eax

    /* EFER: SCE | LME | NXE (kernel page tables use the NX bit) */
    mov ecx, HEX(C0000080)
    rdmsr
    or eax, HEX(901)
    wrmsr

    /* Enable paging: this activates long mode (compatibility mode for now) */
    mov eax, cr0
    or eax, HEX(80000001)
    mov cr0, eax

    /* jmp far fword ptr [ebx + HalpApJump64] -> HalpApEntry64Low with CS = 0x18 */
    .byte HEX(FF), HEX(AB)
    .long SO(HalpApJump64)

.code64
HalpApEntry64Low:
    /* Upper halves of the registers are undefined after the mode switch */
    mov ebx, ebx

    mov byte ptr [rbx + SO(HalpApStage)], 3

    /* Continue in the HAL image (mapped in the temporary page table) */
    mov rax, [rbx + SO(HalpApKernelEntry)]
    jmp rax

/* DATA, filled in by HalStartNextProcessor ***********************************/

.align 16
HalpApTempGdt:
    .quad HEX(0000000000000000)  /* 0x00: null */
    .quad HEX(00CF9A000000FFFF)  /* 0x08: 32-bit code */
    .quad HEX(00CF92000000FFFF)  /* 0x10: 32-bit data */
    .quad HEX(00AF9A000000FFFF)  /* 0x18: 64-bit code */

.align 8
    .word 0, 0, 0
HalpApTempGdtr:
    .word 0              /* Limit */
    .long 0              /* Base (physical) */

.align 8
HalpApJump32:
    .long 0              /* Physical address of HalpApEntry32 */
    .word HEX(08)
.align 8
HalpApJump64:
    .long 0              /* Physical address of HalpApEntry64Low */
    .word HEX(18)

.align 8
HalpApPml4:
    .quad 0              /* Physical address of the temporary PML4 */
HalpApKernelEntry:
    .quad 0              /* HalpAPEntry64 (virtual) */
    .word 0, 0, 0
HalpApGdtr:
    .word 0              /* Limit */
    .quad 0              /* Base */
    .word 0, 0, 0
HalpApIdtr:
    .word 0              /* Limit */
    .quad 0              /* Base */
HalpApCr0:
    .quad 0
HalpApCr3:
    .quad 0
HalpApCr4:
    .quad 0
HalpApRsp:
    .quad 0
HalpApRip:
    .quad 0
HalpApRcx:
    .quad 0
HalpApStage:
    .quad 0

HalpAPEntry16End:

/* Long mode entry in the HAL image ******************************************/

/*
 * Entered with RBX = physical (identity mapped) address of the low stub,
 * on the temporary page table and GDT, with no stack.
 */
.align 16
HalpAPEntry64:
    /* Load the final GDT and IDT (virtual bases, mapped in both page tables) */
    lgdt [rbx + SO(HalpApGdtr)]
    lidt [rbx + SO(HalpApIdtr)]

    /* Fetch everything we need before the identity mapping disappears */
    mov rsp, [rbx + SO(HalpApRsp)]
    mov rcx, [rbx + SO(HalpApRcx)]
    mov rdx, [rbx + SO(HalpApRip)]
    mov r8, [rbx + SO(HalpApCr3)]
    mov r9, [rbx + SO(HalpApCr4)]
    mov r10, [rbx + SO(HalpApCr0)]

    mov byte ptr [rbx + SO(HalpApStage)], 4

    /* Switch to the kernel address space and the final control registers */
    mov cr3, r8
    mov cr4, r9
    mov cr0, r10

    /* Load the segment registers of the final GDT */
    mov ax, HEX(18)          /* KGDT64_R0_DATA */
    mov ss, ax
    mov ax, HEX(2B)          /* KGDT64_R3_DATA | RPL_MASK */
    mov ds, ax
    mov es, ax
    mov ax, HEX(53)          /* KGDT64_R3_CMTEB | RPL_MASK */
    mov fs, ax

    /* Load the task register (the TSS descriptor is marked available) */
    mov ax, HEX(40)          /* KGDT64_SYS_TSS */
    ltr ax

    /* Reload CS with KGDT64_R0_CODE */
    push HEX(10)
    lea rax, [rip + HalpApReloadCs]
    push rax
    .byte HEX(48), HEX(CB)   /* retfq */
HalpApReloadCs:

    /* Call KiSystemStartup(LoaderBlock) -- it never returns */
    xor rbp, rbp
    jmp rdx

END

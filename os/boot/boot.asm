; NovaOS boot entry: Multiboot 1 header + switch to the kernel stack.
; GRUB loads us in 32-bit protected mode and (if possible) sets a
; 1024x768x32 linear framebuffer for us.

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ (1 << 0) | (1 << 1) | (1 << 2)   ; align, meminfo, video mode
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    dd 0, 0, 0, 0, 0        ; address fields (unused, we are ELF)
    dd 0                    ; mode_type: 0 = linear framebuffer
    dd 1024                 ; width
    dd 768                  ; height
    dd 32                   ; depth

section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

section .text
global _start
extern kmain
_start:
    cli
    mov esp, stack_top
    push ebx                ; multiboot info pointer
    push eax                ; multiboot magic
    call kmain
.hang:
    cli
    hlt
    jmp .hang

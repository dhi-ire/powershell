; CPU exception / IRQ entry stubs, plus GDT and IDT loaders.

section .text

global gdt_flush
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload
.reload:
    ret

global idt_load
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; Exceptions 8, 10-14, 17, 21, 29, 30 push an error code themselves.
%assign i 0
%rep 32
  %if i = 8 || (i >= 10 && i <= 14) || i = 17 || i = 21 || i = 29 || i = 30
    ISR_ERR %[i]
  %else
    ISR_NOERR %[i]
  %endif
  %assign i i+1
%endrep

; Hardware IRQs 0-15 remapped to vectors 32-47.
%assign i 32
%rep 16
    ISR_NOERR %[i]
  %assign i i+1
%endrep

extern isr_handler
isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; struct regs *
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8              ; vector number + error code
    iret

section .data
global isr_table
isr_table:
%assign i 0
%rep 48
    dd isr%[i]
  %assign i i+1
%endrep

global isr_handler_stub
global schedule_next
global loadIDT

extern Exception_Handler
extern IRQ_common_Handler 
extern in_interrupt
extern scheduler_tick
extern send_eoi
extern current_task
extern scheduler_pick_next

extern task_t


loadIDT:
    mov eax, [esp+4]
    lidt [eax]
    ret

isr_handler_stub:
    cli

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax


    push edi
    push esi 
    push ebp 
    push esp
    push ebx 
    push edx
    push ecx
    push eax

    mov eax, esp
    push eax

    call Exception_Handler

    add esp, 4
    popa 

    pop gs
    pop fs
    pop es
    pop ds

    add esp, 8

    sti 
    iret




irq_timer_stub:
    cli
    inc dword [in_interrupt]

    pusha

    mov eax, [current_task]
    mov [eax], esp

    call scheduler_tick
    call IRQ_common_Handler

    mov eax, [current_task]
    mov esp, [eax]

    popa
    
    dec dword [in_interrupt]
    
    sti
    iret


irq_common_stub:
    cli
    inc dword [in_interrupt]

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    pusha 

    mov eax, esp
    push eax

    call IRQ_common_Handler
    
    add esp, 4
    
    popa
    pop gs
    pop fs
    pop es
    pop ds

    add esp, 8
    sti

    dec dword [in_interrupt]

    iret



global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

global irq69
global irq60

isr0:
    push 0
    push 0
    jmp isr_handler_stub

; 1: Debug Exception
isr1:
    push 0
    push 1
    jmp isr_handler_stub

; 2: Non Maskable Interrupt Exception
isr2:
    push 0
    push 2
    jmp isr_handler_stub

; 3: Int 3 Exception
isr3:
    push 0
    push 3
    jmp isr_handler_stub

; 4: INTO Exception
isr4:
    push 2
    push 4
    jmp isr_handler_stub

; 5: Out of Bounds Exception
isr5:
    push 0
    push 5
    jmp isr_handler_stub

; 6: Invalid Opcode Exception
isr6:
    push 0
    push 6
    jmp isr_handler_stub

; 7: Coprocessor Not Available Exception
isr7:
    push 0
    push 7
    jmp isr_handler_stub

; 8: Double Fault Exception (With Error Code!)
isr8:
    push 8
    jmp isr_handler_stub

; 9: Coprocessor Segment Overrun Exception
isr9:
    push 0
    push 9
    jmp isr_handler_stub

; 10: Bad TSS Exception (With Error Code!)
isr10:
    push 10
    jmp isr_handler_stub

; 11: Segment Not Present Exception (With Error Code!)
isr11:
    push 11
    jmp isr_handler_stub

; 12: Stack Fault Exception (With Error Code!)
isr12:
    push 12
    jmp isr_handler_stub

; 13: General Protection Fault Exception (With Error Code!)
isr13:
    push 13
    jmp isr_handler_stub

; 14: Page Fault Exception (With Error Code!)
isr14:
    push 14
    jmp isr_handler_stub

; 15: Unknown Interrupt
; Do nothing
isr15:
    iret

; 16: Floating Point Exception
isr16:
    push 0
    push 16
    jmp isr_handler_stub

; 17: Alignment Check Exception
isr17:
    push 0
    push 17
    jmp isr_handler_stub

; 18: Machine Check Exception
isr18:
    push 0
    push 18
    jmp isr_handler_stub

; 19: Reserved
isr19:
    push 0
    push 19
    jmp isr_handler_stub

; 20: Reserved
isr20:
    push 0
    push 20
    jmp isr_handler_stub

; 21: Reserved
isr21:
    push 0
    push 21
    jmp isr_handler_stub

; 22: Reserved
isr22:
    push 0
    push 22
    jmp isr_handler_stub

; 23: Reserved
isr23:
    push 0
    push 23
    jmp isr_handler_stub

; 24: Reserved
isr24:
    push 0
    push 24
    jmp isr_handler_stub

; 25: Reserved
isr25:
    push 0
    push 25
    jmp isr_handler_stub

; 26: Reserved
isr26:
    push 0
    push 26
    jmp isr_handler_stub

; 27: Reserved
isr27:
    push 0
    push 27
    jmp isr_handler_stub

; 28: Reserved
isr28:
    push 0
    push 28
    jmp isr_handler_stub

; 29: Reserved
isr29:
    push 0
    push 29
    jmp isr_handler_stub

; 30: Reserved
isr30:
    push 0
    push 30
    jmp isr_handler_stub

; 31: Reserved
isr31:
    push 0
    push 31
    jmp isr_handler_stub

; IRQ handlers
irq0:
        
        jmp irq_timer_stub

irq1:
        
        push 1
        push 33
        jmp irq_common_stub

irq2:
        push 2
        push 34
        jmp irq_common_stub

irq3:
        push 3
        push 35
        jmp irq_common_stub

irq4:
        push 4
        push 36
        jmp irq_common_stub

irq5:
        push 5
        push 37
        jmp irq_common_stub

irq6:
        push 6
        push 38
        jmp irq_common_stub

irq7:
        push 7
        push 39
        jmp irq_common_stub

irq8:
        push 8
        push 40
        jmp irq_common_stub

irq9:
        push 9
        push 41
        jmp irq_common_stub

irq10:
        push 10
        push 42
        jmp irq_common_stub

irq11:
        push 11
        push 43
        jmp irq_common_stub

irq12:
        push 12
        push 44
        jmp irq_common_stub

irq13:
        push 13
        push 45
        jmp irq_common_stub

irq14:
        push 14
        push 46
        jmp irq_common_stub

irq15:
        push 15
        push 47
        jmp irq_common_stub



irq60:
        cli
        push 60
        push 60
        jmp irq_common_stub

irq69:
        cli
        push 69
        push 69
        jmp irq_common_stub
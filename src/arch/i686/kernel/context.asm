section .text
global context_switch
global user_task_enter
global user_task_return
global return_to_idle

extern current_task
extern idle_task
extern scheduler_choose_next
extern in_interrupt


; user_task_enter: called the first time a user task is scheduled.
; Stack at this point has the iret frame from create_ptask
user_task_enter:
    ; load user data selector into all data segment registers
    mov ax, 0x23          ; USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iret                  ; pops EIP, CS, EFLAGS, ESP, SS → ring 3

user_task_return:
    mov eax, 60           ; SYS_EXIT
    int 0x80
.return_halt:
    hlt
    jmp .return_halt

; void context_switch(uint32_t **old_sp_ptr, uint32_t *new_sp)
context_switch:

    mov eax, [esp + 4]       ; eax = old_sp_ptr
    mov edx, [esp + 8]       ; edx = new_sp

    pusha                    
    mov [eax], esp
    mov esp, edx
    popa

    sti
    ret

return_to_idle:
    mov eax, [idle_task]
    mov esp, [eax] ; stack offset is zero

    popa
    
    sti
    iret

section .text
global context_switch
global return_to_idle

extern current_task
extern idle_task
extern scheduler_choose_next
extern in_interrupt


; void context_switch(uint32_t **old_sp_ptr, uint32_t *new_sp)
context_switch:

    mov eax, [esp + 4]       ; eax = old_sp_ptr
    mov edx, [esp + 8]       ; edx = new_sp

    pusha                    
    mov [eax], esp
    mov esp, edx
    popa

    ret

return_to_idle:
    mov eax, [idle_task]
    mov esp, [eax] ; stack offset is zero

    popa
    
    sti
    iret

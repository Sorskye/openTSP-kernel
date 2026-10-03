section .trampoline
align 4096
global vmm_switch_and_jump

;vmm_switch_and_jump(new_cr3, higher_half_target, new_esp)

vmm_switch_and_jump:
    mov eax, [esp+4] ; new cr3
    mov ecx, [esp+8] ; higher half targed
    mov edx, [esp+12]; new esp

    mov cr3, eax ; still at low addr
    
    mov esp, edx
    
    jmp ecx


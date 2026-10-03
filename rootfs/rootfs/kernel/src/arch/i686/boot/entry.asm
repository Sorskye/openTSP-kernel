section .multiboot
align 4

; --- Constants ---
MULTIBOOT_MAGIC     equ 0x1BADB002
MULTIBOOT_FLAGS     equ (1 << 0) | (1 << 1) | (1 << 2)   ; add framebuffer flag
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Framebuffer settings
FB_WIDTH  equ 1920
FB_HEIGHT equ 1080
FB_DEPTH  equ 32


; header
dd MULTIBOOT_MAGIC
dd MULTIBOOT_FLAGS
dd MULTIBOOT_CHECKSUM



; framebuffer request
dd FB_WIDTH
dd FB_HEIGHT
dd FB_DEPTH

section .text
align 4
bits 32

extern kernel_main
extern GDT_install
extern IDT_install
extern no_sse

global _start
global gdt_flush
global page_directory
global page_tables
global stack_bottom
global stack_top

global page_directory_start
global page_directory_end
global page_tables_start
global page_tables_end

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

    jmp 0x08:flush_cs_reload

flush_cs_reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

_start:
    cli

    ; Set temporary stack
    mov esp, stack_top

    ; Save multiboot registers before clobbering
    mov [mb_magic], eax
    mov [mb_info], ebx

    

    ; Optional: check SSE support
    mov eax, 1
    cpuid
    test edx, (1 << 25)
    jz no_sse

    ; Properly enable SSE
    mov eax, cr0
    and eax, ~(1 << 2)      ; clear EM
    or  eax,  (1 << 1)      ; set MP
    mov cr0, eax

    mov eax, cr4
    or  eax, (1 << 9)       ; OSFXSR
    or  eax, (1 << 10)      ; OSXMMEXCPT
    mov cr4, eax

    ; set GDT and IDT
    call GDT_install
    call IDT_install

     

    ; Zero page directory
    mov edi, page_directory
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; Zero early page tables (16 tables = 64 MiB)
    mov edi, page_tables
    xor eax, eax
    mov ecx, (16 * 4096) / 4
    rep stosd

    ; Build identity map for first 64 MiB
    ;
    ; page_directory[i] -> early_page_tables + i*4096
    ; each PT maps 4 MiB
    ;
    xor ebx, ebx                ; physical address being mapped
    xor esi, esi                ; page directory index: 0..15
    

.setup_pd_loop:
    cmp esi, 16   ; 512 MiB;cmp esi, 16
    jge .paging_ready

    ; PT address = early_page_tables + esi*4096
    mov eax, page_tables
    mov edx, esi
    shl edx, 12
    add eax, edx

    ; PDE = PT address | present | writable
    mov edx, eax
    and edx, 0xFFFFF000
    or edx, 0x3
    mov [page_directory + esi*4], edx

    ; Fill page table with 1024 identity-mapped pages
    mov edi, eax
    mov ecx, 1024

.fill_pt_loop:
    mov edx, ebx
    or edx, 0x3
    mov [edi], edx

    add ebx, 0x1000
    add edi, 4
    loop .fill_pt_loop

    inc esi
    jmp .setup_pd_loop

.paging_ready:
    ; Load page directory into CR3
  
    mov eax, page_directory
    test eax, 0xFFF
    jnz .bad_alignment
    mov cr3, eax
     

    ; Enable paging (protected mode is already active under GRUB)
    mov eax, cr0
    or eax, 0x80000000         ; set PG
    mov cr0, eax

  
    ; Small serializing jump after enabling paging
    jmp .after_paging

.after_paging:
   
    ; Restore multiboot args and call C
    push dword [mb_info]
    push dword [mb_magic]
    
    call kernel_main
    add esp, 8
    
.bad_alignment:
    jmp $

.hang:
    cli
    hlt
    jmp .hang

section .bss



section .paging align=4096


page_directory_start:
page_directory:
    resb 4096
page_directory_end:


align 4096

page_tables_start:
page_tables:
    resb 16 * 4096   ; 512 MiB mapping
page_tables_end:

section .bss align=16

align 16
mb_magic:
    resd 1

mb_info:
    resd 1


align 16
stack_bottom:
    resb 16384

stack_top:
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
KERNEL_PDE_INDEX equ (0xC0000000 >> 22)


; header
dd MULTIBOOT_MAGIC
dd MULTIBOOT_FLAGS
dd MULTIBOOT_CHECKSUM

; framebuffer request
dd FB_WIDTH
dd FB_HEIGHT
dd FB_DEPTH

section .boot.text
align 4
bits 32

extern kernel_bootstrap
extern GDT_install
extern tss_init
extern IDT_install
extern no_sse

global _start
global gdt_flush
global tss_flush

global bootstrap_page_directory
global bootstrap_page_tables
global stack_bottom
global stack_top

global bootstrap_page_directory_start
global bootstrap_page_directory_end
global bootstrap_page_tables_start
global bootstrap_page_tables_end


extern BOOTSTRAP_PAGE_TABLE_COUNT

tss_flush:
    mov ax, 0x28
    ltr ax
    ret

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

     mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

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

    mov dx, 0x3F8
    mov al, '*'
    out dx, al

    

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

    ; Zero page directory
    mov edi, bootstrap_page_directory
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; Zero early page tables (16 tables = 64 MiB)
    mov edi, bootstrap_page_tables
    xor eax, eax
    mov ecx, (BOOTSTRAP_PAGE_TABLE_COUNT * 1024) 
    rep stosd

    ; Build identity map for first 64 MiB
    ;
    ; page_directory[i] -> early_page_tables + i*4096
    ; each PT maps 4 MiB
    ;
    xor ebx, ebx                ; physical address being mapped
    xor esi, esi                ; page directory index: 0..15
    

.setup_pd_loop:
    cmp esi, BOOTSTRAP_PAGE_TABLE_COUNT   ; tables to identity map
    jge .paging_ready

    ; compute address of this PT: page_tables + esi*4096
    mov eax, bootstrap_page_tables
    mov edx, esi
    shl edx, 12 ; esi * 4096
    add eax, edx

    ; make the PDE point to that PT, present+writable
    mov edx, eax
    and edx, 0xFFFFF000 ; ensure lower 12 bits are zero
    or edx, 0x3 ; present + writable
    mov [bootstrap_page_directory + esi*4], edx

    ; Fill page table with 1024 identity-mapped pages
    mov edi, eax    ;pointer to PT
    mov ecx, 1024

.fill_pt_loop:
    mov edx, ebx
    or edx, 0x3 ; physical address + present + writable
    mov [edi], edx

    add ebx, 0x1000 ;next physical page
    add edi, 4  ;next entry in page table
    loop .fill_pt_loop

    inc esi
    jmp .setup_pd_loop

.paging_ready:
    xor esi, esi

.setup_high_pd_loop:
    cmp esi, BOOTSTRAP_PAGE_TABLE_COUNT
    jge .high_paging_ready

    mov eax, bootstrap_page_tables
    mov edx, esi
    shl edx, 12
    add eax, edx
    or eax, 0x3

    mov edx, KERNEL_PDE_INDEX
    add edx, esi
    mov [bootstrap_page_directory + edx*4], eax

    inc esi
    jmp .setup_high_pd_loop

.high_paging_ready:

    ; Load page directory into CR3
  
    mov eax, bootstrap_page_directory
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
    mov dx, 0x3F8
    mov al, '('
    out dx, al

    call GDT_install
    call tss_init
    call IDT_install

    mov dx, 0x3F8
    mov al, ')'
    out dx, al

    ; Restore multiboot args and call C
    push dword [mb_info]
    push dword [mb_magic]
    
    call kernel_bootstrap
    add esp, 8
    
.bad_alignment:
    jmp $

.hang:
    cli
    hlt
    jmp .hang




section .boot.paging align=4096

BOOTSTRAP_PAGE_TABLE_COUNT equ 16

bootstrap_page_directory_start:
bootstrap_page_directory:
    resb 4096
bootstrap_page_directory_end:

align 4096

bootstrap_page_tables_start:
bootstrap_page_tables:
    resb 4096 * BOOTSTRAP_PAGE_TABLE_COUNT   ; reserve space for bootstrap page tables 
bootstrap_page_tables_end:

section .boot.bss align=16

align 16
mb_magic:
    resd 1

mb_info:
    resd 1

align 16
stack_bottom:
    resb 16384

stack_top:
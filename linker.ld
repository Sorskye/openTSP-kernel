ENTRY(_start)
KERNEL_OFFSET = 0xC0000000;
KERNEL_PHYS   = 1M;

SECTIONS
{
  . = KERNEL_PHYS;

  __kernel_start = .;
  

  /* Multiboot header MUST be early */
  .multiboot ALIGN(4K) :
  {
    KEEP(*(.multiboot))
  }

  __kernel_phys_start = LOADADDR(.multiboot);

  .boot :
  {
    KEEP(*(.boot*))
  }

  .boot_data :
  {
    KEEP(*(.boot_data*))
  }



  /* Higher-half virtual addresses with contiguous physical load addresses. */

  . = KERNEL_OFFSET + ALIGN(LOADADDR(.boot_data) + SIZEOF(.boot_data), 4K);

  .text ALIGN(4K) : AT(ALIGN(LOADADDR(.boot_data) + SIZEOF(.boot_data), 4K))
  {
    *(.text*)
    *(.trampoline*)
  }

  .rodata ALIGN(4K) : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 4K))
  {
    *(.rodata*)
  }

  .data ALIGN(4K) : AT(ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 4K))
  {
    *(.data*)
  }

  /* 🔴 Paging structures: force 4K alignment */
  
  .paging ALIGN(4K) : AT(ALIGN(LOADADDR(.data) + SIZEOF(.data), 4K))
  {
    __paging_start = .;
    *(.paging)
    __paging_end = .;
  }

  /* BSS last */
  .bss ALIGN(4K) : AT(ALIGN(LOADADDR(.paging) + SIZEOF(.paging), 4K))
  {
    *(COMMON)
    *(.bss*)
  }

  __kernel_end = .;
  __kernel_phys_end = LOADADDR(.bss) + SIZEOF(.bss);
}
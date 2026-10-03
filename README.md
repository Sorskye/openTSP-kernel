# openTSP

openTSP is a hobby operating-system project for learning and experimentation. It is an early, incomplete 32-bit x86 kernel, not a dependable general-purpose OS. The code contains known bugs, unsafe shortcuts, missing cleanup, unfinished drivers, stale artifacts, and security failures. Expect crashes, corrupted state, boot failures, and behavior that changes with the build or emulator. Do not use it for important data, security-sensitive work, or as a model for production kernel design.

This README describes the behavior wired into the top-level build and current kernel sources. It also calls out separate userland and prototype trees so that files present in the repository are not mistaken for features the booted system provides.

## At A Glance

- Target: 32-bit i686, Multiboot 1, freestanding C (GNU99) and NASM assembly.
- Bootloader: GRUB, normally via a generated bootable ISO.
- Kernel image: `kernel.elf`, linked as a higher-half kernel at `0xC0000000`, with physical load addresses beginning at 1 MiB.
- Early paging: 4 KiB pages; bootstrap tables identity-map and higher-half-map the first 64 MiB.
- Main user interface: one framebuffer-backed graphical screen with a text terminal drawn by a kernel compositor. This is not yet a functioning desktop/window manager.
- Filesystem at boot: an in-memory RAM filesystem reconstructed from a GRUB module named `tmpfs`; it is not a disk filesystem and does not persist changes across reboot.
- Initial user program: `/bin/bash.elf`, loaded as a static ELF32 executable if that path exists in the supplied filesystem image.
- Current kernel version constants: `1.4`, codename `HEXA`; the ISO target is separately named `openTSP-0.1.4.iso`. Version strings in older user programs and filesystem artifacts may not match.

## Build And Run

The root `Makefile` expects an `i686-elf` cross toolchain and the GRUB/NASM ISO tools. At minimum, provide `i686-elf-gcc`, `i686-elf-ld`, `i686-elf-nm`, `i686-elf-objcopy`, `nasm`, and `grub-mkrescue` (usually backed by `xorriso`).

```sh
make kernel.elf
make
qemu-system-i386 -cdrom openTSP-0.1.4.iso -m 64M -serial stdio
```

`make` builds the kernel, generates a symbol table in a two-pass link, copies `kernel.elf` and the existing project-root `tmpfs.img` into `iso/`, writes a GRUB configuration there, and invokes `grub-mkrescue`. The framebuffer request/configuration is fixed around 1920x1080x32; another machine or emulator may not provide that mode. Serial output is useful for boot logs and diagnostics.

Important build caveat: the root Makefile does **not** build user programs or create/update `tmpfs.img`. The image must already exist. The bundled user-program Makefile is separate; for example:

```sh
make -C rootfs/rootfs/usr/programs PROGRAM=bash.c OUTPUT=bash.elf
```

That sub-build may configure/build the bundled Newlib source on the host. It produces a user ELF in the programs directory, but does not insert it into `tmpfs.img`. A host utility named `rootfs/ramfs_build` is present, but the top-level Makefile neither invokes it nor documents a reproducible image-packaging command. Rebuilding `kernel.elf` alone therefore does not guarantee that the booted shell or filesystem matches the latest source.

`make clean` removes the generated build directory, kernel outputs, ISO staging directory, and generated ISO. It does not rebuild or refresh the RAMFS image. The root Makefile has no automated test target; example/test programs under `rootfs/rootfs/usr/programs/` are not a kernel test suite.

## Operator Manual

### Keyboard And Reboot

- **Ctrl+Alt+R** requests an immediate PC reset by writing `0xFE` to the keyboard controller command port. The source comment says Ctrl+Alt+Delete, but the implemented key check is **R**, not Delete. This is a hardware reset request, not a clean shutdown; buffered data is lost.
- On the fatal-error screen, press **R** by itself to request the same reset.
- On the fatal-error screen, press **Tab** to turn off the PC speaker.
- **F1** attempts prints "hello!" to the serial console
- **Right Alt+1** raises the PIT's recorded frequency by 5 Hz. **Right Alt+2** lowers it by 5 Hz. This is a debug control, not a calibrated clock setting; the initial rate is 105 Hz. If it is lowered when at 0 the kernel panics. **Right Alt+0** stops the hardware timer. **Right Alt+r** resets it to the default (105hz).
- The PS/2 keyboard path recognizes a small Set-1-style key map. Character input is line-buffered by the TTY, echoed, and sent to the active terminal. There is no terminal-switching shortcut wired up.

### The Shell

The intended initial user program is a small bash-like shell in `rootfs/rootfs/usr/programs/bash.c`. It prints a prompt containing the current working directory, followed by `$ `. `help` lists the actual built-ins:

`:` `true` `false` `cd` `pwd` `echo` `exit` `export` `unset` `set` `read` `source` `.` `type` `help` `test` `[`

Implemented shell syntax includes simple `NAME=value` assignments, `$NAME` and `${NAME}` expansion, `$?`, `$$`, `~` (from `HOME`, default `/`), single/double quotes, backslash escapes, comments beginning with `#`, and command separators/conditionals `;`, `&&`, and `||`. `test`/`[` implements a small set of string, numeric, and existence comparisons. `source file` and `. file` read a script into the current shell; starting the shell with a script argument is intended too, but the current process startup does not reliably construct `argc`/`argv`.

The parser recognizes `<`, `>`, and `>>` redirections for built-ins. Redirection is not a dependable file-editing facility: filesystem creation, truncation, append, relative-path, and access-mode handling are incomplete. Shell variables are internal to the shell; `export` marks them for an environment array, but that array is currently only scaffolding for external execution.

A built-in-only session can look like this:

```sh
pwd
NAME=world
echo "hello, $NAME"
false || echo "the right side runs after failure"
test "$NAME" = world && echo "comparison succeeded"
cd /sys
pwd
```

Use absolute paths when opening, sourcing, or redirecting files; VFS relative-path lookup does not consistently follow the directory shown by the prompt. Commands in this example exercise shell parsing and built-ins only; they do not demonstrate external program execution.

Not implemented: running external programs, pipes, background jobs, `if`/`while`/`for`, globbing, command substitution, and a complete POSIX shell grammar. `PATH` is initialized to `/bin`, but is not used because `external_command()` always reports that launching programs is unimplemented. Thus the presence of other ELF files in the image does not mean they can be launched from the prompt.

### Fatal Errors

Kernel bugchecks print a fatal message and, when available, the current process/task, CPU registers, symbolized fault address, stack trace, and page-fault information. A panic may sound the PC speaker. The fatal path then polls the keyboard rather than returning to normal operation; use **R** to reset or **Tab** to silence the speaker. A user-mode divide-by-zero, invalid opcode, general-protection fault, or selected page faults are intended to terminate that process and schedule another task, but process cleanup is incomplete and that recovery is not reliable.

## How The System Boots

The main source of truth is `src/` plus the root `Makefile` and `linker.ld`.

1. GRUB loads the Multiboot kernel and, for the normal ISO target, passes `tmpfs.img` as a module with the module name `tmpfs`.
2. `src/arch/i686/boot/entry.asm` enters with interrupts disabled, installs a temporary stack, saves the Multiboot registers, checks for SSE, enables SSE state, clears the bootstrap page structures, and creates 16 page tables. These tables cover 64 MiB both at physical addresses and at the `0xC0000000` higher-half offset.
3. Assembly enables paging, installs the GDT, TSS, and IDT, then calls `kernel_bootstrap()`. The kernel checks the Multiboot magic and initializes the PIT and physical-memory bitmap.
4. `vmm_init()` creates a supervisor page directory, maps the kernel at its higher-half address, maps selected boot data/modules/framebuffer, establishes a physical-access window, and jumps to `kernel_main()` with the higher-half stack.
5. `kernel_main()` creates an empty RAMFS root, replaces it from the first GRUB module only if its name is exactly `tmpfs`, initializes the VFS and debug font, registers syscall handlers, creates the kernel process/idle task, enumerates PCI devices, initializes TTY and serial/PS/2 devices, then creates the compositor task and attempts to load `/bin/bash.elf`.
6. The timer-driven scheduler starts. The user ELF is loaded into a per-process address space and starts through the ring-3 task trampoline. If the module, terminal setup, ELF, or file paths do not match the assumptions above, initialization can fail or panic.

The top-level `grub.cfg` only contains a graphics-mode setting. The ISO recipe writes a different GRUB configuration directly into `iso/boot/grub/grub.cfg`; the `grubtheme` variable/assets are not wired into that recipe.

### CPU, Interrupts, And Tasks

The GDT defines null, kernel code/data, user code/data, and a TSS entry. The TSS supplies a kernel stack when privilege transitions occur. The IDT handles x86 exceptions, remapped legacy PIC IRQs, software scheduling via vector `0x20`, user syscalls via `int 0x80`, and a thread-exit vector `0x81`.

The scheduler is a single-CPU, circular task list preempted by the PIT. Tasks have a 4 KiB kernel stack; a process can hold up to 16 task pointers, and the global task limit is 512. Timer ticks wake sleeping tasks and select a ready task. The initial PIT request is only 2 Hz. `sleep_ms()` rounds delays to timer ticks, so short sleeps can take roughly half a second at that setting. The idle task currently loops and continuously logs `idle`, rather than halting the CPU.

This is not a complete process model: there is no working fork/exec/wait lifecycle, process table, robust task reaper, or complete synchronization model. Several scheduler structures and removal paths are inconsistent; see [Known Issues](#known-issues).

### Physical And Virtual Memory

Physical memory is tracked by a bitmap using 4 KiB pages and Multiboot's memory map. Kernel virtual memory begins at `0xC0000000`; the early boot tables cover only the first 64 MiB. A direct physical-access mapping begins at `0xE0000000` and is also currently sized for only 64 MiB even though the physical allocator's bitmap describes a much larger 32-bit address range.

The kernel heap is a bump allocator. User processes get a new page directory, demand-mapped ELF pages, a fixed eight-page user stack near `0x50000000`, and a `brk`-style heap range. A missing page fault is usually handled by allocating and zeroing a page. There is no reliable free/reclaim path for kernel allocations, and many process/address-space failure paths leak their partial allocations.

### File System And VFS

The kernel uses VFS inode/file-operation tables and a RAM filesystem (`src/kernel/fs/ramfs.c`). The boot image format starts with a `RAFS` header (magic `0x52414653`, inode count, root inode), followed by fixed-layout inode records and a file-data section. Directories reference consecutive child inode indices; files refer to byte ranges in the image. The loader creates in-memory inode/directory/file objects and points file data into the loaded module. It is not a block-device filesystem: there is no disk driver, journaling, durable writeback, permissions, or persistence across reboot.

The file-descriptor limit is 16 per process. The first user process is given descriptors 0, 1, and 2 backed by `/dev/tty0`. RAMFS supports reads, bounded writes, basic lookup, directory enumeration, and directory creation, but the overall VFS/syscall semantics are unfinished. In particular, `O_CREAT` in `vfs_open()` allocates an inode without inserting it into its parent directory; `O_TRUNC` and `O_APPEND` are not consistently implemented; relative `vfs_open()` paths start at the root rather than the process current directory; and closing a file does not free its file object.

`src/kernel/fs/ufs.c` is primarily file-descriptor and syscall glue despite its name; it is not an on-disk UFS implementation.

### Syscalls And User Programs

The intended i386 syscall convention is the number in `EAX`, arguments in `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, and `EBP`, with the result in `EAX`. User wrappers in `rootfs/rootfs/usr/libc/syscall.c` enter through `int 0x80`.

| Number | Operation | Current status |
| --- | --- | --- |
| 0 | `read(fd, buffer, size)` | Implemented, with TTY and pointer-safety caveats |
| 1 | `write(fd, buffer, size)` | Implemented, with pointer-safety caveats |
| 2 | `open(path, flags)` | Implemented incompletely |
| 3 | `close(fd)` | Implemented incompletely; file allocations leak |
| 4 | `getpid()` | Implemented |
| 8 | `lseek(fd, offset, whence)` | Implemented for regular files |
| 12 | `brk(address)` | Basic heap-break update; pages are demand allocated |
| 24 | `yield()` | Requests a schedule via software interrupt |
| 60 | `exit(status)` | Marks tasks exited; status/reaping are incomplete |
| 79 | `getcwd(buffer, size)` | Implemented with path/pointer caveats |
| 80 | `chdir(path)` | Implemented with path/pointer caveats |

The kernel ELF loader accepts little-endian ELF32 `ET_EXEC` files for `EM_386`, and loads `PT_LOAD` segments. It is not a dynamic linker and does not implement relocations, shared libraries, ELF permissions/execute protection, or a complete validation pass. The user CRT calls `main()` and then `exit()`, but the kernel does not build a proper initial argument/environment stack. libc stubs for `fork`, `execve`, `wait`, `kill`, and other POSIX operations are placeholders or return errors; they are not kernel services.

### Input, TTY, And Graphics

The PS/2 keyboard IRQ queues scan codes; a kernel task decodes keys, tracks modifier state, handles global shortcuts, and dispatches character events to the active input sink. A TTY worker performs simple line editing (including backspace), echo, and line delivery. The active process's standard streams refer to `/dev/tty0`.

The PS/2 mouse driver parses movement, buttons, and optional wheel packets, but the active TTY input sink has no mouse callback, so mouse events do not control the visible interface. The compositor requests a GRUB framebuffer, loads a PSF1 font and BMP wallpaper/theme assets from the RAMFS, and draws terminal characters using software pixel operations. Window structures and theme assets exist, but the running compositor does not implement a usable window manager, cursor, scrolling terminal, or application event routing. The screen's assumed pixel channel layout is hard-coded and may not match the framebuffer format reported by GRUB.

Other hardware code includes legacy PIC/PIT access, COM1 serial logging, VGA text-mode helpers, PC-speaker tones, and PCI configuration-space enumeration/BAR probing. PCI enumeration reports devices; it does not provide general storage, network, USB, or graphics drivers.

## Known Issues

These are source-level findings, not a claim that every failure has been reproduced on every machine.

### Critical: Ring 3 Is Not A Security Boundary

`allocUserVmEnv()` copies supervisor page-directory entries into user page directories and sets the user bit on those PDEs and their page-table entries. The copied mappings include the higher-half kernel. Consequently, ring-3 code can access kernel mappings; the page-fault check that intends to reject such addresses cannot protect pages that are already mapped as user-accessible. In addition, `USER_INITIAL_EFLAGS` sets IOPL to 3, allowing user code privileged port I/O. Never run untrusted programs under this kernel.

### Critical: Syscalls Trust User Pointers

Syscall implementations use user-provided path strings and buffers directly (`strlen`, `memcpy`, filesystem reads/writes, and `getcwd`) without validating or safely copying ranges. A bad pointer can fault while executing kernel code. The syscall-context page-fault path bugchecks instead of safely returning an error, so a malformed request can crash the whole machine. The ELF loader also does not comprehensively validate program-header bounds, segment arithmetic, or file ranges before using them.

### Critical: Syscall Interrupts Send A PIC EOI

The `int 0x80` stub pushes a dummy error code of zero. `syscall_handler()` passes that field to `send_eoi()`, which acknowledges PIC IRQ 0 even though a syscall is not a hardware IRQ. This can disturb timer/PIC state and makes interrupt behavior less reliable.

### Critical: TTY Reads Write Past Small Buffers

`ttyfile_read()` calls `tty_read_line()`, which always writes a trailing NUL at `buffer[count]`, including when `count == maxlen`. The bundled shell reads stdin one byte at a time with a one-byte local buffer, so this writes one byte beyond that buffer. TTY reads are also line-oriented rather than normal byte-stream reads. This can corrupt the shell's stack and is a likely source of unstable input behavior.

### Critical: Task Exit And Scheduling Are Unsafe

The allocator adds dynamically allocated tasks to a circular list, while `remove_task_from_list()` indexes a separate static `task_table` that is not populated by that allocator and computes a predecessor from `tid - 1`. Task counts are not consistently decremented, exited tasks are not reclaimed, and process exit does not release address spaces or descriptors. Scheduler traversal/removal can therefore corrupt task state, leak resources, or fail after task churn. The idle task's serial-print loop can also flood logs and consume the CPU.

### High: Memory-Management Limits Do Not Match

The early and direct physical mappings cover 64 MiB, while the allocator accepts physical pages across the 32-bit range. An allocation outside the mapped physical window cannot be safely dereferenced through `physical_access()`. The Multiboot memory-map parser uses a fixed 64-entry array without a visible bounds check, and reserved ranges such as the framebuffer are not consistently excluded from allocation. These assumptions may appear to work in a small QEMU configuration and fail with different memory layouts.

### High: The No-SSE Boot Path Is Not A Fallback

The entry code checks for SSE, but on a CPU without SSE it jumps to `no_sse()`, a C function that simply returns. That branch is not a controlled halt or alternate initialization path, and it returns using a stack that has no valid C call return address. The kernel therefore effectively requires SSE despite targeting i686 broadly.

### High: User Process Startup ABI Is Incomplete

The ELF loader starts the image entry point but the kernel does not construct the normal `argc`, `argv`, and `envp` stack. The CRT calls `main()` without arguments, while the shell inspects `argc` and may use `argv[1]`. Argument-dependent startup is undefined and can misbehave even when the ELF loaded successfully.

### High: ELF Loading Is Not Robust Against Malformed Images

Only basic ELF identity/class/data/type/machine checks are performed. Header-table bounds, segment file ranges, `p_filesz <= p_memsz`, address overflow, overlapping segment permissions, and entry-point validity are not all checked. The loader is suitable only for trusted, locally built test images.

### High: Allocation And Cleanup Are Incomplete

Kernel `kfree()` and the kernel `free()` implementation are empty; heap allocations are not reused. File close does not release the file object, and several failure paths leak inodes, descriptors, page tables, or loaded pages. Long-running workloads will exhaust memory.

### High: Filesystem Semantics Are Incomplete

RAMFS image parsing trusts inode counts, child ranges, offsets, and sizes without validating them against the supplied module length. Directory entry limits and string copying are fragile. VFS creation, access modes, truncation, append, relative-path resolution, reference counts, and close semantics are incomplete. The filesystem is volatile and should be treated as disposable.

### High: Kernel And Userland Builds Can Drift

The top-level build consumes all C/assembly under `src/`, but does not compile the shell or regenerate `tmpfs.img`. The shell build is separate, the filesystem image is an existing artifact, and the checked-in `rootfs/rootfs/kernel/src/` tree differs substantially from the active `src/` tree. `tmp/compositor.c` is another prototype outside the top-level source search. Do not assume those trees/artifacts describe the kernel in the ISO without checking which files were rebuilt and packed.

### Medium: The Shell Cannot Launch Commands

The shell's external-command handler is deliberately a stub; `fork`, `execve`, and `wait` are also not implemented. The advertised `PATH` variable and prepared environment-array helpers do not make external execution work. Redirection syntax is parsed but depends on unfinished filesystem operations.

### Medium: Graphics And Input Are Prototype-Level

Framebuffer color fields are hard-coded, the requested video mode is fixed, pixel operations are software-heavy, terminal scrolling is commented out, and mouse input is not connected to a cursor/window system. Missing or incompatible framebuffer/theme files can leave the display partially initialized.

### Medium: Error Handling And API Contracts Vary

Many operations return generic `-1` rather than consistent `errno` values; some wrappers translate all failures to `EIO`. Several APIs have mismatched stream/line semantics, unchecked allocation assumptions, or incomplete synchronization. The serial console and kernel debug screen are the most useful diagnostic surfaces, but neither guarantees recovery.

## Repository Map

- `src/arch/i686/boot/`: Multiboot entry, interrupt stubs, and higher-half transition.
- `src/arch/i686/kernel/`: GDT, IDT, TSS setup, and low-level CPU support.
- `src/kernel/core/`: kernel bootstrap, scheduler, tasks, and process creation.
- `src/kernel/mem/`: physical-page bitmap, virtual memory, heap, and page-fault support.
- `src/kernel/fs/`: RAMFS, VFS, and file-descriptor/syscall plumbing.
- `src/kernel/drivers/`: TTY, framebuffer/video, PS/2, PIT, PCI, serial, VGA text, and speaker code.
- `src/kernel/modules/`: active compositor module and its headers.
- `src/lib/`: freestanding string/stdio helpers, syscall glue, ELF/BMP/PSF support, ring buffers, locks, and debug symbols.
- `src/include/`: kernel and library interfaces.
- `rootfs/rootfs/usr/`: separate libc startup/syscall wrappers and shell/example program sources.
- `rootfs/rootfs/sys/`: fonts and graphics assets intended for the RAMFS image.
- `rootfs/rootfs/kernel/src/`: a divergent older/alternate kernel snapshot; not compiled by the root Makefile.
- `rootfs/tmpfs.img`, project-root `tmpfs.img`, `build/`, `kernel.elf`, and ISO files: generated or prebuilt artifacts whose freshness depends on how they were produced.
- `tmp/compositor.c`: scratch compositor prototype outside the active `src/` build.

## Project Status

This is an educational work in progress. The implementation is useful for exploring boot code, x86 descriptors/interrupts, basic paging, a timer-preempted task model, a RAMFS/VFS boundary, syscall entry, a minimal ELF loader, PS/2 input, and framebuffer drawing. Those pieces are not yet integrated with the validation, isolation, resource lifetime, compatibility, or test coverage required of a real operating system. Treat every interface as experimental and verify the active source and image contents before relying on any behavior.

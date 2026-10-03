# !! NOTICE
## The kernel itself is being completly rewritten. This is the outdated version. The new version will be published to this repo in the end of 2026. This version contains silly errors and should not be used as an example


# openTSP Kernel

## **Overview**

openTSP is a hobby operating system kernel developed as a learning project to explore low-level system design and operating system internals.

The project focuses on implementing fundamental OS components from scratch in order to better understand how modern systems work. While still experimental, the kernel has grown beyond a minimal prototype and now includes several core subsystems such as preemptive multitasking, terminal interfaces, a basic window compositor, and virtual memory management.

The system is intentionally kept simple and readable so that new features and ideas can be explored without excessive complexity.

---

## **Features**

Current functionality implemented in the kernel:

- **Preemptive Multitasking**  
  A timer-driven scheduler allowing multiple tasks to run concurrently.
  Round robin style.
  Most processes run in ring 3

- **TTY Subsystem**  
  Basic terminal interfaces used for system interaction and debugging.
  tty1 is the standard in and output to the screen
  COM0 is the standard output to the serial console 

- **Temp Compositor**  
  A temporary ring 0 compositor that renders a background image and the stdout to the framebuffer

- **Memory Management**
  - Kernel runs in higher half
  - Each process has a 32bit virtual address space
  - On demand paging
  - Basic and unpolished userspace errorhandling
  - User and kernelspace seperation

- **RAM Filesystem (Work in Progress)**  
  - An in-memory filesystem used during early system development.
  - Loads external .img file using grub modules, mounts it to the ram filesystem.

---

## **Architecture**

The goal for openTSP is to follow a **micro-kernel architecture**, executing as much as possible in ring 3.
The kernel isn't a microkernel yet. Its in its early stages of development.
Ring 3 userspace seperation has been implemented, but most drivers and applications run in ring 0 because they depend on the old ring 0 kernel code.
userspace libraries have been implemented using newlib. Not all syscalls are in place. Current syscalls are:
* * read, write, open, close, getpid, lseek, brk, yield, exit, getcwd and chdir. * *
The bash implementation runs in ring 3. source code is also included in /usr/programs

---

## **Project Goals**

The primary goals of this project are:

- Learn practical **kernel development**
- Understand **memory management and scheduling**
- Build fundamental OS components from scratch
- Experiment with system design decisions

The project prioritizes **learning, experimentation, and simplicity** over completeness or production readiness.

---

## **Roadmap**

Planned next steps for the system:

- Improve and stabilize the **RAM filesystem**
- Implement a **disk-based filesystem**
- Port **TinyCC (TCC)** to allow compiling programs directly inside the OS
- Expand userspace support
- Use graphics framebuffer instead of VGA for more complicated graphics.
- move kernel drivers to ring 3 for a microkernel design
- cleanup old kernel code
---

## **Building**

### **Requirements**

- GCC cross-compiler (e.g. `x86_64-elf-gcc`)
- NASM
- Make
- QEMU

### **Build**

```bash
make
```

## **Boot Process**

A simplified overview of the system startup sequence:
- Bootloader loads the kernel
- Kernel initializes basic hardware and memory
- Paging is enabled
- Bootloader modules are parsed
- Kernel memory gets mapped to the higher half
- Jump to higher half
- Filesystem gets mounted.
- Scheduler and multitasking are initialized
- Core subsystems (TTY, compositor) start
- System enters the main kernel loop

## **Project Structure**
wip

## **Screenshots**
below is a screenshot of the bash program running in ring 3 userspace. fork and exec are not implemented so only builtin bash commands are currently working such as cd and echo.

everything is rendered by a temporary ring 0 compositor that renders a background image that is loaded into the temp file system aswell as the font.

<img width="806" height="491" alt="image" src="https://github.com/user-attachments/assets/a46b7a1c-e1e5-4bfa-92d8-a00f2c2bd6ac" />




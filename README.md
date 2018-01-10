
Running the code 
================

Run `make` to build the project, `make run` to run it in QEMU, and `make clean` 
to remove built objects. `make prep` will create `myos.iso` which is bootable.
`make write-usb` will overwrite `/dev/sdb` with this ISO. (Be careful; that drive is
assumed to be a USB.)



Status of the project
=====================

The current status is:

| branch   | status      | keywords                                                   |
|----------|-------------|------------------------------------------------------------|
| lab1     | done        | bootloader, mode switching                                 |
| lab2     | done        | physical memory allocator, page tables                     |
| lab3     | done        | running user-mode ELF files in an "environment" (process)  |
| lab4     | done        | preemptive multitasking, COW fork(), IPC                   |
| lab5     | done        | file system and shell                                      |
| lab6     | done        | networking                                                 |
| graphics | done        | graphical user interface                                   |
| hardware | done        | running the OS on real hardware                            |



Overview of each lab
====================

To provide an overview, here's a summary of my work so far:

- Lab 1 was about getting the kernel running. The BIOS loads a small chunk of
  code, the bootloader, which is the first kernel code that runs. It runs in
  16-bit real mode, so the loader must set up a rudimentary page table, switch
  to 32-bit protected mode, and load the rest of the kernel.
- Lab 2 was about memory management. We wrote a bootstrap memory allocator and
  a physical page allocator. We also wrote functions for manipulating page
  tables, which we used to properly set up the kernel page table.
- Lab 3 involved getting *a* process to run in user mode. We wrote code which
  loads an ELF file into the address space of a process. We also initialized
  the Interrupt Descriptor Table to handle exceptions and traps without the
  kernel crashing. Finally we wrote the code for handling system calls.
- In lab 4 we made *multiple* processes work together. We brought up secondary
  processors and added kernel locking. We wrote a primitive scheduler and
  implemented preemptive multitasking. We wrote an exokernel-style
  copy-on-write fork function. Finally we added syscalls for IPC.
- In lab 5 we implemented a file system. We then wrote an FS daemon which
  performs disk i/o in userland. The daemon uses block caching for efficiency.
  We also implemented a simple shell for running programs from the disk.
- In lab 6 we connected the OS to the internet by implementing a driver for
  the e1000 network card. We also wrote a small web server which can serve
  files from the file system.
- In the graphics lab we implemented OS support for a graphical user
  interface. We implemented a central display server responsible for rendering
  applications. We wrote a user-mode graphics library and a PS/2 mouse driver.
  We also wrote a graphical paint application and a terminal emulator.
- In the hardware lab we made changes to the OS to enable it to run on real
  hardware.


Learning more
=============

The report/report.pdf file contains a detailed writeup of this work. The
writeups/ directory contains internal writeups made during development.

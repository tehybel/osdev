

The code in this repository is based on the MIT
[6.828](https://pdos.csail.mit.edu/6.828/2016/) OS engineering course, which I
have covered in full by now. I'm currently adding more advanced features, such
as graphics, to the OS.

The current status is:

| branch   | status      | keywords                                                   |
|----------|-------------|------------------------------------------------------------|
| lab1     | done        | bootloader, mode switching                                 |
| lab2     | done        | physical memory allocator, page tables                     |
| lab3     | done        | running user-mode ELF files in an "environment" (process)  |
| lab4     | done        | preemptive multitasking, COW fork(), IPC                   |
| lab5     | done        | file system and shell                                      |
| lab6     | done        | networking                                                 |
| graphics | in progress | graphical user interface                                   |


To provide an overview, here's a summary of the different labs and work so far:

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
- As a more advanced feature, work is in progress on a GUI.

The writeups/ directory contains detailed writeups for each branch.  

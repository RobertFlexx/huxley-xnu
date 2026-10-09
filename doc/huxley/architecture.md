# HXNU architecture at the first engineering checkpoint

The kernel retains XNU's Mach, BSD and I/O Kit architecture. This change adds
an EFI memory-map intake boundary and an explicit legacy serial selection;
it does not introduce a competing machine or driver framework.

| Layer | Source and responsibility | Current independence boundary |
| --- | --- | --- |
| Mach | `osfmk/kern`, `osfmk/ipc`, `osfmk/vm`: tasks, scheduling, IPC, VM | Core contracts and synchronization retained; x86 intake changes are described below |
| BSD | `bsd/kern`, `bsd/vfs`, network trees: processes, Unix interfaces, files | Preserve Darwin ABI; missing root drivers/userspace remain external |
| Device core | `iokit/Kernel`, `iokit/IOKit`: services, registry, DMA, work loops | Retain; platform/storage/PCI families are separate dependencies |
| Platform Expert | `pexpert/i386`, `pexpert/arm`, `pexpert/pexpert` | Owns boot arguments, tree, console and platform callbacks |
| Architecture | `osfmk/i386`, `osfmk/x86_64`, `osfmk/arm*` | Keep direct CPU, MMU, interrupt and timer paths |
| Runtime/support | `libkern`, `libsa`, `security` | C++ runtime, boot support, MAC hooks; external security providers need audit |
| User boundary | `libsyscall`, exported headers, `bsd/kern/syscalls.master` | Existing ABI and header generation, not a Huxley C library |

The x86 startup sequence is grounded in these callers:

1. `osfmk/x86_64/start.s:_start/pstart` receives a physical `boot_args` pointer
   in EAX in 32-bit protected mode with paging disabled. It builds bootstrap
   page tables and enters long mode, then calls `vstart`.
2. `osfmk/i386/i386_init.c:vstart` initializes the boot CPU, validates and binds
   the bootstrap command line through `PE_init_boot_args`, then performs early
   serial discovery. It fixes kernel image addressing before full
   `PE_init_platform(FALSE)` and `Idle_PTs_init`, then calls `i386_init`.
   Binding boot arguments does not initialize Platform Expert or the tree;
   full platform/tree setup still precedes paging randomization. Enabled KASAN
   validates the map before reserving shadow memory during this bootstrap.
3. `i386_init` initializes TSC/early clock, startup callbacks and CPU state,
   advances to KPRINTF callbacks, counts firmware CPUs, and calls
   `i386_vm_init`. The new validator runs before the EFI map becomes PMAP
   memory regions. Later `pmap_bootstrap` remains the existing implementation.
4. `PE_init_platform(TRUE)`, console setup, thread/processor bootstrap and
   `machine_startup` lead to `osfmk/kern/startup.c:kernel_bootstrap`.
5. `kernel_bootstrap` calls `vm_mem_bootstrap`, `sched_init`, advances through
   MACH_IPC startup (`osfmk/ipc/ipc_init.c:ipc_init`), and initializes tasks and
   threads before loading the bootstrap thread's context.
6. `kernel_bootstrap_thread` starts scheduler services, clocks and devices,
   calls `PE_init_iokit`, enables interrupts at the established startup stage,
   initializes commpages and later calls `bsd_init`. On x86, Platform Expert
   begins driver matching early; CPU enumeration is required by lockdown.
7. `bsd/kern/bsd_init.c:bsd_init` initializes credentials, VFS, networking and
   process state; `setconf` invokes `IOFindBSDRoot`, then `vfs_mountroot` mounts
   root. `bsd_utaskbootstrap` creates the first userspace task.
8. `bsd_utaskbootstrap` marks the new thread with a BSD AST. On first execution,
   `bsd/kern/kern_sig.c:bsd_ast` dispatches to `bsdinit_task`, which sets up
   exception-to-signal handling and invokes
   `bsd/kern/kern_exec.c:load_init_program`, currently selecting launchd paths
   through the existing Mach-O exec and code-signing path.

ARM64 follows `osfmk/arm64/start.s` to `osfmk/arm/arm_init.c:arm_init` (or the
SPTM path), then machine startup and the common Mach/BSD bootstrap. Its board,
interrupt and memory assumptions are distinct; none was replaced in this pass.

The immediate design rule is to validate data where ownership changes, keep
firmware normalization with the loader, and leave VM allocation policy with
PMAP. The allocation-free validator has no callbacks, global capability
booleans or allocation dependencies. Kernel consumers continue to own the
original map's lifetime. There is no generic PC platform backend yet.

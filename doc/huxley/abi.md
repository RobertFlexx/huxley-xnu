# ABI and native userspace boundary

No syscall numbers, Mach traps, public layouts, executable formats, commpage
behavior, `uname` identity or security policies changed in this pass.
`config/version.c.template` still derives the inherited `ostype` and version.
The XNU import version, eventual HXNU project version and Darwin ABI version
must remain separate. Build manifests retain import/source identity without
inventing a new kernel ABI or changing userspace feature discovery.

| Facility | Actual implementation boundary | Huxley status |
| --- | --- | --- |
| BSD syscall table/conventions | `bsd/kern/syscalls.master`, architecture syscall entry files | Implemented inherited Darwin semantics; NOT RUN |
| fork/process creation | `bsd/kern/kern_fork.c` | Implemented; runtime behavior NOT RUN |
| exec/posix_spawn | `bsd/kern/kern_exec.c`, `mach_loader.c` | Mach-O loading and code-signing policy; future libc/dyld dependency, NOT RUN |
| signals/credentials/sessions | `kern_sig.c`, `kern_prot.c`, `kern_credential.c` | Implemented inherited behavior; NOT RUN |
| descriptors/pipes/sockets | `kern_descrip.c`, `sys_pipe.c`, `uipc_*` | Implemented; device/network availability separate, NOT RUN |
| VFS/device nodes/TTY | `bsd/vfs`, `bsd/miscfs/devfs`, `bsd/kern/tty.c` | Implemented; root/block drivers and user device management missing |
| poll/kqueue | `sys_generic.c`, `kern_event.c` | Implemented Darwin interfaces; NOT RUN |
| VM/mapping/timers | `osfmk/vm`, `bsd/kern/kern_mman.c`, `kern_time.c` | Implemented; Mach traps/libsyscall glue required, NOT RUN |
| sysctl/process inspection | `kern_sysctl.c`, `kern_proc.c`, `kern_mib.c` | Darwin structures/semantics retained; NOT RUN |
| user headers/glue | header-export Makefiles, `libsyscall` | SDK-derived build; no independent Huxley libc yet |

This is an implementation inventory, not POSIX certification or evidence of
usable shells. The existing kernel primitives need an independently linked
native runtime, libc, executable loader, filesystem and init before application
tests can run. Darwin ABI differences should be tested with native programs
instead of adopting Linux syscall names or changing public layouts silently.

Process 1 follows `bsd_init` -> `bsd_utaskbootstrap`, which creates its thread
and sets its BSD AST. The thread's first `bsd/kern/kern_sig.c:bsd_ast` dispatches
to `bsdinit_task` -> `load_init_program`. The exec routine searches launchd paths and has debug
suffix overrides. A future Huxley init contract should select a real native
init executable through `load_init_program_at_path`, preserving credentials,
exception-to-signal setup, Mach-O validation and code-signing enforcement.
There is no init implementation or alternate path in this patch.

Huxley Base should be a separate repository. Its first acceptance test should
execute a minimal Mach-O process with write/exit, followed by fork/exec, signal,
pipe, socket and VM tests. A pkgsrc platform port will need native compiler,
libc/header feature discovery, dynamic linking, shell/utilities and tested
Unix semantics. pkgin needs packages rebuilt for that ABI; NetBSD binaries
cannot be presumed compatible. Linux ABI emulation is deferred.

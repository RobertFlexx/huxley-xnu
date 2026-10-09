# Next implementation gates

The first completed slice is firmware intake/serial selection with host and
QEMU firmware validation. Full kernel boot remains blocked. Proceed according
to actual dependencies:

1. Reproduce the imported kernel with compatible public MIG, headers, host
   generators, crypto archive and Mach-O link tools. Isolate SDK/tool discovery
   from compiler configuration; record Darwin ABI version explicitly. Compile
   the modified VM unit in the real generated-header environment before making
   further VM changes. Complete the remaining EFI, hibernation/resume bitmap
   arithmetic and panic-map consumer audit alongside this work. Gate: actual kernel image plus symbol/artifact checks
   (BOOT-0), not these diagnostic objects.
2. Implement a separate development UEFI loader with Mach-O/collection layout,
   slide/fixup handling, device tree/entropy, reservations and firmware ownership
   transfer. Reuse the tested EFI map contract and UART path. Gate: image
   acceptance and captured architecture entry/serial logs (BOOT-1/2/3).
3. Bound existing ACPI table/MADT traversal and implement an I/O Kit generic
   platform provider. Add firmware CPU/APIC/timer/PCI resource discovery with
   concrete callers. Gate: VM and interrupt/timer progress, then measured SMP
   delivery/bring-up (BOOT-4/5). Do not mistake CPU counts for SMP operation.
4. Select compatible public root storage/filesystem components and an independent
   trust policy/provider. Mount a disposable test root; execute a minimal native
   process using the inherited exec path. Gate: Mach/BSD progress, defined
   handoff and actual process execution (BOOT-6/7/8).
5. Run scheduling/IPC/syscall/VM/device/filesystem regressions and failure/stress
   tests before adding general hardware or performance claims.
6. Establish an independent ARM64 `virt` platform/loader contract, including page
   sizes, exception level, GIC, timer, MMIO/cache ordering and SMP firmware.
   Reuse architecture-independent checks where applicable; keep Apple/VMAPPLE
   and x86 regression paths. ARM64 object compilation alone is insufficient.

After native base stability, extract boot, Huxley Base and package infrastructure
into separate repositories. Display buffer/modesetting/synchronization APIs
and Mesa/Wayland/X11/KDE userspace ports follow real device/userspace consumers.
No desktop logic belongs in the kernel. The initial source-based graphics
comparison is in `platforms.md`; implementing device interfaces, drivers and
userspace ports remains future work. Performance baselines, panic/debugger
integration and broader ABI conformance are also unfinished.

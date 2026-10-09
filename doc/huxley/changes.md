# Initial engineering decisions and validation

## EFI map intake

The VM bootstrap checked only that descriptor stride was nonzero. A short
stride, partial tail, zero page count or overflow could be interpreted as
physical allocation data. The new internal header validates metadata and the
whole map before the existing consumer reads ranges. It is exported by the
existing pexpert header Makefile, so upstream build inclusion is explicit.
KASAN reads the map earlier than VM, so it validates independently before
reserving shadow memory and excludes EFI runtime ranges from allocation.
Runtime descriptors are checked against EFI's page-count narrowing and
effective kernel-shadow mapping endpoints and the actual page-table physical
mask. ACPI NVS and PAL ranges have
additional page-number/count bounds for hibernation's existing bitmap consumer.

A freestanding inline helper avoids new early allocation/link/runtime
dependencies and lets the same implementation execute in host and UEFI tests.
It preserves the inherited wire ABI and PMAP policy instead of adding an unused
new boot structure or sorting/modifying firmware data in the kernel. Sorted
disjoint input is required by existing PMAP behavior; unsupported descriptor
versions fail explicitly. Readability/reservations/authentication are not solved.

Review also found two existing PMAP boundary errors: an inclusive range ending
at `first_avail` could expose bootstrap pages, and splitting at the last table
slot could write past the region array. The reservation comparison now includes
that endpoint, and a split requires two slots before either write. These fixes
are independently reviewed; VM/KASAN translation-unit and runtime validation
remain blocked by the real kernel build prerequisites.

## Serial selection and baud validation

The inherited driver probed Apple/PCH-specific MMIO before COM1, and accepted
`serialbaud=0` into a remainder operation. `legacy_uart=1` now restricts discovery
to its existing COM1 scratch probe and forbids MMIO fallback even if absent.
Baud rates must be nonzero, divide the base rate, and fit a 16-bit divisor.
Invalid values retain the existing default. A new driver/vtable was unnecessary;
the existing MMIO/legacy/PCIe ordering remains when the argument is absent/zero.

The first serial initialization occurs before `PE_init_platform` installs the
boot-argument pointer. A bounded command-line binder now runs in `vstart`
before that initialization, without performing premature tree/platform setup.
It rejects a null pointer or a command line without a NUL inside its 1024-byte
field. Host integration and the firmware probe use this binder and the actual
inherited parser, rather than substituting successful argument lookups.

## Public development validation

`tools/huxley/hxnu.py` executes real host tests, LLVM protocol-object builds,
the inherited full build, and a separate PE/COFF UEFI diagnostic. It logs exact
commands/results, hashes source changes/artifacts, and imposes execution
deadlines. Its public firmware path provides real serial/map evidence without
pretending to solve the kernel SDK/loader/platform dependencies.

## Executed results

| Check | Result / evidence |
| --- | --- |
| Untouched full baseline build | FAIL exit 2; missing SDK/tools and Darwin host discovery, `BUILD/huxley/baseline/make.log` |
| Developer full build | FAIL exit 2 before C compilation; SDK/KDK version/tool discovery, `BUILD/huxley/kernel-x86_64-development/make.log` |
| Host EFI validation | PASS, 60,063 checks; padding, ordering, runtime/hibernation bounds, physical mapping mask, corruption, guard page and generated cases under ASan/UBSan |
| Actual serial-driver fixture | PASS, thirteen cases covering UART presence, baud bounds, RX/TX/error, MMIO/PCIe preference and legacy restrictions |
| Early boot-argument integration | PASS, bounded binding and actual parser/driver interaction under ASan/UBSan |
| Diagnostic tooling | PASS, seven Python regressions for resource reporting, missing prerequisites, result history and timeout child cleanup; synthetic compiler/firmware inputs make no build/boot claim |
| x86-64 / ARM64 protocol Mach-O objects | PASS, four objects using compiler and actual kernel type headers, ABI assertions, CPU/type and undefined-symbol checks; no full-kernel compilation claim |
| QEMU/OVMF firmware probe | PASS, three cycles; 98 descriptors, 48-byte stride, real COM1 and corruption rejection |
| Full-kernel regression/unit tests | BLOCKED by SDK/generated headers/kernel execution prerequisites |
| Kernel boot / physical hardware / ARM64 virtual hardware | NOT RUN; no boot milestone achieved |
| GitHub workflow | NOT RUN remotely; checked-in workflow invokes executed local checks |

QEMU serial evidence (`BUILD/huxley/firmware/serial-0.log` through `serial-2.log`):

```text
HXNU firmware: serial PASS
HXNU firmware: memory-map PASS descriptors=0x0000000000000062 stride=0x0000000000000030
HXNU firmware: reject-zero PASS
```

Initial native Clang sanitizer linking failed because runtime archives were
missing; GCC resolved it. GCC LeakSanitizer was blocked by ptrace; explicit
LSan disabling leaves ASan/UBSan checks active. The first firmware invocation
failed on read-only FAT attachment, then QEMU's temporary overlay encountered
the sandbox's read-only `/var/tmp`; the final run used a snapshot and authorized
QEMU execution outside that restriction. These were diagnosed, not counted as
passing attempts. Kernel VM translation-unit compilation/runtime integration
remains unverified until the full build prerequisites are available.
The final harness sets QEMU's `TMPDIR` to its ignored build directory, and
subsequent firmware cycles pass inside the normal workspace sandbox.

As a baseline regression check, the unchanged imported serial driver was built
against the same fixture and executed with `serialbaud=0`: UBSan reported its
division by zero. The corrected driver passes that case. The tooling also has
focused negative-path checks for stale summaries, missing tools, timeout child
cleanup and final result/history agreement; these do not claim kernel behavior.
The firmware acceptance regression exercises the actual tool's missing-marker
branch with synthetic inputs and rejects exit 33 without complete serial
evidence. The full hibernation/resume and panic-map consumer audit remains
unfinished; `security.md` records identified inherited arithmetic limits.

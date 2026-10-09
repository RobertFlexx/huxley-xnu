# Developer build and validation

Run from any directory; the tool locates the source root itself:

```sh
python3 tools/huxley/hxnu.py doctor
python3 tools/huxley/hxnu.py test
python3 tools/huxley/hxnu.py firmware-test --cycles 3
python3 tools/huxley/hxnu.py build --config development --arch x86_64
```

`doctor` records tool versions/paths and source metadata in
`BUILD/huxley/doctor.json`. Exit 2 means a missing prerequisite. An environment
report does not certify that an SDK contains all kernel-private headers/libs.

`test` first executes seven Python diagnostic regressions covering result
reporting, resource discovery, missing tools and timeout cleanup. Their mocked
compiler/firmware inputs test control flow and make no build/boot claim; the
timeout check executes a real parent/child process pair. It then compiles and
executes the actual firmware validator, serial driver,
early boot-argument binding and inherited boot-argument parser,
with AddressSanitizer and UBSan, then compiles the protocol into x86-64/ARM64
Mach-O objects and verifies object magic/CPU/type and the absence of undefined
symbols with `llvm-nm`. A missing symbol inspection tool records BLOCKED and
prevents a passing test summary. Use `--cc gcc` or `--cc clang`
to choose a host compiler with installed sanitizer runtimes. The default is
`cc`; this host's Clang 23 lacks its ASan runtime, so GCC 16 is used. LeakSanitizer
is disabled explicitly for these allocation-free tests because it cannot run
under the host's ptrace sandbox. Bounds/lifetime and undefined behavior checks
remain fatal. Inherited serial-only unused-function/set-variable warnings are
suppressed; new test/validator code builds with warnings as errors.
The native fixture compiles the inherited boot parser as a separate object
with its incompatible-pointer warning suppressed: Linux `int64_t` differs
from the parser's `long long` parameter in `get_range_bounds`. These tests
exercise command-line arguments rather than that range API and make no claim
of Darwin type compatibility for the fixture. The parser source is unchanged;
the Windows X64 EFI build does not need this suppression.

`firmware-test` builds a standalone X64 PE/COFF EFI diagnostic using public
Clang and `lld-link`, verifies its image/subsystem, and runs it in QEMU/OVMF.
It executes `pexpert/i386/pe_serial.c` with real port I/O, binds boot arguments
through `pexpert/i386/pe_bootargs.c` and parses them through the inherited
`pexpert/gen/bootargs.c`. It calls the same EFI
validator used by the VM intake. Unexpected MMIO fails immediately. It obtains
the map through firmware GetMemoryMap, normalizes ordering in its private copy
if necessary, checks the map, and verifies rejection after corruption. It
does not call ExitBootServices, load HXNU or claim a kernel boot milestone.

Requirements for that diagnostic: `clang`, `lld-link`, `qemu-system-x86_64` and
a matching OVMF code/variable template. Defaults are
`/usr/share/OVMF/OVMF_CODE_4M.fd` and `OVMF_VARS_4M.fd`; override with
`--ovmf-code` / `--ovmf-vars`. The test copies NVRAM, uses a generated FAT
directory and temporary snapshot, and never touches host boot settings/disks.
QEMU's temporary overlays use the writable `BUILD/huxley/firmware/tmp`
directory via `TMPDIR`. Every cycle has a
40-second deadline (`--timeout` can override it). PASS requires exit 33 from
the QEMU test-only isa-debug-exit device plus all three serial markers; a
timeout or mere firmware banner is FAIL. The linked PE timestamp is fixed at
zero, and manifests record firmware/image hashes and QEMU version.

`build` invokes the inherited Makefiles with actual output directories,
selected DEBUG/DEVELOPMENT/RELEASE configuration, host CPU/memory values and a
single kernel build stripe. It does not substitute a host test for the kernel.
Darwin uses `sysctl` for physical memory and physical/logical CPU counts;
Linux uses `sysconf` for memory and online CPU topology for both CPU counts.
If topology discovery is unavailable, the manifest records an unknown
physical count and the tool leaves that inherited variable unspecified.
Use `--sdk` for an actual suitable SDK/KDK configuration, `--jobs` and
`--timeout` as needed. ARM64 requires an explicit inherited `--machine`, such
as VMAPPLE; that configuration does not target QEMU `virt`. Default output is
`BUILD/huxley/kernel-<arch>-<config>/`; command, failure log and source manifest
are retained there.

The full build currently fails before C compilation. `MakeInc.cmd` resolves
SDKs and tools using xcrun, including Clang, MIG/MIGCOM, IIG, Mach-O tools,
dsymutil, unifdef and host lexer/parser tools. `SETUP/` builds generators;
`config/`, exported headers and MIG generate architecture/configuration data.
`MakeInc.kernel` obtains the Darwin kernel version from SDK/KDK System.kext;
`doc/building/xnu_version.md` documents an explicit
`RC_DARWIN_KERNEL_VERSION` override. This tool leaves SDK-derived identity
intact instead of guessing the Darwin ABI version from the XNU source tag.
Linking requires external kernel crypto and collection/driver integration.
LLD's ability to emit Mach-O alone does not prove full link equivalence.

Each command writes readable logs and JSON under ignored `BUILD/huxley/`.
Manifests contain HEAD, tracked/untracked change identity, compiler versions,
configuration, selected tool environment (including explicit SDK/KDK and
`RC_DARWIN_KERNEL_VERSION` settings), command, elapsed time and exit code;
they do not dump the process environment. JSON records PASS/FAIL/BLOCKED;
PARTIAL means some required checks were blocked while others ran. Missing
prerequisites replace the current summary with BLOCKED. Named outputs are
overwritten on reruns, while subprocess logs and finalized result JSON are
retained under each command's `history/` directory. Firmware attempts retain
their corresponding serial logs there after checking the acceptance markers.
No generated kernels/objects are checked in.

The GitHub workflow runs host tests and the firmware probe on Ubuntu with
public packages and retains logs/artifacts. It has been added but has not run
on GitHub in this engineering pass. No CI full-kernel success is claimed.
For the next full build, obtain compatible public headers/tools/libraries
first, compile the modified VM translation unit and compare against baseline,
then inspect/link/package the actual kernel and pursue BOOT-0/1/2 in order.

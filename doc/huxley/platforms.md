# Platform scope and dependencies

The reference diagnostic machine is QEMU `q35`, TCG, the named `Nehalem` CPU
model, one CPU, 512 MiB RAM, OVMF 4 MiB firmware, COM1 at 0x3f8 and a disposable
UEFI FAT boot volume on emulated SATA. Networking and graphical output are
disabled. The manifest records the QEMU version and firmware hashes because
the `q35` alias varies with emulator releases. This is a firmware test target;
kernel CPU compatibility has not been established for this model.

| Support dimension | Status |
| --- | --- |
| x86-64 full HXNU compilation | BLOCKED by SDK/toolchain prerequisites; attempted build failed before C compilation |
| x86-64 platform protocol objects | PASS, freestanding Mach-O with Clang |
| x86-64 QEMU firmware application | PASS, real COM1 output and OVMF memory map, three cycles |
| x86-64 QEMU kernel execution | BLOCKED, no kernel image/independent loader |
| Physical x86-64 | NOT RUN; no support claim |
| ARM64 full kernel compilation | NOT RUN; SDK prerequisites missing |
| ARM64 protocol objects | PASS; compile/layout evidence only, x86 boot protocol is not an ARM platform implementation |
| ARM64 QEMU `virt` / physical ARM64 | NOT RUN; no platform or loader support claim |

Dependency classification and action:

| Dependency / actual source | Classification | Decision and reason |
| --- | --- | --- |
| Mach VM/IPC/tasks/scheduler in `osfmk` | Fundamental XNU | Keep; no evidence justifies replacement |
| x86 page tables, LAPIC, TSC, SMP in `osfmk/i386` | Architecture | Keep fast paths; test them after real entry is available |
| boot arguments, EFI map, chosen tree in `pexpert/i386` | Platform | Encapsulate intake validation; retain ABI and loader ownership |
| Apple board configuration in `pexpert/pexpert/arm64/board_config.h` | Platform | Defer generic ARM64 backend; VMAPPLE is not QEMU `virt` |
| syscalls, Mach traps, commpage, Mach-O in `bsd`/`osfmk` | Darwin ABI | Keep layouts/semantics; Huxley Base must supply libc/loader glue |
| SDK discovery/tool selection in `MakeInc.cmd`; test SDK/darwintest | Development tool | Isolate with host tests and direct public LLVM diagnostic build; full kernel path still requires replacement work |
| MIG interfaces in `osfmk/mach/*.defs` | Missing external open-source build dependency | Obtain/build a compatible public MIG; do not stub generated IPC |
| `-lcc_kext` in `MakeInc.def`, `osfmk/corecrypto`, `EXTERNAL_HEADERS/corecrypto` | Missing external kernel crypto dependency | Match public corecrypto implementation and link contract; headers alone are insufficient |
| ACPI/platform driver matching in `IOPlatformExpert.cpp`, `PE_init_iokit` | Platform / missing external driver | Adapt existing I/O Kit; standalone ACPI/PCI platform provider still missing |
| IOPCIFamily, block-device/storage drivers, root disk filesystem | Missing external open-source dependency | Inventory compatible public releases and licenses; no driver implementation here establishes generic storage |
| AMFI/Image4/CoreTrust interface wrappers in `libkern`, `EXTERNAL_HEADERS` | Potentially replaceable Apple integration | Audit provider behavior and trust boundary; never install unconditional-success replacements |
| SecureDT, kernel collection fixups, platform entropy | Platform plus security | Supply valid loader data and preserve checks before generalizing |
| Private/internal SDK configurations and binaries | Unavailable in this environment | Do not import private binaries or infer redistribution rights; legal availability of a specific replacement needs separate provenance review |

`osfmk/i386/acpi.c` already finds RSDP/RSDT/XSDT tables and counts enabled
local APIC entries in the MADT. It is not a complete ACPI namespace/AML or
PCI routing implementation. Its variable-length table walks warrant bounded
validation before generic firmware exposure. This pass does not add another
ACPI parser or claim comprehensive ACPI support.

I/O Kit's service matching, registry, work loops, interrupt and DMA abstractions
remain in place. A generic platform provider must eventually supply firmware
discovery, CPU/interrupt routing, PCI roots, resource ownership and reset/power
actions through those existing boundaries. A capability structure should be
added with the first real provider/consumer and frozen after early discovery;
no unused capability framework or unrelated global flags are introduced now.

The kernel contains VFS, devfs, NFS, memory-disk support and image-boot paths;
none guarantees an independently mountable local root. A disposable RAM/image
root is a plausible next storage milestone only after a real driver and disk
filesystem implementation are selected. No host disks are used by these tests.

The firmware framebuffer remains future work. Dimensions, pitch, pixel format,
mapping/cache attributes and ownership must be validated before use. Firmware
scanout alone supplies no tested modesetting, acceleration or refresh-rate
control.

Graphics interface comparison (research only; no display API is implemented):

| Approach | Advantage | Cost and acceptance condition |
| --- | --- | --- |
| A: native display device API | Direct fit with I/O Kit ownership, VM and interrupts | Requires Huxley backends in compositor/rendering libraries; prove buffer sharing and presentation with a real userspace consumer |
| B: DRM/KMS-compatible device API | Reuses established libdrm/Mesa/compositor expectations | Must implement ioctl behavior, privileged display ownership, mapped buffers, events and synchronization; Linux internal APIs are not an XNU implementation |
| C: native device core plus targeted DRM/KMS compatibility | Retains XNU resource ownership while allowing selected established userspace interfaces | Requires one core owning buffers/devices and contract tests for every supported operation; unsupported capabilities must fail accurately |

Recommendation: pursue C once native userspace and a real display driver are
available. This is an architectural inference from the interfaces below,
not a commitment to a Linux kernel port or an implemented compatibility layer.
Keep device/resource ownership in I/O Kit; choose the compatibility subset
against a working userspace consumer before freezing a public ABI.

Linux DRM distinguishes display control from unprivileged render access and
defines memory mapping, buffer, event and fence interfaces; KMS adds modes,
connectors, planes and atomic state changes. These concepts transfer; Linux
file operations, internal memory managers and device discovery require native
implementations. [DRM userspace interface](https://docs.kernel.org/gpu/drm-uapi.html),
[KMS](https://docs.kernel.org/gpu/drm-kms.html).
Shared buffers need reference lifetimes, DMA/cache ownership and synchronization,
including device-failure completion. Linux dma-buf is a concrete model to study,
not functionality supplied by an I/O Kit memory descriptor alone.
[dma-buf documentation](https://docs.kernel.org/driver-api/dma-buf.html).

Mesa separates rendering drivers from platform connections through winsys;
GBM allocates device buffers and EGL supplies window-system integration.
A native winsys/EGL port is possible in principle, while existing DRM paths
require the corresponding real device operations. [Mesa source architecture](https://docs.mesa3d.org/sourcetree.html),
[EGL](https://docs.mesa3d.org/egl.html).
Vulkan WSI defines platform surfaces and swapchain presentation in userspace;
its Wayland surface uses compositor protocol objects. No new kernel window
object follows from that API. [Vulkan WSI](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html).

Wayland keeps compositing in a userspace display server and requires hardware
display access and shared client buffers. Xorg exposes a device-dependent porting
layer, and KWin has a concrete DRM backend. Huxley therefore needs native device,
input/session and rendering integration before X11, Xwayland or KDE can be
tested. [Wayland architecture](https://wayland.freedesktop.org/architecture.html),
[X server porting layer](https://xorg.freedesktop.org/archive/current/doc/xorg-server/Xserver-spec.html),
[KWin DRM backend](https://github.com/KDE/kwin/blob/master/src/backends/drm/drm_backend.cpp).
High-refresh presentation remains dependent on measured mode timing, vblank,
fences and compositor pacing; no refresh-rate claim is made here.

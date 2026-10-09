# Boot boundary and validation

HXNU has not booted. No BOOT-0 through BOOT-8 milestone is achieved: the full
kernel/dependencies do not build on the current host. The QEMU firmware probe
executes a separate test application, not the kernel entry point.

The implemented intake contract retains `pexpert/pexpert/i386/boot.h`:
`boot_args` is 4096 bytes, Version 2 with inherited Revision fields;
`EfiMemoryRange` has a 40-byte prefix. No unused Huxley wire protocol is added.
The compile tests freeze size and important offsets for both 64-bit ABIs.
This is a documented inherited loader boundary, not validation of every field:
image layout, boot-argument version/revision and device-tree integrity still
need a complete loader/kernel intake review.

`vstart` now calls `PE_init_boot_args` before its first serial initialization.
The loader must supply a readable `boot_args` whose 1024-byte `CommandLine`
contains a NUL. The binder rejects a missing terminator and binds only
`PE_state.bootArgs`; it does not initialize firmware discovery or the device
tree. Later `PE_init_platform` replaces the bootstrap identity pointer with
the final mapped pointer. An unbound `PE_boot_args` returns an empty string so
diagnostic parsing cannot read page zero. These early entry changes have host
integration tests; their actual kernel-entry execution remains unverified.

| Boot-map field | Type / interpretation | Ownership and requirements |
| --- | --- | --- |
| `MemoryMap` | uint32 physical byte address | Mandatory, nonzero, 8-byte aligned, mapped/readable by the bootstrap; whole buffer below or ending at 4 GiB |
| `MemoryMapSize` | uint32 byte count | Mandatory, nonzero, exact multiple of descriptor stride |
| `MemoryMapDescriptorSize` | uint32 byte stride | Mandatory, at least 40 and multiple of 8; larger descriptor tails preserved/ignored |
| `MemoryMapDescriptorVersion` | uint32 descriptor schema version | Version 1 supported; others fail explicitly pending a version review |
| descriptor `Type` | uint32 firmware memory class | Known usable classes retain PMAP policy; unknown types remain unavailable |
| `PhysicalStart`, `VirtualStart` | uint64 byte addresses | 4 KiB aligned; last page must be representable |
| `NumberOfPages` | uint64 count of 4 KiB pages | Nonzero, no range/accounting overflow; PMAP usable pages fit uint32 `ppnum_t` |
| `Attribute` | uint64 firmware/kernel attributes | Preserved, including runtime and kernel reservations |

Descriptors marked `EFI_MEMORY_RUNTIME`, including unknown types, must also
fit the inherited uint32 page-count conversion and non-wrapping exclusive
physical/virtual mapping endpoints. The validator uses the caller's kernel
shadow base to check the same low-virtual-address conversion as `efi_init`.
It also uses the caller's page-table physical mask to reject runtime addresses
that would alias after `pmap_map_bd` truncation. The CPU's potentially narrower
physical-address width still needs platform validation.
ACPI NVS and PAL ranges must fit uint32 page numbers/counts because hibernation
saves them, even though PMAP does not allocate them.

The loader owns allocation, ordering and reservations until handoff. It must
provide ascending, disjoint physical ranges for existing PMAP coalescing and
ordered allocation/truncation. This ordering is an HXNU intake rule,
not a claim that GetMemoryMap guarantees sorted output. The map is not freed
after initial validation: EFI and hibernation paths reference it later.

When enabled, `san/memory/kasan-x86_64.c` performs the same checks before its
earlier map intake and shadow-memory reservation, and excludes EFI runtime
memory from shadow allocation. VM revalidates the map after KASAN adjusts a
range. `osfmk/i386/i386_vm_init.c` checks metadata before `ml_static_ptovirt`, then
calls `PE_efi_memory_map_validate_ranges` before converting any descriptor to
PMAP ranges. Invalid input panics with a status and descriptor index. The
validator accepts the last representable physical page for reserved memory,
uses page arithmetic to avoid exclusive-end wrap, and checks aggregate byte
accounting. It validates structure/arithmetic, not mapping accessibility,
firmware authenticity, reservation completeness or concurrent mutation.
Hibernation's separately supplied replacement map is outside this initial
intake validation and requires its own review.

PMAP's existing split now correctly reserves bootstrap pages when an inclusive
range ends exactly at `first_avail`, and checks for two region-table slots
before writing a split. Those consumer fixes have source review but have not
been compiled or executed in a kernel because the build prerequisites are
missing.

A future standalone loader must additionally satisfy these actual XNU needs:

- Load appropriate Mach-O segments/kernel collection and relocate/slide them;
  `vstart`, `i386_vm_init` and kernel collection fixups consume those layouts.
- Provide the device tree, chosen boot properties and entropy required by
  `PE_init_platform` and early random initialization. Do not replace entropy
  or trust-provider failures with successful stubs.
- Reserve kernel, stack, map, tree, modules, page tables and firmware runtime
  memory; accurately represent framebuffer and firmware tables.
- Complete `ExitBootServices` using the final map key, then establish the
  32-bit protected-mode, flat-segment, paging-off entry state documented in
  `start.s`. A jump from UEFI's 64-bit environment is insufficient.
- Preserve inherited executable/module verification and security metadata,
  or implement and validate an explicit independent trust model first.

The independent loader belongs in a future boot repository. The current
`tests/huxley/firmware_probe.c` can be extracted as a diagnostic, but is not a
loader. It intentionally keeps boot services alive and never allocates OS
memory, exits firmware ownership, or jumps into HXNU.

For early serial diagnostics, `legacy_uart=1 serialbaud=115200` selects the
existing verified COM1 path without MMIO probing. `debug=0x8` requests KPRINTF
serial through the inherited DB_KPRT flag; `serial=3` requests input/output
through the inherited serial-console flags. These are intended kernel boot
arguments, not evidence that this kernel has run. Without `legacy_uart`,
upstream MMIO/legacy/PCIe preference is preserved.

Firmware descriptor semantics were checked against the primary
[UEFI Boot Services specification](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html?highlight=exitbootservice).
The implemented descriptor stride and nonzero/alignment checks follow that
contract; HXNU's version/order/address limitations are stated above.

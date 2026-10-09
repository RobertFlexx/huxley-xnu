/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#include <stddef.h>
#include <pexpert/i386/efi_memory_map.h>

/* Freeze the inherited wire layout; cross-compilation is not a kernel boot. */
_Static_assert(sizeof(boot_args) == 4096, "boot_args ABI changed");
_Static_assert(sizeof(EfiMemoryRange) == 40, "EFI descriptor prefix changed");
_Static_assert(offsetof(EfiMemoryRange, PhysicalStart) == 8, "physical offset");
_Static_assert(offsetof(EfiMemoryRange, NumberOfPages) == 24, "page count offset");
_Static_assert(offsetof(boot_args, MemoryMap) == 1032, "map pointer offset");
_Static_assert(offsetof(boot_args, KC_hdrs_vaddr) == 1256, "KC pointer offset");

PE_efi_memory_map_status
hxnu_check_boot_map(const boot_args *args, const void *map,
    uint64_t runtime_virtual_base, uint64_t runtime_physical_max, uint32_t *bad_index)
{
	PE_efi_memory_map_status status = PE_efi_memory_map_validate_metadata(
		args->MemoryMap, args->MemoryMapSize, args->MemoryMapDescriptorSize,
		args->MemoryMapDescriptorVersion);
	if (status != PE_EFI_MEMORY_MAP_VALID) {
		return status;
	}
	return PE_efi_memory_map_validate_ranges(map, args->MemoryMapSize,
	           args->MemoryMapDescriptorSize, runtime_virtual_base,
	           runtime_physical_max, bad_index);
}

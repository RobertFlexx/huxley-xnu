/*
 * Copyright (c) 2026 Huxley contributors.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _PEXPERT_I386_EFI_MEMORY_MAP_H
#define _PEXPERT_I386_EFI_MEMORY_MAP_H

#include <pexpert/i386/boot.h>
#include <pexpert/i386/efi.h>

/* Internal intake checks; this header does not change the boot_args ABI. */
typedef enum PE_efi_memory_map_status {
	PE_EFI_MEMORY_MAP_VALID = 0,
	PE_EFI_MEMORY_MAP_BAD_ADDRESS,
	PE_EFI_MEMORY_MAP_BAD_SIZE,
	PE_EFI_MEMORY_MAP_BAD_STRIDE,
	PE_EFI_MEMORY_MAP_BAD_VERSION,
	PE_EFI_MEMORY_MAP_BAD_ALIGNMENT,
	PE_EFI_MEMORY_MAP_EMPTY_RANGE,
	PE_EFI_MEMORY_MAP_RANGE_OVERFLOW,
	PE_EFI_MEMORY_MAP_BAD_ORDER,
	PE_EFI_MEMORY_MAP_PAGE_OVERFLOW
} PE_efi_memory_map_status;

/* Validate before converting the boot_args physical pointer to a mapping. */
static inline PE_efi_memory_map_status
PE_efi_memory_map_validate_metadata(uint32_t physical_address, uint32_t map_size,
    uint32_t descriptor_size, uint32_t descriptor_version)
{
	if (physical_address == 0 || (physical_address & 7) != 0 ||
	    (uint64_t)physical_address + map_size > (UINT64_C(1) << 32)) {
		return PE_EFI_MEMORY_MAP_BAD_ADDRESS;
	}
	if (descriptor_size < sizeof(EfiMemoryRange) || (descriptor_size & 7) != 0) {
		return PE_EFI_MEMORY_MAP_BAD_STRIDE;
	}
	if (map_size == 0 || map_size % descriptor_size != 0) {
		return PE_EFI_MEMORY_MAP_BAD_SIZE;
	}
	if (descriptor_version != 1) {
		return PE_EFI_MEMORY_MAP_BAD_VERSION;
	}
	return PE_EFI_MEMORY_MAP_VALID;
}

/*
 * The caller owns a readable map_size-byte buffer for the duration of this
 * allocation-free check and subsequent consumption. Firmware descriptors may
 * have a larger stride than the known prefix. Do not modify or reclaim this
 * buffer while the kernel's EFI/hibernate consumers still reference it.
 *
 * X86 PMAP coalescing and ordered page allocation require ascending, disjoint
 * physical ranges. A loader must normalize firmware ordering before handoff.
 * Unknown types remain unavailable to PMAP. Allocatable ranges and the NVS/PAL
 * ranges saved by hibernation must fit the existing 32-bit ppnum_t; this does
 * not impose that limit on reserved MMIO.
 * Runtime ranges must also fit the existing uint32 page-count conversion and
 * the exclusive endpoints used by EFI mapping. runtime_virtual_base is the
 * architecture's kernel shadow base, as used by efi_init() for low EFI virtual
 * addresses; the caller supplies it rather than encoding a kernel layout here.
 * runtime_physical_max is the highest physical address representable by the
 * caller's page tables; runtime ranges must not be truncated by their mask.
 * A narrower CPU-specific physical-address limit remains a platform contract.
 * bad_index, when supplied, receives the first failing descriptor, or zero
 * for a layout failure or success. No descriptor is read on a layout failure.
 */
static inline PE_efi_memory_map_status
PE_efi_memory_map_validate_ranges(const void *map, uint32_t map_size,
    uint32_t descriptor_size, uint64_t runtime_virtual_base,
    uint64_t runtime_physical_max, uint32_t *bad_index)
{
	const uint64_t max_page = UINT64_MAX >> 12;
	uint64_t previous_last_page = 0;
	uint64_t total_bytes = 0;

	if (bad_index != 0) {
		*bad_index = 0;
	}
	if (map == 0 || (uintptr_t)map > UINTPTR_MAX - map_size) {
		return PE_EFI_MEMORY_MAP_BAD_ADDRESS;
	}
	if (((uintptr_t)map & 7) != 0) {
		return PE_EFI_MEMORY_MAP_BAD_ALIGNMENT;
	}
	if (descriptor_size < sizeof(EfiMemoryRange) || (descriptor_size & 7) != 0) {
		return PE_EFI_MEMORY_MAP_BAD_STRIDE;
	}
	if (map_size == 0 || map_size % descriptor_size != 0) {
		return PE_EFI_MEMORY_MAP_BAD_SIZE;
	}

	for (uint32_t i = 0; i < map_size / descriptor_size; i++) {
		const EfiMemoryRange *range = (const EfiMemoryRange *)
		    ((const uint8_t *)map + (uint64_t)i * descriptor_size);
		PE_efi_memory_map_status status = PE_EFI_MEMORY_MAP_VALID;
		uint64_t first_page = range->PhysicalStart >> 12;
		uint64_t last_page;
		uint64_t range_bytes;

		if (bad_index != 0) {
			*bad_index = i;
		}
		if ((range->PhysicalStart & 4095) != 0 ||
		    (range->VirtualStart & 4095) != 0) {
			return PE_EFI_MEMORY_MAP_BAD_ALIGNMENT;
		}
		if (range->NumberOfPages == 0) {
			return PE_EFI_MEMORY_MAP_EMPTY_RANGE;
		}
		if (range->NumberOfPages > max_page ||
		    range->NumberOfPages - 1 > max_page - first_page ||
		    range->NumberOfPages - 1 > max_page - (range->VirtualStart >> 12)) {
			return PE_EFI_MEMORY_MAP_RANGE_OVERFLOW;
		}
		last_page = first_page + range->NumberOfPages - 1;
		range_bytes = range->NumberOfPages << 12;
		if ((range->Attribute & EFI_MEMORY_RUNTIME) != 0) {
			uint64_t runtime_virtual_start = range->VirtualStart;

			if (range->NumberOfPages > UINT32_MAX) {
				return PE_EFI_MEMORY_MAP_PAGE_OVERFLOW;
			}
			if (runtime_virtual_start < runtime_virtual_base) {
				runtime_virtual_start |= runtime_virtual_base;
			}
			if ((runtime_virtual_start & 4095) != 0) {
				return PE_EFI_MEMORY_MAP_BAD_ALIGNMENT;
			}
			if (range->PhysicalStart > UINT64_MAX - range_bytes ||
			    runtime_virtual_start > UINT64_MAX - range_bytes) {
				return PE_EFI_MEMORY_MAP_RANGE_OVERFLOW;
			}
			if (last_page > (runtime_physical_max >> 12)) {
				return PE_EFI_MEMORY_MAP_PAGE_OVERFLOW;
			}
		}
		if (i != 0 && first_page <= previous_last_page) {
			return PE_EFI_MEMORY_MAP_BAD_ORDER;
		}
		previous_last_page = last_page;
		if (UINT64_MAX - total_bytes < range_bytes) {
			return PE_EFI_MEMORY_MAP_RANGE_OVERFLOW;
		}
		total_bytes += range_bytes;

		switch (range->Type) {
		case kEfiLoaderCode:
		case kEfiLoaderData:
		case kEfiBootServicesCode:
		case kEfiBootServicesData:
		case kEfiConventionalMemory:
		case kEfiACPIMemoryNVS:
		case kEfiPalCode:
			if (last_page > UINT32_MAX || range->NumberOfPages > UINT32_MAX) {
				status = PE_EFI_MEMORY_MAP_PAGE_OVERFLOW;
			}
			break;
		default:
			break;
		}
		if (status != PE_EFI_MEMORY_MAP_VALID) {
			return status;
		}
	}
	if (bad_index != 0) {
		*bad_index = 0;
	}
	return PE_EFI_MEMORY_MAP_VALID;
}

#endif /* _PEXPERT_I386_EFI_MEMORY_MAP_H */

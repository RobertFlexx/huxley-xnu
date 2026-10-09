/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pexpert/i386/efi_memory_map.h>

static unsigned checks;
/* Matches the current x86 VM_MIN_KERNEL_ADDRESS, supplied by kernel callers. */
#define TEST_RUNTIME_BASE UINT64_C(0xffffff8000000000)
/* Matches the current x86 PG_FRAME page-table physical address mask. */
#define TEST_RUNTIME_PHYSICAL_MAX UINT64_C(0x000ffffffffff000)

static void
expect_ranges(const void *map, uint32_t size, uint32_t stride,
    PE_efi_memory_map_status expected, uint32_t expected_index)
{
	uint32_t index = UINT32_MAX;
	PE_efi_memory_map_status status = PE_efi_memory_map_validate_ranges(
		map, size, stride, TEST_RUNTIME_BASE, TEST_RUNTIME_PHYSICAL_MAX, &index);
	assert(status == expected && index == expected_index);
	assert(PE_efi_memory_map_validate_ranges(map, size, stride, TEST_RUNTIME_BASE,
	    TEST_RUNTIME_PHYSICAL_MAX, NULL) == expected);
	checks++;
}

static void
metadata_tests(void)
{
#define META(address, size, stride, version, expected) do { \
	assert(PE_efi_memory_map_validate_metadata(address, size, stride, version) == expected); \
	checks++; \
} while (0)
	META(0x1000, 40, 40, 1, PE_EFI_MEMORY_MAP_VALID);
	META(0xfffff000, 4096, 64, 1, PE_EFI_MEMORY_MAP_VALID);
	META(0xfffff000, 4160, 64, 1, PE_EFI_MEMORY_MAP_BAD_ADDRESS);
	META(0, 40, 40, 1, PE_EFI_MEMORY_MAP_BAD_ADDRESS);
	META(0x1001, 40, 40, 1, PE_EFI_MEMORY_MAP_BAD_ADDRESS);
	META(0x1000, 40, 0, 1, PE_EFI_MEMORY_MAP_BAD_STRIDE);
	META(0x1000, 40, 32, 1, PE_EFI_MEMORY_MAP_BAD_STRIDE);
	META(0x1000, 41, 41, 1, PE_EFI_MEMORY_MAP_BAD_STRIDE);
	META(0x1000, 0, 40, 1, PE_EFI_MEMORY_MAP_BAD_SIZE);
	META(0x1000, 41, 40, 1, PE_EFI_MEMORY_MAP_BAD_SIZE);
	META(0x1000, 40, 40, 0, PE_EFI_MEMORY_MAP_BAD_VERSION);
	META(0x1000, 40, 40, 2, PE_EFI_MEMORY_MAP_BAD_VERSION);
#undef META
}

static void
descriptor_tests(void)
{
	EfiMemoryRange map[2] = {
		{ .Type = kEfiConventionalMemory, .PhysicalStart = 0x1000, .NumberOfPages = 3 },
		{ .Type = kEfiRuntimeServicesCode, .PhysicalStart = 0x4000, .NumberOfPages = 1,
		  .Attribute = UINT64_C(1) << 63 }
	};
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	map[1].PhysicalStart = 0x3000;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_BAD_ORDER, 1);
	map[1].PhysicalStart = 0;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_BAD_ORDER, 1);
	map[1].PhysicalStart = 0x4001;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_BAD_ALIGNMENT, 1);
	map[1].PhysicalStart = 0x4000;
	map[1].VirtualStart = 1;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_BAD_ALIGNMENT, 1);
	map[1].VirtualStart = 0;
	map[1].NumberOfPages = 0;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_EMPTY_RANGE, 1);
	map[1].NumberOfPages = UINT64_MAX;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 1);
	map[1].PhysicalStart = UINT64_MAX & ~UINT64_C(4095);
	map[1].NumberOfPages = 2;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 1);
	map[1].Attribute = 0;
	map[1].NumberOfPages = 1; /* Legal final physical page, reserved from PMAP. */
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	map[1].PhysicalStart = 0x4000;
	map[1].VirtualStart = UINT64_MAX & ~UINT64_C(4095);
	map[1].NumberOfPages = 2;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 1);
	map[1].VirtualStart = 0;
	map[1].NumberOfPages = 1;
	map[1].PhysicalStart = ((uint64_t)UINT32_MAX + 1) << 12;
	map[1].Type = kEfiConventionalMemory;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 1);
	map[1].Type = 0x80000000; /* Unknown types remain non-allocatable. */
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	for (uint32_t type = kEfiLoaderCode; type <= kEfiBootServicesData; type++) {
		map[1].Type = type;
		expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 1);
	}
	map[1].PhysicalStart = (uint64_t)UINT32_MAX << 12;
	map[1].Type = kEfiConventionalMemory;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	map[1].NumberOfPages = 2;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 1);
	/* Hibernation also stores the usable range's page count in ppnum_t. */
	map[0].PhysicalStart = 0;
	map[0].NumberOfPages = (uint64_t)UINT32_MAX + 1;
	expect_ranges(map, sizeof(map[0]), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
	/* A map covering 2^64 bytes would overflow firmware accounting. */
	map[0].Type = kEfiReservedMemoryType;
	map[0].PhysicalStart = 0;
	map[0].NumberOfPages = UINT64_MAX >> 12;
	map[1].Type = kEfiReservedMemoryType;
	map[1].PhysicalStart = UINT64_MAX & ~UINT64_C(4095);
	map[1].NumberOfPages = 1;
	expect_ranges(map, sizeof(map), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 1);
}

static void
runtime_tests(void)
{
	EfiMemoryRange range = { .Type = kEfiRuntimeServicesData,
	    .PhysicalStart = 0x1000, .NumberOfPages = 1, .Attribute = UINT64_C(1) << 63 };
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	range.NumberOfPages = (uint64_t)UINT32_MAX + 1;
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
	range.Type = 0x80000000; /* The runtime attribute applies to every type. */
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
	range.NumberOfPages = 1;
	range.PhysicalStart = UINT64_MAX & ~UINT64_C(4095);
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 0);
	range.Attribute = 0; /* Last reserved page remains a valid non-runtime range. */
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	range.Attribute = UINT64_C(1) << 63;
	range.PhysicalStart = 0x1000;
	range.VirtualStart = UINT64_MAX & ~UINT64_C(4095);
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 0);
	range.VirtualStart = 0;
	range.NumberOfPages = UINT32_MAX;
	/* Effective kernel shadow address, not raw VirtualStart, would wrap. */
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 0);
	range.NumberOfPages = UINT64_C(1) << 27; /* Exactly fills -512 GiB to 2^64. */
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_RANGE_OVERFLOW, 0);
	range.NumberOfPages--;
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	range.VirtualStart = TEST_RUNTIME_BASE;
	range.NumberOfPages = 1;
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	range.PhysicalStart = TEST_RUNTIME_PHYSICAL_MAX;
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
	range.PhysicalStart += 4096;
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
	range.PhysicalStart = TEST_RUNTIME_PHYSICAL_MAX;
	range.NumberOfPages = 2;
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
	range.Attribute = 0; /* Wide non-runtime reserved metadata is not mapped. */
	expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
}

static void
hibernate_tests(void)
{
	/* Hibernation also saves these classes using uint32 base/page counts. */
	const uint32_t types[] = { kEfiACPIMemoryNVS, kEfiPalCode };
	for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		EfiMemoryRange range = { .Type = types[i],
		    .PhysicalStart = 0x1000, .NumberOfPages = 1 };
		expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
		range.PhysicalStart = (uint64_t)UINT32_MAX << 12;
		expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_VALID, 0);
		range.PhysicalStart = ((uint64_t)UINT32_MAX + 1) << 12;
		expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
		range.PhysicalStart = (uint64_t)UINT32_MAX << 12;
		range.NumberOfPages = 2;
		expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
		range.PhysicalStart = 0;
		range.NumberOfPages = (uint64_t)UINT32_MAX + 1;
		expect_ranges(&range, sizeof(range), 40, PE_EFI_MEMORY_MAP_PAGE_OVERFLOW, 0);
	}
}

static void
guard_tests(void)
{
	long page_size = sysconf(_SC_PAGESIZE);
	assert(page_size >= 4096);
	unsigned char *pages = mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(pages != MAP_FAILED);
	assert(mprotect(pages + page_size, (size_t)page_size, PROT_NONE) == 0);
	EfiMemoryRange range = { .Type = kEfiConventionalMemory,
	                        .PhysicalStart = 0x1000, .NumberOfPages = 1 };
	unsigned char *last = pages + page_size - sizeof(range);
	memcpy(last, &range, sizeof(range));
	expect_ranges(last, 40, 40, PE_EFI_MEMORY_MAP_VALID, 0);
	/* A too-small stride must fail without reading across the guard page. */
	expect_ranges(pages + page_size - 8, 8, 8, PE_EFI_MEMORY_MAP_BAD_STRIDE, 0);
	expect_ranges(pages + page_size, 0, 40, PE_EFI_MEMORY_MAP_BAD_SIZE, 0);
	expect_ranges(last, 41, 40, PE_EFI_MEMORY_MAP_BAD_SIZE, 0);
	expect_ranges(last + 1, 40, 40, PE_EFI_MEMORY_MAP_BAD_ALIGNMENT, 0);
	expect_ranges(NULL, 40, 40, PE_EFI_MEMORY_MAP_BAD_ADDRESS, 0);
	expect_ranges((void *)(UINTPTR_MAX - 7), 40, 40, PE_EFI_MEMORY_MAP_BAD_ADDRESS, 0);
	assert(munmap(pages, (size_t)page_size * 2) == 0);
}

static uint64_t random_state = UINT64_C(0x68786e7565666931);
static uint64_t
random_word(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return random_state;
}

static void
generated_tests(void)
{
	/* Exercise padded firmware stride, not just arrays of the known prefix. */
	_Alignas(8) unsigned char buffer[4 * 56];
	for (unsigned iteration = 0; iteration < 20000; iteration++) {
		uint64_t start = 0;
		memset(buffer, 0xa5, sizeof(buffer));
		for (unsigned i = 0; i < 4; i++) {
			EfiMemoryRange range = { .Type = kEfiConventionalMemory,
			                        .PhysicalStart = start,
			                        .NumberOfPages = (random_word() % 1024) + 1 };
			memcpy(buffer + i * 56, &range, sizeof(range));
			start += (range.NumberOfPages + (random_word() % 16)) * 4096;
		}
		expect_ranges(buffer, sizeof(buffer), 56, PE_EFI_MEMORY_MAP_VALID, 0);
		unsigned bad = (unsigned)(random_word() % 4);
		EfiMemoryRange range;
		memcpy(&range, buffer + bad * 56, sizeof(range));
		range.NumberOfPages = 0;
		memcpy(buffer + bad * 56, &range, sizeof(range));
		expect_ranges(buffer, sizeof(buffer), 56, PE_EFI_MEMORY_MAP_EMPTY_RANGE, bad);
		/* Arbitrary descriptor contents at a bounded, valid buffer length. */
		for (unsigned offset = 0; offset < sizeof(buffer); offset += 8) {
			uint64_t word = random_word();
			memcpy(buffer + offset, &word, sizeof(word));
		}
		uint32_t index = UINT32_MAX;
		PE_efi_memory_map_status status = PE_efi_memory_map_validate_ranges(
			buffer, sizeof(buffer), 56, TEST_RUNTIME_BASE, TEST_RUNTIME_PHYSICAL_MAX, &index);
		assert(status >= PE_EFI_MEMORY_MAP_VALID && status <= PE_EFI_MEMORY_MAP_PAGE_OVERFLOW);
		assert(index < 4);
		checks++;
	}
}

int
main(void)
{
	metadata_tests();
	descriptor_tests();
	runtime_tests();
	hibernate_tests();
	guard_tests();
	generated_tests();
	printf("PASS EFI memory-map validation: %u checks (seed 0x68786e7565666931)\n", checks);
	return 0;
}

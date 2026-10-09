/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define UINT64 uint64_t
#define UINT32 uint32_t
#define UINT16 uint16_t
#define UINT8 uint8_t
#include <acpi/Acpi.h>
#include <acpi/Acpi_v1.h>
#include <pexpert/i386/acpi_validate.h>

_Static_assert(sizeof(ACPI_TABLE_HEADER) == PE_ACPI_HEADER_SIZE, "ACPI header");
_Static_assert(sizeof(RSDP_DESCRIPTOR) == PE_ACPI_RSDP_V2_SIZE, "RSDP prefix");
_Static_assert(sizeof(MULTIPLE_APIC_TABLE) == PE_ACPI_MADT_SIZE, "MADT prefix");
_Static_assert(sizeof(MADT_PROCESSOR_APIC) == 8, "Local APIC prefix");

static unsigned checks;

static void
put32(uint8_t *bytes, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++) {
		bytes[i] = (uint8_t)(value >> (i * 8));
	}
}

static void
put64(uint8_t *bytes, uint64_t value)
{
	for (unsigned i = 0; i < 8; i++) {
		bytes[i] = (uint8_t)(value >> (i * 8));
	}
}

static void
checksum(uint8_t *bytes, uint32_t size, uint32_t offset)
{
	bytes[offset] = 0;
	uint8_t sum = 0;
	for (uint32_t i = 0; i < size; i++) {
		sum = (uint8_t)(sum + bytes[i]);
	}
	bytes[offset] = (uint8_t)(0 - sum);
}

static void
table(uint8_t *bytes, uint32_t size, const char *signature)
{
	memset(bytes, 0, size);
	memcpy(bytes, signature, 4);
	put32(bytes + 4, size);
	bytes[8] = 1;
	checksum(bytes, size, offsetof(ACPI_TABLE_HEADER, Checksum));
}

static void
expect_rsdp(const void *bytes, uint32_t size, PE_acpi_status expected,
    uint64_t expected_address, uint8_t expected_width)
{
	uint64_t address = UINT64_MAX;
	uint8_t width = UINT8_MAX;
	assert(PE_acpi_validate_rsdp(bytes, size, &address, &width) == expected);
	assert(address == expected_address && width == expected_width);
	assert(PE_acpi_validate_rsdp(bytes, size, NULL, NULL) == expected);
	checks++;
}

static void
expect_root(const void *bytes, uint32_t size, uint8_t width,
    PE_acpi_status expected, uint32_t expected_count)
{
	uint32_t count = UINT32_MAX;
	assert(PE_acpi_validate_root(bytes, size, width, &count) == expected);
	assert(count == expected_count);
	assert(PE_acpi_validate_root(bytes, size, width, NULL) == expected);
	checks++;
}

static void
expect_madt(const void *bytes, uint32_t size, PE_acpi_status expected,
    uint32_t expected_count, uint32_t expected_offset)
{
	uint32_t count = UINT32_MAX, bad = UINT32_MAX;
	assert(PE_acpi_count_enabled_cpus(bytes, size, &count, &bad) == expected);
	assert(count == expected_count && bad == expected_offset);
	assert(PE_acpi_count_enabled_cpus(bytes, size, NULL, NULL) == expected);
	checks++;
}

static void
rsdp_tests(void)
{
	uint8_t bytes[40] = {0};
	memcpy(bytes, RSDP_SIG, 8);
	put32(bytes + 16, 0x12345000);
	checksum(bytes, PE_ACPI_RSDP_V1_SIZE, 8);
	expect_rsdp(bytes, 20, PE_ACPI_VALID, 0x12345000, 4);
	expect_rsdp(bytes, 19, PE_ACPI_BAD_SIZE, 0, 0);
	bytes[9]++;
	expect_rsdp(bytes, 20, PE_ACPI_BAD_CHECKSUM, 0, 0);
	checksum(bytes, 20, 8);
	bytes[0] = 'X';
	expect_rsdp(bytes, 20, PE_ACPI_BAD_SIGNATURE, 0, 0);
	bytes[0] = 'R';
	bytes[15] = 1;
	checksum(bytes, 20, 8);
	expect_rsdp(bytes, 20, PE_ACPI_BAD_REVISION, 0, 0);
	bytes[15] = 2;
	put32(bytes + 20, 36);
	put64(bytes + 24, UINT64_C(0x123456789000));
	checksum(bytes, 20, 8);
	checksum(bytes, 36, 32);
	expect_rsdp(bytes, 36, PE_ACPI_VALID, UINT64_C(0x123456789000), 8);
	expect_rsdp(bytes, 35, PE_ACPI_BAD_SIZE, 0, 0);
	bytes[33]++;
	expect_rsdp(bytes, 36, PE_ACPI_BAD_CHECKSUM, 0, 0);
	checksum(bytes, 36, 32);
	put32(bytes + 20, 35);
	expect_rsdp(bytes, 36, PE_ACPI_BAD_LENGTH, 0, 0);
	put32(bytes + 20, 41);
	expect_rsdp(bytes, 40, PE_ACPI_BAD_LENGTH, 0, 0);
	put32(bytes + 20, PE_ACPI_MAX_RSDP_SIZE + 1);
	expect_rsdp(bytes, UINT32_MAX, PE_ACPI_BAD_LENGTH, 0, 0);
	put32(bytes + 20, 40); /* Future revision with a checksummed trailing prefix. */
	bytes[15] = 3;
	bytes[39] = 0xa5;
	checksum(bytes, 20, 8);
	checksum(bytes, 40, 32);
	expect_rsdp(bytes, 40, PE_ACPI_VALID, UINT64_C(0x123456789000), 8);
	put64(bytes + 24, 0);
	checksum(bytes, 40, 32);
	expect_rsdp(bytes, 40, PE_ACPI_BAD_ADDRESS, 0, 0);
	expect_rsdp(NULL, 40, PE_ACPI_BAD_ADDRESS, 0, 0);
	expect_rsdp((void *)(UINTPTR_MAX - 7), 40, PE_ACPI_BAD_ADDRESS, 0, 0);
}

static void
root_tests(void)
{
	uint8_t bytes[53]; /* Deliberately use an unaligned root below. */
	uint8_t *root = bytes + 1;
	table(root, 52, XSDT_SIG);
	put64(root + 36, UINT64_C(0x123456789000));
	put64(root + 44, 0x3000);
	checksum(root, 52, 9);
	expect_root(root, 52, 8, PE_ACPI_VALID, 2);
	assert(PE_acpi_root_entry(root, 0, 8) == UINT64_C(0x123456789000));
	assert(PE_acpi_root_entry(root, 1, 8) == 0x3000);
	checks += 2;
	expect_root(root, 51, 8, PE_ACPI_BAD_LENGTH, 0);
	expect_root(root, 52, 4, PE_ACPI_BAD_SIGNATURE, 0);
	expect_root(root, 52, 0, PE_ACPI_BAD_ENTRY, 0);
	put64(root + 44, 0);
	checksum(root, 52, 9);
	expect_root(root, 52, 8, PE_ACPI_BAD_ENTRY, 0);
	table(root, 45, XSDT_SIG);
	put64(root + 36, 0x2000);
	checksum(root, 45, 9);
	expect_root(root, 45, 8, PE_ACPI_BAD_ENTRY, 0);
	table(root, 44, RSDT_SIG);
	put32(root + 36, 0x1000);
	put32(root + 40, 0x2000);
	checksum(root, 44, 9);
	expect_root(root, 44, 4, PE_ACPI_VALID, 2);
	assert(PE_acpi_root_entry(root, 1, 4) == 0x2000);
	checks++;
	root[10]++;
	expect_root(root, 44, 4, PE_ACPI_BAD_CHECKSUM, 0);
	checksum(root, 44, 9);
	put32(root + 4, 35);
	expect_root(root, 44, 4, PE_ACPI_BAD_LENGTH, 0);
	put32(root + 4, PE_ACPI_MAX_TABLE_SIZE + 1);
	expect_root(root, UINT32_MAX, 4, PE_ACPI_BAD_LENGTH, 0);
	table(root, 36, RSDT_SIG);
	expect_root(root, 36, 4, PE_ACPI_VALID, 0);
	expect_root(NULL, 36, 4, PE_ACPI_BAD_ADDRESS, 0);
}

static void
madt_tests(void)
{
	uint8_t bytes[64];
	table(bytes, 64, ACPI_SIG_MADT);
	bytes[44] = APIC_PROCESSOR;
	bytes[45] = 8;
	bytes[48] = 1;
	bytes[52] = APIC_PROCESSOR;
	bytes[53] = 8;
	bytes[56] = 2; /* Online-capable/reserved flag does not mean enabled. */
	bytes[60] = 0xfe; /* Well-formed unknown type is skipped. */
	bytes[61] = 4;
	checksum(bytes, 64, 9);
	expect_madt(bytes, 64, PE_ACPI_VALID, 1, 0);
	bytes[56] = 1;
	checksum(bytes, 64, 9);
	expect_madt(bytes, 64, PE_ACPI_VALID, 2, 0);
	bytes[53] = 0; /* Previously an infinite loop after partial CPU counting. */
	checksum(bytes, 64, 9);
	expect_madt(bytes, 64, PE_ACPI_BAD_ENTRY, 0, 52);
	bytes[53] = 1;
	checksum(bytes, 64, 9);
	expect_madt(bytes, 64, PE_ACPI_BAD_ENTRY, 0, 52);
	bytes[53] = 7;
	checksum(bytes, 64, 9);
	expect_madt(bytes, 64, PE_ACPI_BAD_ENTRY, 0, 52);
	bytes[53] = 13;
	checksum(bytes, 64, 9);
	expect_madt(bytes, 64, PE_ACPI_BAD_ENTRY, 0, 52);
	table(bytes, 45, ACPI_SIG_MADT); /* Only one byte of the subtable header. */
	checksum(bytes, 45, 9);
	expect_madt(bytes, 45, PE_ACPI_BAD_ENTRY, 0, 44);
	table(bytes, 43, ACPI_SIG_MADT);
	expect_madt(bytes, 43, PE_ACPI_BAD_LENGTH, 0, 0);
	table(bytes, 44, ACPI_SIG_MADT);
	expect_madt(bytes, 44, PE_ACPI_VALID, 0, 0);
	bytes[10]++;
	expect_madt(bytes, 44, PE_ACPI_BAD_CHECKSUM, 0, 0);
	table(bytes, 44, "FACP");
	expect_madt(bytes, 44, PE_ACPI_BAD_SIGNATURE, 0, 0);
	expect_madt(NULL, 44, PE_ACPI_BAD_ADDRESS, 0, 0);
}

static void
guard_tests(void)
{
	long page = sysconf(_SC_PAGESIZE);
	assert(page >= 4096);
	uint8_t *buffer = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(buffer != MAP_FAILED);
	assert(mprotect(buffer + page, (size_t)page, PROT_NONE) == 0);
	uint8_t *rsdp = buffer + page - 20;
	memset(rsdp, 0, 20);
	memcpy(rsdp, RSDP_SIG, 8);
	put32(rsdp + 16, 0x1000);
	checksum(rsdp, 20, 8);
	expect_rsdp(rsdp, 20, PE_ACPI_VALID, 0x1000, 4);
	uint8_t *madt = buffer + page - 45;
	table(madt, 45, ACPI_SIG_MADT);
	expect_madt(madt, 45, PE_ACPI_BAD_ENTRY, 0, 44);
	expect_madt(buffer + page, 0, PE_ACPI_BAD_SIZE, 0, 0);
	assert(munmap(buffer, (size_t)page * 2) == 0);
}

static void
generated_tests(void)
{
	uint8_t bytes[44 + 32 * 8];
	uint64_t random = UINT64_C(0x68786e7561637069);
	for (unsigned iteration = 0; iteration < 10000; iteration++) {
		random ^= random << 13;
		random ^= random >> 7;
		random ^= random << 17;
		uint32_t entries = (uint32_t)(random % 32) + 1;
		uint32_t size = 44 + entries * 8;
		uint32_t count = 0;
		table(bytes, size, ACPI_SIG_MADT);
		for (uint32_t i = 0; i < entries; i++) {
			uint8_t *entry = bytes + 44 + i * 8;
			entry[0] = 0;
			entry[1] = 8;
			entry[2] = (uint8_t)i;
			entry[3] = (uint8_t)i;
			entry[4] = (uint8_t)((random >> i) & 1);
			count += entry[4];
		}
		checksum(bytes, size, 9);
		expect_madt(bytes, size, PE_ACPI_VALID, count, 0);
		uint32_t bad = (uint32_t)(random % entries);
		bytes[44 + bad * 8 + 1] = 0;
		checksum(bytes, size, 9);
		expect_madt(bytes, size, PE_ACPI_BAD_ENTRY, 0, 44 + bad * 8);
	}
}

int
main(void)
{
	rsdp_tests();
	root_tests();
	madt_tests();
	guard_tests();
	generated_tests();
	printf("PASS ACPI intake: %u checks (seed 0x68786e7561637069)\\n", checks);
	return 0;
}

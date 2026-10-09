/*
 * Copyright (c) 2026 Huxley contributors.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _PEXPERT_I386_ACPI_VALIDATE_H
#define _PEXPERT_I386_ACPI_VALIDATE_H

#include <stdint.h>

/* Internal firmware intake limits, not ACPI specification limits. */
#define PE_ACPI_MAX_TABLE_SIZE  (16u * 1024u * 1024u)
#define PE_ACPI_MAX_RSDP_SIZE   4096u
#define PE_ACPI_HEADER_SIZE     36u
#define PE_ACPI_RSDP_V1_SIZE    20u
#define PE_ACPI_RSDP_V2_SIZE    36u
#define PE_ACPI_MADT_SIZE       44u

typedef enum PE_acpi_status {
	PE_ACPI_VALID = 0,
	PE_ACPI_BAD_ADDRESS,
	PE_ACPI_BAD_SIZE,
	PE_ACPI_BAD_SIGNATURE,
	PE_ACPI_BAD_CHECKSUM,
	PE_ACPI_BAD_REVISION,
	PE_ACPI_BAD_LENGTH,
	PE_ACPI_BAD_ENTRY
} PE_acpi_status;

/* ACPI fields may be unaligned. Read their little-endian wire representation. */
static inline uint32_t
PE_acpi_read32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline uint64_t
PE_acpi_read64(const uint8_t *bytes)
{
	return PE_acpi_read32(bytes) | ((uint64_t)PE_acpi_read32(bytes + 4) << 32);
}

static inline int
PE_acpi_signature_matches(const uint8_t *bytes, const char *signature,
    uint32_t size)
{
	for (uint32_t i = 0; i < size; i++) {
		if (bytes[i] != (uint8_t)signature[i]) {
			return 0;
		}
	}
	return 1;
}

static inline uint8_t
PE_acpi_checksum(const uint8_t *bytes, uint32_t size)
{
	uint8_t sum = 0;
	for (uint32_t i = 0; i < size; i++) {
		sum = (uint8_t)(sum + bytes[i]);
	}
	return sum;
}

/* A size bound is not a mapping or firmware-authentication guarantee. */
static inline PE_acpi_status
PE_acpi_validate_buffer(const void *buffer, uint32_t readable_size,
    uint32_t minimum_size)
{
	if (buffer == 0 || (uintptr_t)buffer > UINTPTR_MAX - readable_size) {
		return PE_ACPI_BAD_ADDRESS;
	}
	if (readable_size < minimum_size) {
		return PE_ACPI_BAD_SIZE;
	}
	return PE_ACPI_VALID;
}

/* Legacy RSDP consumes exactly 20 bytes; revision >= 2 uses its own Length. */
static inline PE_acpi_status
PE_acpi_validate_rsdp(const void *buffer, uint32_t readable_size,
    uint64_t *root_address, uint8_t *entry_width)
{
	const uint8_t *bytes = (const uint8_t *)buffer;
	PE_acpi_status status = PE_acpi_validate_buffer(buffer, readable_size,
	    PE_ACPI_RSDP_V1_SIZE);
	uint64_t address;
	uint8_t width;

	if (root_address != 0) {
		*root_address = 0;
	}
	if (entry_width != 0) {
		*entry_width = 0;
	}
	if (status != PE_ACPI_VALID) {
		return status;
	}
	if (!PE_acpi_signature_matches(bytes, "RSD PTR ", 8)) {
		return PE_ACPI_BAD_SIGNATURE;
	}
	if (PE_acpi_checksum(bytes, PE_ACPI_RSDP_V1_SIZE) != 0) {
		return PE_ACPI_BAD_CHECKSUM;
	}
	if (bytes[15] == 0) {
		address = PE_acpi_read32(bytes + 16);
		width = 4;
	} else if (bytes[15] >= 2) {
		if (readable_size < PE_ACPI_RSDP_V2_SIZE) {
			return PE_ACPI_BAD_SIZE;
		}
		uint32_t length = PE_acpi_read32(bytes + 20);
		if (length < PE_ACPI_RSDP_V2_SIZE || length > readable_size ||
		    length > PE_ACPI_MAX_RSDP_SIZE) {
			return PE_ACPI_BAD_LENGTH;
		}
		if (PE_acpi_checksum(bytes, length) != 0) {
			return PE_ACPI_BAD_CHECKSUM;
		}
		address = PE_acpi_read64(bytes + 24);
		width = 8;
	} else {
		return PE_ACPI_BAD_REVISION;
	}
	if (address == 0) {
		return PE_ACPI_BAD_ADDRESS;
	}
	if (root_address != 0) {
		*root_address = address;
	}
	if (entry_width != 0) {
		*entry_width = width;
	}
	return PE_ACPI_VALID;
}

/* Callers keep the complete readable buffer immutable during consumption. */
static inline PE_acpi_status
PE_acpi_validate_sdt(const void *buffer, uint32_t readable_size,
    const char *signature, uint32_t *table_length)
{
	const uint8_t *bytes = (const uint8_t *)buffer;
	PE_acpi_status status = PE_acpi_validate_buffer(buffer, readable_size,
	    PE_ACPI_HEADER_SIZE);
	if (table_length != 0) {
		*table_length = 0;
	}
	if (status != PE_ACPI_VALID) {
		return status;
	}
	if (signature != 0 && !PE_acpi_signature_matches(bytes, signature, 4)) {
		return PE_ACPI_BAD_SIGNATURE;
	}
	uint32_t length = PE_acpi_read32(bytes + 4);
	if (length < PE_ACPI_HEADER_SIZE || length > readable_size ||
	    length > PE_ACPI_MAX_TABLE_SIZE) {
		return PE_ACPI_BAD_LENGTH;
	}
	if (PE_acpi_checksum(bytes, length) != 0) {
		return PE_ACPI_BAD_CHECKSUM;
	}
	if (table_length != 0) {
		*table_length = length;
	}
	return PE_ACPI_VALID;
}

/* Requires a validated root and index < the returned entry_count. */
static inline uint64_t
PE_acpi_root_entry(const void *buffer, uint32_t index, uint8_t entry_width)
{
	const uint8_t *entry = (const uint8_t *)buffer + PE_ACPI_HEADER_SIZE +
	    (uint64_t)index * entry_width;
	return entry_width == 8 ? PE_acpi_read64(entry) : PE_acpi_read32(entry);
}

static inline PE_acpi_status
PE_acpi_validate_root(const void *buffer, uint32_t readable_size,
    uint8_t entry_width, uint32_t *entry_count)
{
	uint32_t length = 0;
	if (entry_count != 0) {
		*entry_count = 0;
	}
	if (entry_width != 4 && entry_width != 8) {
		return PE_ACPI_BAD_ENTRY;
	}
	PE_acpi_status status = PE_acpi_validate_sdt(buffer, readable_size,
	    entry_width == 8 ? "XSDT" : "RSDT", &length);
	if (status != PE_ACPI_VALID) {
		return status;
	}
	if ((length - PE_ACPI_HEADER_SIZE) % entry_width != 0) {
		return PE_ACPI_BAD_ENTRY;
	}
	uint32_t count = (length - PE_ACPI_HEADER_SIZE) / entry_width;
	for (uint32_t i = 0; i < count; i++) {
		if (PE_acpi_root_entry(buffer, i, entry_width) == 0) {
			return PE_ACPI_BAD_ENTRY;
		}
	}
	if (entry_count != 0) {
		*entry_count = count;
	}
	return PE_ACPI_VALID;
}

/* Preserve the current kernel policy: enabled type-0 local APIC CPUs only. */
static inline PE_acpi_status
PE_acpi_count_enabled_cpus(const void *buffer, uint32_t readable_size,
    uint32_t *cpu_count, uint32_t *bad_offset)
{
	const uint8_t *bytes = (const uint8_t *)buffer;
	uint32_t length = 0;
	uint32_t count = 0;
	if (cpu_count != 0) {
		*cpu_count = 0;
	}
	if (bad_offset != 0) {
		*bad_offset = 0;
	}
	PE_acpi_status status = PE_acpi_validate_sdt(buffer, readable_size, "APIC", &length);
	if (status != PE_ACPI_VALID) {
		return status;
	}
	if (length < PE_ACPI_MADT_SIZE) {
		return PE_ACPI_BAD_LENGTH;
	}
	for (uint32_t offset = PE_ACPI_MADT_SIZE; offset < length;) {
		if (bad_offset != 0) {
			*bad_offset = offset;
		}
		if (length - offset < 2 || bytes[offset + 1] < 2 ||
		    bytes[offset + 1] > length - offset) {
			return PE_ACPI_BAD_ENTRY;
		}
		uint8_t entry_length = bytes[offset + 1];
		if (bytes[offset] == 0) {
			if (entry_length < 8) {
				return PE_ACPI_BAD_ENTRY;
			}
			if ((bytes[offset + 4] & 1) != 0) {
				count++;
			}
		}
		offset += entry_length;
	}
	if (cpu_count != 0) {
		*cpu_count = count;
	}
	if (bad_offset != 0) {
		*bad_offset = 0;
	}
	return PE_ACPI_VALID;
}

#endif /* _PEXPERT_I386_ACPI_VALIDATE_H */

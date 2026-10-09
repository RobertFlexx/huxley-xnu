/*
 * Copyright (c) 2026 Huxley contributors.
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _PEXPERT_I386_EFI_TABLES_H
#define _PEXPERT_I386_EFI_TABLES_H

#include <stddef.h>
#include <pexpert/i386/efi.h>

/* Existing BSD CRC implementation; do not duplicate it in firmware consumers. */
extern uint32_t crc32(uint32_t crc, const void *buffer, size_t size);

/* Explicit intake limits, not firmware allocation/readability guarantees. */
#define PE_EFI_MAX_HEADER_SIZE 4096u
#define PE_EFI_MAX_CONFIGURATION_TABLES 4096u

typedef enum PE_efi_table_status {
	PE_EFI_TABLE_VALID = 0,
	PE_EFI_TABLE_BAD_ADDRESS,
	PE_EFI_TABLE_BAD_SIZE,
	PE_EFI_TABLE_BAD_SIGNATURE,
	PE_EFI_TABLE_BAD_CHECKSUM,
	PE_EFI_TABLE_BAD_COUNT
} PE_efi_table_status;

/*
 * Caller supplies a readable, stable buffer. Header extensions up to the
 * intake limit participate in CRC verification. Compute the CRC with a zero
 * CRC32 field without writing into firmware-owned or read-only memory.
 */
static inline PE_efi_table_status
PE_efi_validate_system_table(const EFI_SYSTEM_TABLE_64 *table, uint32_t readable_size)
{
	const size_t crc_offset = offsetof(EFI_TABLE_HEADER, CRC32);
	const uint32_t zero = 0;
	uint32_t checksum, length;

	if (table == NULL || ((uintptr_t)table & 7) != 0 ||
	    (uintptr_t)table > UINTPTR_MAX - readable_size) {
		return PE_EFI_TABLE_BAD_ADDRESS;
	}
	if (readable_size < sizeof(*table)) {
		return PE_EFI_TABLE_BAD_SIZE;
	}
	if (table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE) {
		return PE_EFI_TABLE_BAD_SIGNATURE;
	}
	length = table->Hdr.HeaderSize;
	if (length < sizeof(*table) || length > readable_size ||
	    length > PE_EFI_MAX_HEADER_SIZE || table->Hdr.Reserved != 0) {
		return PE_EFI_TABLE_BAD_SIZE;
	}
	checksum = crc32(0, table, crc_offset);
	checksum = crc32(checksum, &zero, sizeof(zero));
	checksum = crc32(checksum, (const uint8_t *)table + crc_offset + sizeof(zero),
	    length - crc_offset - sizeof(zero));
	return checksum == table->Hdr.CRC32 ? PE_EFI_TABLE_VALID : PE_EFI_TABLE_BAD_CHECKSUM;
}

/*
 * Configuration entries use the inherited X64 UEFI layout. The owner maps the
 * complete array before calling; successful lookup returns its raw address,
 * leaving physical/legacy boot-address conversion to the architecture caller.
 * No match (or a null vendor address) returns VALID with *vendor_address == 0.
 */
static inline PE_efi_table_status
PE_efi_find_configuration_table(const EFI_CONFIGURATION_TABLE_64 *tables,
    uint64_t readable_size, uint64_t count, const EFI_GUID *guid,
    uint64_t *vendor_address)
{
	if (vendor_address != NULL) {
		*vendor_address = 0;
	}
	if (guid == NULL || vendor_address == NULL) {
		return PE_EFI_TABLE_BAD_ADDRESS;
	}
	if (count > PE_EFI_MAX_CONFIGURATION_TABLES) {
		return PE_EFI_TABLE_BAD_COUNT;
	}
	if (count == 0) {
		return PE_EFI_TABLE_VALID;
	}
	if (tables == NULL || ((uintptr_t)tables & 7) != 0 ||
	    count * sizeof(*tables) > UINTPTR_MAX - (uintptr_t)tables) {
		return PE_EFI_TABLE_BAD_ADDRESS;
	}
	if (readable_size < count * sizeof(*tables)) {
		return PE_EFI_TABLE_BAD_SIZE;
	}
	for (uint64_t i = 0; i < count; i++) {
		const uint8_t *left = (const uint8_t *)&tables[i].VendorGuid;
		const uint8_t *right = (const uint8_t *)guid;
		size_t byte;

		for (byte = 0; byte < sizeof(*guid); byte++) {
			if (left[byte] != right[byte]) {
				break;
			}
		}
		if (byte == sizeof(*guid)) {
			*vendor_address = tables[i].VendorTable;
			return PE_EFI_TABLE_VALID;
		}
	}
	return PE_EFI_TABLE_VALID;
}

#endif /* _PEXPERT_I386_EFI_TABLES_H */

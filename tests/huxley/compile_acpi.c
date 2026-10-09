/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#include <stdint.h>
#include <stddef.h>
#define UINT64 uint64_t
#define UINT32 uint32_t
#define UINT16 uint16_t
#define UINT8 uint8_t
#include <acpi/Acpi.h>
#include <acpi/Acpi_v1.h>
#include <pexpert/i386/acpi_validate.h>

_Static_assert(sizeof(ACPI_TABLE_HEADER) == PE_ACPI_HEADER_SIZE, "ACPI header");
_Static_assert(offsetof(ACPI_TABLE_HEADER, Length) == 4, "ACPI length offset");
_Static_assert(sizeof(RSDP_DESCRIPTOR) == PE_ACPI_RSDP_V2_SIZE, "RSDP prefix");
_Static_assert(offsetof(RSDP_DESCRIPTOR, Revision) == 15, "RSDP revision");
_Static_assert(offsetof(RSDP_DESCRIPTOR, XsdtPhysicalAddress) == 24, "XSDT address");
_Static_assert(sizeof(MULTIPLE_APIC_TABLE) == PE_ACPI_MADT_SIZE, "MADT prefix");
_Static_assert(sizeof(MADT_PROCESSOR_APIC) == 8, "Local APIC prefix");

PE_acpi_status
hxnu_check_rsdp(const void *buffer, uint32_t size, uint64_t *address, uint8_t *width)
{
	return PE_acpi_validate_rsdp(buffer, size, address, width);
}

PE_acpi_status
hxnu_check_root(const void *buffer, uint32_t size, uint8_t width, uint32_t *count)
{
	return PE_acpi_validate_root(buffer, size, width, count);
}

PE_acpi_status
hxnu_check_madt(const void *buffer, uint32_t size, uint32_t *count, uint32_t *bad)
{
	return PE_acpi_count_enabled_cpus(buffer, size, count, bad);
}

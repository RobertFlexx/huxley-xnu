/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
/*
 * QEMU/OVMF test application, not an HXNU loader. It never exits boot services,
 * loads a kernel, or claims memory for the OS. The real serial driver executes
 * port I/O; any unexpected MMIO access fails the test through isa-debug-exit.
 */
#include <stddef.h>
#include <pexpert/i386/efi.h>
#include <pexpert/i386/efi_memory_map.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <pexpert/protos.h>

/* X64 UEFI uses 64-bit UINTN/status, unlike legacy EFI_UINTN in efi.h. */
typedef uint64_t (*probe_get_memory_map_t)(uint64_t *, void *, uint64_t *,
    uint64_t *, uint32_t *);
typedef struct {
	EFI_TABLE_HEADER header;
	uint64_t raise_tpl, restore_tpl, allocate_pages, free_pages;
	probe_get_memory_map_t get_memory_map;
} probe_boot_services_prefix_t;
_Static_assert(offsetof(probe_boot_services_prefix_t, get_memory_map) == 56,
    "UEFI GetMemoryMap offset");
_Static_assert(offsetof(EFI_SYSTEM_TABLE_64, BootServices) == 96,
    "UEFI BootServices offset");

static _Alignas(8) uint8_t memory_map[65536];
static boot_args arguments = { .CommandLine = "legacy_uart=1 serialbaud=115200" };
PE_state_t PE_state;
#define PROBE_RUNTIME_BASE UINT64_C(0xffffff8000000000)
#define PROBE_RUNTIME_PHYSICAL_MAX UINT64_C(0x000ffffffffff000)

static __attribute__((noreturn)) void
finish(unsigned value)
{
	__asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"((uint16_t)0xf4));
	for (;;) {
		__asm__ volatile ("hlt");
	}
}

void
outb(unsigned short port, unsigned char value)
{
	__asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

unsigned char
inb(unsigned short port)
{
	unsigned char value;
	__asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

unsigned int ml_phys_read_word(vm_offset_t address) { (void)address; finish(0x11); }
unsigned int ml_phys_read_byte(vm_offset_t address) { (void)address; finish(0x11); }
void ml_phys_write_word(vm_offset_t address, unsigned int value) { (void)address; (void)value; finish(0x11); }
void ml_phys_write_byte(vm_offset_t address, unsigned int value) { (void)address; (void)value; finish(0x11); }

size_t
strlen(const char *value)
{
	size_t size = 0;
	while (value[size]) {
		size++;
	}
	return size;
}

int
strncmp(const char *left, const char *right, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		unsigned char l = (unsigned char)left[i], r = (unsigned char)right[i];
		if (l != r || l == 0) {
			return (int)l - (int)r;
		}
	}
	return 0;
}

void *
memcpy(void *destination, const void *source, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		((unsigned char *)destination)[i] = ((const unsigned char *)source)[i];
	}
	return destination;
}

/* Boot-argument parsing must not attempt firmware device-tree discovery. */
int IODTGetDefault(const char *name, void *value, unsigned size) { (void)name; (void)value; (void)size; finish(0x11); }
int SecureDTLookupEntry(const DTEntry start, const char *path, DTEntry *found) { (void)start; (void)path; (void)found; finish(0x11); }
int SecureDTGetProperty(const DTEntry entry, const char *name, const void **value, unsigned *size) { (void)entry; (void)name; (void)value; (void)size; finish(0x11); }

static void
emit(const char *message)
{
	while (*message) {
		serial_putc(*message++);
	}
}

static void
emit_hex(uint64_t value)
{
	const char digits[] = "0123456789abcdef";
	emit("0x");
	for (int shift = 60; shift >= 0; shift -= 4) {
		serial_putc(digits[(value >> shift) & 15]);
	}
}

static __attribute__((noreturn)) void
fail(const char *reason, uint64_t code)
{
	emit("HXNU firmware: FAIL ");
	emit(reason);
	emit(" ");
	emit_hex(code);
	emit("\r\n");
	finish(0x11);
}

/* Loader-side ordering normalization of a private copy, never kernel policy. */
static void
sort_map(uint32_t count, uint32_t stride)
{
	for (uint32_t i = 1; i < count; i++) {
		for (uint32_t j = i; j > 0; j--) {
			uint8_t *left = memory_map + (j - 1) * stride;
			uint8_t *right = left + stride;
			if (((EfiMemoryRange *)left)->PhysicalStart <=
			    ((EfiMemoryRange *)right)->PhysicalStart) {
				break;
			}
			for (uint32_t byte = 0; byte < stride; byte++) {
				uint8_t temporary = left[byte];
				left[byte] = right[byte];
				right[byte] = temporary;
			}
		}
	}
}

uint64_t
efi_main(void *image_handle, EFI_SYSTEM_TABLE_64 *table)
{
	(void)image_handle;
	if (!PE_init_boot_args(&arguments) || !serial_init()) {
		finish(0x11);
	}
	emit("HXNU firmware: serial PASS\r\n");
	if (table == 0 || table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
	    table->Hdr.HeaderSize < sizeof(EFI_SYSTEM_TABLE_64) || table->BootServices == 0) {
		fail("system table", 0);
	}
	probe_boot_services_prefix_t *services =
	    (probe_boot_services_prefix_t *)(uintptr_t)table->BootServices;
	if (services->header.Signature != UINT64_C(0x56524553544f4f42) ||
	    services->header.HeaderSize < sizeof(*services) || services->get_memory_map == 0) {
		fail("boot services", 0);
	}
	uint64_t size = sizeof(memory_map), key = 0, stride = 0;
	uint32_t version = 0;
	uint64_t efi_status = services->get_memory_map(&size, memory_map, &key, &stride, &version);
	if (efi_status != 0 || size > sizeof(memory_map) || stride > UINT32_MAX ||
	    (uintptr_t)memory_map > UINT32_MAX) {
		fail("GetMemoryMap", efi_status);
	}
	PE_efi_memory_map_status status = PE_efi_memory_map_validate_metadata(
		(uint32_t)(uintptr_t)memory_map, (uint32_t)size, (uint32_t)stride, version);
	if (status != PE_EFI_MEMORY_MAP_VALID) {
		fail("metadata", status);
	}
	uint32_t bad = 0;
	status = PE_efi_memory_map_validate_ranges(memory_map, (uint32_t)size,
	    (uint32_t)stride, PROBE_RUNTIME_BASE, PROBE_RUNTIME_PHYSICAL_MAX, &bad);
	if (status == PE_EFI_MEMORY_MAP_BAD_ORDER) {
		emit("HXNU firmware: normalizing memory-map ordering\r\n");
		sort_map((uint32_t)(size / stride), (uint32_t)stride);
		status = PE_efi_memory_map_validate_ranges(memory_map, (uint32_t)size,
		    (uint32_t)stride, PROBE_RUNTIME_BASE, PROBE_RUNTIME_PHYSICAL_MAX, &bad);
	}
	if (status != PE_EFI_MEMORY_MAP_VALID) {
		fail("ranges", status);
	}
	emit("HXNU firmware: memory-map PASS descriptors=");
	emit_hex(size / stride);
	emit(" stride=");
	emit_hex(stride);
	emit("\r\n");
	((EfiMemoryRange *)memory_map)->NumberOfPages = 0;
	status = PE_efi_memory_map_validate_ranges(memory_map, (uint32_t)size,
	    (uint32_t)stride, PROBE_RUNTIME_BASE, PROBE_RUNTIME_PHYSICAL_MAX, &bad);
	if (status != PE_EFI_MEMORY_MAP_EMPTY_RANGE || bad != 0) {
		fail("negative map check", status);
	}
	emit("HXNU firmware: reject-zero PASS\r\n");
	finish(0x10);
}

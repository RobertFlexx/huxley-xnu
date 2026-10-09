/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
/* Actual binder, existing generic boot parser and UART driver linked together. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pexpert/boot.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <pexpert/protos.h>

PE_state_t PE_state;
static unsigned char registers[8], divisor[2];
static unsigned writes;

void
outb(unsigned short port, unsigned char value)
{
	assert(port >= 0x3f8 && port <= 0x3ff);
	unsigned offset = port - 0x3f8;
	writes++;
	if (offset < 2 && (registers[3] & 0x80)) {
		divisor[offset] = value;
	} else {
		registers[offset] = value;
	}
}

unsigned char
inb(unsigned short port)
{
	assert(port >= 0x3f8 && port <= 0x3ff);
	return registers[port - 0x3f8];
}

/* Unexpected platform access is an assertion failure, never fake discovery. */
unsigned int ml_phys_read_word(vm_offset_t address) { (void)address; abort(); }
unsigned int ml_phys_read_byte(vm_offset_t address) { (void)address; abort(); }
void ml_phys_write_word(vm_offset_t address, unsigned int value) { (void)address; (void)value; abort(); }
void ml_phys_write_byte(vm_offset_t address, unsigned int value) { (void)address; (void)value; abort(); }
int IODTGetDefault(const char *name, void *value, unsigned size) { (void)name; (void)value; (void)size; abort(); }
int SecureDTLookupEntry(const DTEntry start, const char *path, DTEntry *found) { (void)start; (void)path; (void)found; abort(); }
int SecureDTGetProperty(const DTEntry entry, const char *name, const void **value, unsigned *size) { (void)entry; (void)name; (void)value; (void)size; abort(); }

int
main(void)
{
	boot_args args = { 0 }, rejected;
	assert(PE_boot_args()[0] == '\0');
	assert(!PE_init_boot_args(NULL) && PE_state.bootArgs == NULL);
	PE_state.initialized = FALSE;
	PE_state.deviceTreeHead = (void *)(uintptr_t)0x1000;
	assert(PE_init_boot_args(&args));
	assert(PE_state.bootArgs == &args && !PE_state.initialized);
	assert(PE_state.deviceTreeHead == (void *)(uintptr_t)0x1000);
	memset(&rejected, 0xa5, sizeof(rejected));
	assert(!PE_init_boot_args(&rejected) && PE_state.bootArgs == &args);
	assert(!PE_init_boot_args(NULL) && PE_state.bootArgs == &args);
	memset(args.CommandLine, 'x', sizeof(args.CommandLine));
	args.CommandLine[sizeof(args.CommandLine) - 1] = '\0';
	assert(PE_init_boot_args(&args));
	unsigned value = 0;
	assert(!PE_parse_boot_argn("legacy_uart", &value, sizeof(value)));
	strcpy(args.CommandLine, "legacy_uart=1 serialbaud=9600");
	assert(PE_init_boot_args(&args));
	assert(PE_parse_boot_argn("legacy_uart", &value, sizeof(value)) && value == 1);
	assert(PE_parse_boot_argn("serialbaud", &value, sizeof(value)) && value == 9600);
	assert(serial_init() == 1);
	assert(writes > 0 && divisor[0] == 12 && divisor[1] == 0);
	assert(!PE_state.initialized); /* Serial does not initialize platform/tree. */
	registers[5] = 0x20;
	serial_putc('S');
	assert(registers[0] == 'S');
	printf("PASS bounded boot-argument binding and real parser/serial integration\n");
	return 0;
}

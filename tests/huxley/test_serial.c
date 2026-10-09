/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
/* Executes the real pe_serial.c against a small 16550 register fixture. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <machine/machine_routines.h>
#include <pexpert/protos.h>

static unsigned char registers[8];
static unsigned char divisor_low, divisor_high, sent, received = 'R';
static unsigned mmio_reads, mmio_writes, port_accesses, tx_count;
static unsigned baud = 115200;
static int baud_present = 1, legacy_only = 1, legacy_present = 1;
static int mmio_present, pcie_present, skip_mmio;
static unsigned char mmio_scratch, pcie_scratch;

boolean_t
PE_parse_boot_argn(const char *name, void *value, int size)
{
	unsigned result;
	assert(size == sizeof(result));
	if (strcmp(name, "serialbaud") == 0 && baud_present) {
		result = baud;
	} else if (strcmp(name, "legacy_uart") == 0) {
		result = (unsigned)legacy_only;
	} else if (strcmp(name, "mmio_uart") == 0 && skip_mmio) {
		result = 0;
	} else {
		return FALSE;
	}
	memcpy(value, &result, sizeof(result));
	return TRUE;
}

void
outb(unsigned short port, unsigned char value)
{
	assert(port >= 0x3f8 && port < 0x400);
	port_accesses++;
	if (!legacy_present) {
		return;
	}
	unsigned offset = port - 0x3f8;
	if ((registers[3] & 0x80) && offset < 2) {
		if (offset == 0) {
			divisor_low = value;
		} else {
			divisor_high = value;
		}
	} else if (offset == 0) {
		sent = value;
		tx_count++;
	} else {
		registers[offset] = value;
	}
}

unsigned char
inb(unsigned short port)
{
	assert(port >= 0x3f8 && port < 0x400);
	port_accesses++;
	if (!legacy_present) {
		return 0xff;
	}
	unsigned offset = port - 0x3f8;
	return offset == 0 ? received : registers[offset];
}

unsigned int
ml_phys_read_word(vm_offset_t address)
{
	assert(!legacy_only);
	mmio_reads++;
	return mmio_present && (address == 0xfe03601c || address == 0xfe03401c) ?
	       mmio_scratch : 0;
}

void
ml_phys_write_word(vm_offset_t address, unsigned int value)
{
	assert(!legacy_only);
	mmio_writes++;
	if (address == 0xfe03601c || address == 0xfe03401c) {
		mmio_scratch = (unsigned char)value;
	}
}

unsigned int
ml_phys_read_byte(vm_offset_t address)
{
	assert(!legacy_only);
	mmio_reads++;
	return pcie_present && address == 0xfe410030 ? pcie_scratch : 0;
}

void
ml_phys_write_byte(vm_offset_t address, unsigned int value)
{
	assert(!legacy_only);
	mmio_writes++;
	if (address == 0xfe410030) {
		pcie_scratch = (unsigned char)value;
	}
}

int
main(int argc, char **argv)
{
	assert(argc == 2); /* Each case runs in a fresh process: driver state is private. */
	unsigned expected_divisor = 1;
	if (strcmp(argv[1], "legacy") == 0) {
		baud_present = 0;
	} else if (strcmp(argv[1], "zero") == 0) {
		baud = 0;
	} else if (strcmp(argv[1], "overflow") == 0) {
		baud = 1; /* Would truncate the 16-bit divisor latch. */
	} else if (strcmp(argv[1], "above-clock") == 0) {
		baud = 230400;
	} else if (strcmp(argv[1], "non-divisor") == 0) {
		baud = 110;
	} else if (strcmp(argv[1], "9600") == 0) {
		baud = 9600;
		expected_divisor = 12;
	} else if (strcmp(argv[1], "low-valid") == 0) {
		baud = 2;
		expected_divisor = 57600;
	} else if (strcmp(argv[1], "missing") == 0) {
		legacy_present = 0;
	} else if (strcmp(argv[1], "auto-legacy") == 0) {
		legacy_only = 0;
	} else if (strcmp(argv[1], "auto-mmio") == 0) {
		legacy_only = 0;
		mmio_present = 1;
	} else if (strcmp(argv[1], "auto-pcie") == 0) {
		legacy_only = legacy_present = 0;
		pcie_present = 1;
	} else if (strcmp(argv[1], "auto-missing") == 0) {
		legacy_only = legacy_present = 0;
	} else if (strcmp(argv[1], "existing-overrides") == 0) {
		legacy_only = 0;
		skip_mmio = 1;
	} else {
		fprintf(stderr, "Unknown serial test: %s\n", argv[1]);
		return 2;
	}
	registers[5] = 0x20;
	int result = serial_init();
	if (!legacy_present && !mmio_present && !pcie_present) {
		assert(result == 0);
		serial_putc('X');
		assert(tx_count == 0 && serial_getc() == -1);
	} else {
		assert(result == 1);
	}
	if (legacy_only || skip_mmio) {
		assert(mmio_reads == 0 && mmio_writes == 0);
	} else {
		assert(mmio_reads > 0 && mmio_writes > 0);
	}
	if (mmio_present) {
		assert(port_accesses == 0); /* Keep upstream MMIO preference. */
	} else if (legacy_present) {
		assert(((unsigned)divisor_high << 8 | divisor_low) == expected_divisor);
		assert(registers[3] == 3 && registers[1] == 0 && registers[2] == 1);
		serial_putc('X');
		assert(sent == 'X' && tx_count == 1);
		assert(serial_getc() == -1);
		registers[5] = 0x21;
		assert(serial_getc() == 'R');
		registers[5] = 0x25; /* RX error is discarded. */
		assert(serial_getc() == -1);
	}
	printf("PASS serial %s\n", argv[1]);
	return 0;
}

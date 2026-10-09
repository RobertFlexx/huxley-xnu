/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#ifndef HXNU_TEST_MACHINE_ROUTINES_H
#define HXNU_TEST_MACHINE_ROUTINES_H
#include <stdint.h>
#include <stdbool.h>
typedef uintptr_t vm_offset_t;
typedef int boolean_t;
#define TRUE 1
#define FALSE 0
#ifndef __unused
#define __unused __attribute__((unused))
#endif
/* Hardware access is replaced only in the host test translation unit. */
void outb(unsigned short port, unsigned char value);
unsigned char inb(unsigned short port);
unsigned int ml_phys_read_word(vm_offset_t address);
unsigned int ml_phys_read_byte(vm_offset_t address);
void ml_phys_write_word(vm_offset_t address, unsigned int value);
void ml_phys_write_byte(vm_offset_t address, unsigned int value);
#endif

/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#ifndef HXNU_TEST_PEXPERT_H
#define HXNU_TEST_PEXPERT_H
#include <machine/machine_routines.h>
#include <stddef.h>
/* Only fields touched by the compiled bootstrap code are modeled here. */
typedef struct {
	boolean_t initialized;
	void *deviceTreeHead;
	void *bootArgs;
} PE_state_t;
extern PE_state_t PE_state;
char *PE_boot_args(void);
boolean_t PE_init_boot_args(void *args);
boolean_t PE_parse_boot_argn(const char *name, void *value, int size);
boolean_t PE_parse_boot_arg_str(const char *name, char *value, int size);
size_t strlen(const char *value);
int strncmp(const char *left, const char *right, size_t size);
void *memcpy(void *destination, const void *source, size_t size);
#endif

/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#ifndef HXNU_TEST_DEVICE_TREE_H
#define HXNU_TEST_DEVICE_TREE_H
#include <stddef.h>
typedef void *DTEntry;
#define kSuccess 0
int SecureDTLookupEntry(const DTEntry start, const char *path, DTEntry *found);
int SecureDTGetProperty(const DTEntry entry, const char *name, const void **value, unsigned *size);
#endif

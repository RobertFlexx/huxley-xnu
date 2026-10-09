/*
 * Copyright (c) 2026 Huxley contributors.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _BSD_KERN_HUXLEY_INIT_H_
#define _BSD_KERN_HUXLEY_INIT_H_

#include <stddef.h>
#include <pexpert/pexpert.h>

/* Internal bootstrap policy; no syscall or executable-format ABI changes. */
typedef enum huxley_init_path_policy {
	HUXLEY_INIT_PATH_DEFAULT = 0,
	HUXLEY_INIT_PATH_OVERRIDE,
	HUXLEY_INIT_PATH_INVALID
} huxley_init_path_policy;

/* Return the length including NUL, or zero for an invalid/unbounded path. */
static inline size_t
huxley_init_path_length(const char *path, size_t maximum_length)
{
	if (path == NULL || maximum_length == 0 || path[0] != '/') {
		return 0;
	}
	for (size_t i = 0; i < maximum_length; i++) {
		if (path[i] == '\0') {
			return i + 1;
		}
	}
	return 0;
}

/*
 * The existing string parser terminates even a truncated value. Reserve one
 * extra byte beyond the accepted pathname size so a full/truncated value is
 * rejected rather than executing a different path. The caller owns this
 * buffer through its subsequent exec attempt; absent arguments leave inherited
 * launchd selection to the caller.
 */
static inline huxley_init_path_policy
huxley_init_path_select(char *path, int path_capacity)
{
	if (path == NULL || path_capacity <= 1) {
		return HUXLEY_INIT_PATH_INVALID;
	}
	if (!PE_parse_boot_arg_str("init_path", path, path_capacity)) {
		return HUXLEY_INIT_PATH_DEFAULT;
	}
	if (huxley_init_path_length(path, (size_t)path_capacity - 1) == 0) {
		return HUXLEY_INIT_PATH_INVALID;
	}
	return HUXLEY_INIT_PATH_OVERRIDE;
}

/*
 * Account for the path, optional "-s", alignment and three argv pointers before
 * any copyout. address_size is the kernel's user_addr_t width. This reserves a
 * conservative pointer array when a 64-bit kernel bootstraps a 32-bit process.
 * Return zero on invalid input or overflow.
 */
static inline size_t
huxley_init_scratch_size(size_t path_length, size_t address_size, int single_user)
{
	const size_t size_max = (size_t)-1;
	if (path_length == 0 || (address_size != 4 && address_size != 8)) {
		return 0;
	}
	const size_t mask = address_size - 1;
	const size_t tail_size = (3 + (single_user != 0)) * address_size;
	if (path_length > size_max - mask) {
		return 0;
	}
	size_t aligned_length = (path_length + mask) & ~mask;
	if (aligned_length > size_max - tail_size) {
		return 0;
	}
	return aligned_length + tail_size;
}

#endif /* _BSD_KERN_HUXLEY_INIT_H_ */

/*
 * Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
 * Copyright (c) 2026 Huxley contributors.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
#include <pexpert/pexpert.h>
#include <pexpert/boot.h>

/*
 * vstart uses the bootstrap identity mapping before PE_init_platform establishes
 * the final boot-argument mapping and device tree. Only bind the command-line
 * source here; leave all other platform state for PE_init_platform.
 */
boolean_t
PE_init_boot_args(void *args)
{
	boot_args *boot_args_ptr = (boot_args *)args;

	if (boot_args_ptr == 0) {
		return FALSE;
	}
	for (unsigned int i = 0; i < sizeof(boot_args_ptr->CommandLine); i++) {
		if (boot_args_ptr->CommandLine[i] == '\0') {
			PE_state.bootArgs = args;
			return TRUE;
		}
	}
	return FALSE;
}

char *
PE_boot_args(
	void)
{
	/* Early validation failures must not make diagnostic parsing read page zero. */
	if (PE_state.bootArgs == 0) {
		return "";
	}
	return ((boot_args *)PE_state.bootArgs)->CommandLine;
}

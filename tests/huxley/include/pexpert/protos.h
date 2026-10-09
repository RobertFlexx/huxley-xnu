/* SPDX-License-Identifier: BSD-2-Clause; Copyright (c) 2026 Huxley contributors. */
#ifndef HXNU_TEST_PEXPERT_PROTOS_H
#define HXNU_TEST_PEXPERT_PROTOS_H
int serial_init(void);
void serial_putc(char c);
int serial_getc(void);
void lpss_uart_enable(int on_off);
#endif

/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef PROFILE_SERIAL_H
#define PROFILE_SERIAL_H

#include <zephyr/device.h>
#include <zephyr/kernel.h>

/* Receive buffer handed to serial_cb() as user data. */
struct rx_buf_t {
	char *buf;
	size_t len;
	size_t pos;
};

/* Given by serial_cb() once the profile string in the buffer is terminated. */
extern struct k_sem profile_received;

void serial_cb(const struct device *dev, void *user_data);

#endif /* PROFILE_SERIAL_H */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "profile_serial.h"

#include <zephyr/drivers/uart.h>

K_SEM_DEFINE(profile_received, 0, 1);

void serial_cb(const struct device *dev, void *user_data)
{
	int rx_recv = 0;
	struct rx_buf_t *rx = (struct rx_buf_t *)user_data;
	char *rx_buf = rx->buf;
	size_t *rx_buf_pos = &rx->pos;

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		rx_recv = uart_fifo_read(dev, &rx_buf[*rx_buf_pos], 1);

		if ((rx_buf[*rx_buf_pos] == '\n') || (rx_buf[*rx_buf_pos] == '\r')) {
			rx_buf[*rx_buf_pos] = 0;
			k_sem_give(&profile_received);
			return;
		}

		*rx_buf_pos += rx_recv;
	}
}

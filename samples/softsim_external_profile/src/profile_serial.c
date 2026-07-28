/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "profile_serial.h"

#include <zephyr/drivers/uart.h>

K_SEM_DEFINE(profile_received, 0, 1);

void serial_cb(const struct device *dev, void *user_data)
{
	struct rx_buf_t *rx = (struct rx_buf_t *)user_data;
	char *rx_buf = rx->buf;
	size_t *rx_buf_pos = &rx->pos;
	uint8_t c;

	if (!uart_irq_update(dev)) {
		return;
	}

	/* Read before deciding what to do with the byte: uart_fifo_read() is what
	 * acknowledges the RX interrupt, so returning with a byte still pending would
	 * re-enter this ISR immediately and starve the thread waiting on the profile. */
	while (uart_fifo_read(dev, &c, 1) == 1) {
		/* Stop on the terminator, or once the buffer is full -- keeping room for
		 * the NUL -- rather than overflowing it. Mask RX before signalling so the
		 * buffer cannot be mutated while main() reads it. */
		if ((c == '\n') || (c == '\r') || (*rx_buf_pos == rx->len - 1)) {
			rx_buf[*rx_buf_pos] = 0;
			uart_irq_rx_disable(dev);
			k_sem_give(&profile_received);
			return;
		}

		rx_buf[(*rx_buf_pos)++] = c;
	}
}

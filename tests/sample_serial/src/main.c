/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Tests for the sample's serial profile receiver (profile_serial.c).
 *
 * serial_cb() runs in interrupt context and writes into a buffer that main()
 * reads -- the two defects it has had (unbounded write past the buffer, and
 * consuming bytes after signalling while the reader is already up) are both
 * boundary conditions of exactly the kind a flood of input provokes. Bytes
 * are injected through the emulated UART, which delivers them through the
 * real interrupt-driven UART API on the emulator's own thread, so the
 * ISR-vs-thread timing is genuine.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "profile_serial.h"

static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(euart0));

#define RX_CAPACITY 16

static char rx_backing[RX_CAPACITY];
static struct rx_buf_t rx;

/* The emulator delivers RX through its own work queue thread; after the
 * semaphore fires, give that thread time to go idle so asserts on pos/buf
 * are not racing a delivery still in flight. */
static void quiesce(void)
{
	k_msleep(20);
}

static void inject(const char *data, size_t len)
{
	zassert_equal(uart_emul_put_rx_data(uart_dev, (const uint8_t *)data, len), len,
		      "emulated RX fifo did not take all %zu bytes", len);
}

static void wait_for_line(void)
{
	zassert_ok(k_sem_take(&profile_received, K_SECONDS(1)),
		   "serial_cb never signalled a finished line");
	quiesce();
}

static void *suite_setup(void)
{
	zassert_true(device_is_ready(uart_dev), "uart-emul device not ready");
	uart_irq_callback_user_data_set(uart_dev, serial_cb, &rx);

	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(rx_backing, 0xaa, sizeof(rx_backing));
	rx.buf = rx_backing;
	rx.len = sizeof(rx_backing);
	rx.pos = 0;

	/* Fresh line state: drop a stale semaphore count, drop bytes a previous
	 * test left in the emulated fifo, and re-arm RX (serial_cb masks it when
	 * it terminates a line). */
	k_sem_take(&profile_received, K_NO_WAIT);
	uart_emul_flush_rx_data(uart_dev);
	uart_irq_rx_enable(uart_dev);
}

ZTEST_SUITE(softsim_sample_serial, NULL, suite_setup, test_before, NULL, NULL);

ZTEST(softsim_sample_serial, test_newline_terminates_the_line)
{
	inject("ABC\n", 4);
	wait_for_line();

	zassert_equal(rx.pos, 3);
	zassert_str_equal(rx.buf, "ABC", "buffer must hold the NUL-terminated line");
}

ZTEST(softsim_sample_serial, test_carriage_return_terminates_the_line)
{
	inject("AB\r", 3);
	wait_for_line();

	zassert_str_equal(rx.buf, "AB");
}

/*
 * The overflow guard: a line longer than the buffer must terminate at
 * capacity -- keeping room for the NUL -- instead of writing past the end.
 * The backing array is exactly rx.len bytes, so under --enable-asan an
 * out-of-bounds write fails the run rather than silently corrupting.
 */
ZTEST(softsim_sample_serial, test_flood_is_truncated_at_capacity)
{
	char flood[2 * RX_CAPACITY];

	memset(flood, 'x', sizeof(flood));
	inject(flood, sizeof(flood));
	wait_for_line();

	zassert_equal(rx.pos, RX_CAPACITY - 1, "line must stop with room for the NUL");
	zassert_equal(rx.buf[RX_CAPACITY - 1], 0, "buffer must be NUL-terminated");
	for (size_t i = 0; i < RX_CAPACITY - 1; i++) {
		zassert_equal(rx.buf[i], 'x', "byte %zu was not stored", i);
	}
}

/*
 * The data-race guard: once serial_cb signals a finished line it masks RX, so
 * bytes arriving while main() reads the buffer must neither be stored nor
 * advance the position.
 */
ZTEST(softsim_sample_serial, test_bytes_after_the_terminator_are_not_consumed)
{
	inject("AB\nCD", 5);
	wait_for_line();

	zassert_str_equal(rx.buf, "AB");
	zassert_equal(rx.pos, 2);

	inject("EF", 2);
	quiesce();

	zassert_equal(rx.pos, 2, "RX must stay masked after the terminator");
	zassert_str_equal(rx.buf, "AB", "buffer changed while the line was being read");
}

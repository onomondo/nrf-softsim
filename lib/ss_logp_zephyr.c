#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the public prototype for ss_logp so signatures match. */
#include <onomondo/softsim/log.h>

/* Use Zephyr logging to print onomondo-uicc log lines. This file owns the
 * softsim_uicc log module, independent of the nrf-softsim layer, so the two
 * can be enabled at different verbosity levels. The implementation is weak so
 * a stronger, package-provided logger can override it if present. */
#include <zephyr/logging/log.h>
#include <zephyr/sys/cbprintf.h>

LOG_MODULE_REGISTER(softsim_uicc, CONFIG_SOFTSIM_LIBS_LOG_LEVEL);

#if defined(CONFIG_SOFTSIM_LOG_IMMEDIATE_MODE)
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

/* Synchronous (immediate) logging blocks on every UART write; at the default
 * 115200 baud that stalls SIM init enough to prevent network registration.
 * Raise the console (DT chosen zephyr,console) to the configured baud at early
 * boot so the whole console stream is at the raised baud and modem/SIM timing
 * still meets its deadlines. Target-agnostic: works on any board with a chosen
 * console. Runs at PRE_KERNEL_2, which is strictly after the PRE_KERNEL_1
 * serial driver init and before the boot banner. */
static int ss_console_baud_for_immediate(void)
{
	const struct device *con = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	struct uart_config cfg;

	if (!device_is_ready(con) || uart_config_get(con, &cfg)) {
		return -ENODEV;
	}
	cfg.baudrate = CONFIG_SOFTSIM_LOG_IMMEDIATE_MODE_BAUD;
	return uart_configure(con, &cfg);
}
SYS_INIT(ss_console_baud_for_immediate, PRE_KERNEL_2, 0);
#endif /* CONFIG_SOFTSIM_LOG_IMMEDIATE_MODE */

struct ss_log_buf {
	char *buf;
	size_t size;
	size_t pos;
};

static int ss_log_buf_putc(int c, void *ctx)
{
	struct ss_log_buf *bc = ctx;
	if (bc->pos + 1 < bc->size) {
		bc->buf[bc->pos++] = (char)c;
		bc->buf[bc->pos] = '\0';
	}
	return c;
}

__attribute__((weak)) void ss_logp(uint32_t subsys, uint32_t level, const char *file, int line,
				   const char *format, ...)
{
	ARG_UNUSED(subsys);

	/* Format via Zephyr's cbprintf (CBPRINTF_COMPLETE + CBPRINTF_FULL_INTEGRAL,
	 * both default-on in NCS) rather than libc vsnprintf. Newlib-nano strips
	 * the %zu/%zx/%zd/%j/%t length modifiers to save code size, which silently
	 * corrupts SS_LOGP traces that pass size_t. cbvprintf handles them. */
	char buf[256];
	struct ss_log_buf bc = {.buf = buf, .size = sizeof(buf), .pos = 0};
	buf[0] = '\0';
	va_list ap;
	va_start(ap, format);
	cbvprintf(ss_log_buf_putc, &bc, format, ap);
	va_end(ap);

	/* Prepend file:line for easier tracing */
	char out[320];
	int n = snprintf(out, sizeof(out), "%s:%d: %s", file ? file : "", line, buf);
	if (n < 0) {
		strncpy(out, buf, sizeof(out));
		out[sizeof(out) - 1] = '\0';
	}

	switch (level) {
	case LERROR:
		LOG_ERR("%s", out);
		break;
	case LINFO:
		LOG_INF("%s", out);
		break;
	case LDEBUG:
		LOG_DBG("%s", out);
		break;
	default:
		LOG_WRN("%s", out);
		break;
	}
}

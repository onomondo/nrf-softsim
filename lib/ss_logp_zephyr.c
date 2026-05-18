#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the public prototype for ss_logp so signatures match. */
#include <onomondo/softsim/log.h>

/* Use Zephyr logging to print onomondo-uicc log lines. We declare the
 * Zephyr log module that other files register and use it here. The
 * implementation is weak so a stronger, package-provided logger can
 * override it if present. */
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_LOG_LEVEL);

__attribute__((weak)) void ss_logp(uint32_t subsys, uint32_t level, const char *file, int line,
				   const char *format, ...)
{
	ARG_UNUSED(subsys);

	/* Format the incoming varargs into a temporary buffer, then forward to
	 * Zephyr's logging API so logs are visible via the configured backend.
	 */
	char buf[256];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
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

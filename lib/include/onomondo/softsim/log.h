#pragma once

#include <stdio.h>
#include <stdint.h>

#define SS_LOGP(subsys, level, fmt, args...) \
	ss_logp(subsys, level, __FILE__, __LINE__, fmt, ## args)

void ss_logp(uint32_t subsys, uint32_t level, const char *file, int line, const char *format, ...)
	__attribute__ ((format (printf, 5, 6)));


enum log_subsys {
	SBTLV,
	SCTLV,
	SVPCD,
	SIFACE,
	SUICC,
	SCMD,
	SLCHAN,
	SFS,
	SSTORAGE,
	SACCESS,
	SADMIN,
	SSFI,
	SDFNAME,
	SFILE,
	SPIN,
	SAUTH,
	SPROACT,
	STLV8,
	SSMS,
	SREMOTECMD,
	SREFRESH,
	_NUM_LOG_SUBSYS
};

enum log_level {
	LERROR,
	LINFO,
	LDEBUG,
	_NUM_LOG_LEVEL
};

#include <stdarg.h>
#include <stdio.h>
#include "log.h"

FILE *fLog = NULL;
int LogMask = 0x1F;

void Log(int level, const char *fmt, ...)
{
	va_list args;
	if (!fLog || !(level & LogMask)) return;
	va_start(args, fmt);
	vfprintf(fLog, fmt, args);
	va_end(args);
	fflush(fLog);
}

void LogInit(config_t *cnf)
{
	long tmp;
	char *stmp;
	
	fLog = stdout;
	if (!cnf) return;
//		Mask
	if (config_lookup_int(cnf, "Log.Mask", &tmp)) LogMask = tmp;
//		File
	LogClose();	
	if (config_lookup_string(cnf, "Log.File", (const char **) &stmp) && stmp[0]) {
		fLog = fopen(stmp, "at");
		if (!fLog) fLog = stdout; 
	} else {
		fLog = stdout;
	}
}

void LogClose(void)
{
	if (fLog && fLog != stdout) fclose(fLog);
}


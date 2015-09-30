#ifndef LOG_H
#define LOG_H

#include <libconfig.h>

#define FATAL	1
#define ERROR	2
#define WARN	4
#define INFO	8
#define DEBUG	16

#ifdef __cplusplus
extern "C" {
#endif

void Log(int level, const char * fmt, ...);
void LogInit(config_t *cnf);
void LogClose(void);

#ifdef __cplusplus
}
#endif

#endif

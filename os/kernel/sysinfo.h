#ifndef SYSINFO_H
#define SYSINFO_H

#include "types.h"

#define OS_NAME    "NovaOS"
#define OS_VERSION "0.1"
#define OS_CODENAME "Aurora"

extern uint32_t sys_mem_kb;       /* detected RAM */

void cpu_brand(char *buf, size_t n);
void uptime_str(char *buf, size_t n);

#endif

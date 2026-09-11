/*
** webcandc: force-included ahead of every translation unit (-include prelude.h).
**
** Pulls in the C/C++ runtime before any 1995 header gets a chance to define
** _WIN32, bool shims, or min/max macros, then supplies the Watcom C library
** extensions the code expects (stricmp, strupr, itoa, _splitpath, ...).
*/
#ifndef WEBCANDC_PRELUDE_H
#define WEBCANDC_PRELUDE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/timeb.h>

#ifdef __cplusplus
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <climits>
#include <ctime>
#include <cmath>
#include <algorithm>
#endif

#include "watcomlib.h"

/* Westwood's MISC.H declares its own random(unsigned long); keep it apart
   from POSIX random(), which <stdlib.h> above has already declared. */
#define random WW_random

/* The 1995 code selects its Win95 paths with _WIN32. Defined only now, after
   the C++ runtime headers above, whose configuration keys off _WIN32 too. */
#ifndef _WIN32
#define _WIN32 1
#endif

#include <arpa/inet.h>	/* htons/ntohl, which Win32 got from <winsock.h> */

#endif

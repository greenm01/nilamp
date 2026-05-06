// SPDX-License-Identifier: MIT
#ifndef NILAMP_COMPAT_H
#define NILAMP_COMPAT_H

#if defined(_WIN32)
#include <string.h>
#define nilamp_stricmp _stricmp
#else
#include <strings.h>
#define nilamp_stricmp strcasecmp
#endif

#endif

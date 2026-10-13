#include <stddef.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <sys/stat.h>
#define S_ISREG(mode) (((mode)&_S_IFMT)==_S_IFREG)
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif
#define strdup _strdup

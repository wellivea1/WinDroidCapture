#ifndef AWM_SCRCPY_UNISTD_H
#define AWM_SCRCPY_UNISTD_H
#include <io.h>
#include <stddef.h>
#include <stdint.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define isatty _isatty
#define fileno _fileno
#endif

#include "error.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void error_exit(tErrorCode code, const char* fmt, ...)
{
    va_list args;
    fprintf(stderr, "Error: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(code);
}

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "error.h"

void error_exit(tErrorCode code, const char *fmt, ...){
    va_list args;
    fprintf(stderr, "Error: ");
    va_start(args, fmt);
    vfprintf(stderr,fmt,args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(code);
}

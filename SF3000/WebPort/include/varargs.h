#ifndef SF_3DO_COMPAT_VARARGS_H
#define SF_3DO_COMPAT_VARARGS_H
#include <stdarg.h>
#include "../sf_3do_compat.h"
int sf_3do_vsprintf(char *destination, const char *format, va_list arguments);
#define vsprintf(destination, format, arguments) \
    sf_3do_vsprintf((destination), (format), (arguments));
#endif

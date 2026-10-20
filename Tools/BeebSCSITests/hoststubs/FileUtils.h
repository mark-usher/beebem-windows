#pragma once
#include <stdio.h>
#include <stdarg.h>
inline void MakeFileName(char *Path, size_t Size, const char *Dir, const char *Name, ...)
{ char n[100]; va_list a; va_start(a, Name); vsnprintf(n, sizeof n, Name, a); va_end(a); snprintf(Path, Size, "%s/%s", Dir, n); }

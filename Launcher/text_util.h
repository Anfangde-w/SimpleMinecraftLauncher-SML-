#ifndef TEXT_UTIL_H
#define TEXT_UTIL_H

#include <stddef.h>

// UTF-8 <-> UTF-16 转换，返回的缓冲区由调用方 free
wchar_t* utf8_to_wide(const char* s);
char* wide_to_utf8(const wchar_t* s);

#endif // TEXT_UTIL_H

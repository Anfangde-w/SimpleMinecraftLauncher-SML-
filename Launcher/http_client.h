#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

typedef void (*HttpProgressFn)(void* user, long long done, long long total);

// 最近一次失败的原因
const char* http_last_error(void);

// 下载到内存（以 \0 结尾），成功后 *out_text 由调用方 free
int http_get_text(const char* url, char** out_text);

// 下载到文件，边写边回调进度；结束后校验大小和 SHA1，失败会删除文件
int http_download_file(const char* url, const char* dest_path,
                       long long expected_size, const char* expected_sha1,
                       HttpProgressFn cb, void* user);

#endif // HTTP_CLIENT_H

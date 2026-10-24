#ifndef SHA1_UTIL_H
#define SHA1_UTIL_H

// 计算文件 SHA1，返回 41 字节十六进制字符串；失败返回 0
int sha1_file_hex(const char* path, char out_hex[41]);

// 校验文件大小和 SHA1；sha1_hex 为空时只校验大小（size<=0 时只看文件是否存在）
int file_matches_sha1(const char* path, const char* sha1_hex, long long expected_size);

#endif // SHA1_UTIL_H

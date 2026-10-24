#ifndef FILE_UTIL_H
#define FILE_UTIL_H

char* read_file(const char* filename);
void fix_path(char* str);

// UTF-8 路径的目录操作（下载模块用）
int ensure_dir_utf8(const char* path);
int file_exists_utf8(const char* path);
long long file_size_utf8(const char* path);   // 失败返回 -1

#endif // FILE_UTIL_H

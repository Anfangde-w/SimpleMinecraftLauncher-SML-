#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file_util.h"
#include "text_util.h"

// ================= 2. 工具函数 =================
char* read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long length = ftell(f);
    if (length < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    char* data = (char*)malloc((size_t)length + 1);
    if (!data) { fclose(f); return NULL; }

    size_t got = fread(data, 1, (size_t)length, f);
    fclose(f);

    data[got] = '\0';
    return data;
}

void fix_path(char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\\') str[i] = '/';
    }
}

int file_exists_utf8(const char* path) {
    wchar_t* wpath;
    DWORD attr;

    if (!path) return 0;
    wpath = utf8_to_wide(path);
    if (!wpath) return 0;
    attr = GetFileAttributesW(wpath);
    free(wpath);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

long long file_size_utf8(const char* path) {
    wchar_t* wpath;
    HANDLE file;
    LARGE_INTEGER size;

    if (!path) return -1;
    wpath = utf8_to_wide(path);
    if (!wpath) return -1;
    file = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (file == INVALID_HANDLE_VALUE) return -1;
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return -1;
    }
    CloseHandle(file);
    return size.QuadPart;
}

int ensure_dir_utf8(const char* path) {
    wchar_t* wpath;
    wchar_t tmp[2048];
    wchar_t* p;
    DWORD attr;

    if (!path || !path[0]) return 0;
    wpath = utf8_to_wide(path);
    if (!wpath) return 0;
    wcsncpy(tmp, wpath, 2047);
    tmp[2047] = L'\0';
    free(wpath);

    for (p = tmp; *p; p++) {
        if ((*p == L'\\' || *p == L'/') && p > tmp + 2) {
            wchar_t saved = *p;
            *p = L'\0';
            CreateDirectoryW(tmp, NULL);
            *p = saved;
        }
    }
    CreateDirectoryW(tmp, NULL);

    attr = GetFileAttributesW(tmp);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

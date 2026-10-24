#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file_util.h"

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

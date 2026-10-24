#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>
#include "cJSON.h"
#include "launcher_config.h"
#include "file_util.h"
#include "classpath.h"

#define CLASSPATH_MAX 65536

// ================= 3. 核心逻辑模块 =================

// 判断一条 rule 是否适用于当前环境（Windows x64，未启用任何 feature）
static bool rule_matches(cJSON* rule) {
    cJSON* os = cJSON_GetObjectItemCaseSensitive(rule, "os");
    if (os) {
        cJSON* name = cJSON_GetObjectItemCaseSensitive(os, "name");
        cJSON* arch = cJSON_GetObjectItemCaseSensitive(os, "arch");
        if (cJSON_IsString(name) && strcmp(name->valuestring, "windows") != 0) return false;
        if (cJSON_IsString(arch) && strcmp(arch->valuestring, "x86_64") != 0) return false;
    }
    // 带 features 的规则一律不匹配（本启动器不支持 demo / 自定义分辨率等）
    if (cJSON_GetObjectItemCaseSensitive(rule, "features")) return false;
    return true;
}

// 按 version json 的 rules 判断这个库要不要用：最后一条匹配的规则生效，默认不允许
static bool rules_allow(cJSON* lib) {
    cJSON* rules = cJSON_GetObjectItemCaseSensitive(lib, "rules");
    if (!cJSON_IsArray(rules)) return true;

    bool allow = false;
    cJSON* rule = NULL;
    cJSON_ArrayForEach(rule, rules) {
        if (!rule_matches(rule)) continue;
        cJSON* action = cJSON_GetObjectItemCaseSensitive(rule, "action");
        allow = cJSON_IsString(action) && strcmp(action->valuestring, "allow") == 0;
    }
    return allow;
}

// 1.21.3 里 natives 是名字带 :natives-windows 的独立库（x64/arm64/x86 三种）
static bool is_native_any(const char* name) {
    return name && strstr(name, ":natives-windows") != NULL;
}

// 只要 x64 那一份，排除 :natives-windows-arm64 / :natives-windows-x86
static bool is_native_x64(const char* name) {
    const char* suffix = ":natives-windows";
    size_t len, suffix_len;
    if (!name) return false;
    len = strlen(name);
    suffix_len = strlen(suffix);
    return len >= suffix_len && _stricmp(name + len - suffix_len, suffix) == 0;
}

static void append_str(char* buf, size_t cap, const char* s) {
    size_t len = strlen(buf);
    if (len + 1 >= cap) return;
    strncat(buf, s, cap - len - 1);
}

// native jar 里的 DLL 在 windows/x64/org/lwjgl/... 下面，递归找出来摊平到 natives 根目录
static void flatten_dlls(const char* dir, const char* dest_root) {
    char pattern[1024];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        char full[1024];
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            flatten_dlls(full, dest_root);
        }
        else {
            size_t n = strlen(fd.cFileName);
            if (n > 4 && _stricmp(fd.cFileName + n - 4, ".dll") == 0) {
                char dest[1024];
                snprintf(dest, sizeof(dest), "%s\\%s", dest_root, fd.cFileName);
                if (_stricmp(full, dest) != 0) {
                    // 覆盖复制：目录里可能残留上次的 arm64/x86 同名 DLL，必须用 x64 版覆盖掉
                    DWORD attr = GetFileAttributesA(dest);
                    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) {
                        SetFileAttributesA(dest, attr & ~FILE_ATTRIBUTE_READONLY);
                    }
                    if (!CopyFileA(full, dest, FALSE)) {
                        MoveFileExA(full, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
                    }
                }
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

// 模块 A：处理 Libraries 和 Natives，生成 Classpath
bool build_classpath_and_natives(LauncherConfig* config, cJSON* root, char* out_classpath, char* out_natives_dir) {
    static char classpath[CLASSPATH_MAX];
    char natives_win[1024];
    char natives_x64[1024];
    char extract_cmd[2048];
    char jar_full_path[1024];
    char mkdir_cmd[1024];
    char version_jar[1024];

    classpath[0] = '\0';

    // 创建 natives 文件夹
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "if not exist \"%s\\natives\" mkdir \"%s\\natives\"", config->mc_dir, config->mc_dir);
    system(mkdir_cmd);

    snprintf(natives_win, sizeof(natives_win), "%s\\natives", config->mc_dir);
    snprintf(out_natives_dir, 512, "%s", natives_win);
    fix_path(out_natives_dir); // 转为 / 方便 Java 读取

    cJSON* libraries = cJSON_GetObjectItemCaseSensitive(root, "libraries");
    if (cJSON_IsArray(libraries)) {
        cJSON* lib = NULL;
        cJSON_ArrayForEach(lib, libraries) {
            // 1. 按 rules 过滤掉其他平台的库
            if (!rules_allow(lib)) continue;

            cJSON* downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");
            if (!downloads) continue;

            cJSON* artifact = cJSON_GetObjectItemCaseSensitive(downloads, "artifact");
            cJSON* path = artifact ? cJSON_GetObjectItemCaseSensitive(artifact, "path") : NULL;
            if (!cJSON_IsString(path)) continue;

            const char* name = NULL;
            cJSON* name_obj = cJSON_GetObjectItemCaseSensitive(lib, "name");
            if (cJSON_IsString(name_obj)) name = name_obj->valuestring;

            snprintf(jar_full_path, sizeof(jar_full_path), "%s\\libraries\\%s", config->mc_dir, path->valuestring);

            // 2. natives 库：解压 DLL，不进 classpath
            if (is_native_any(name)) {
                if (is_native_x64(name)) {
                    snprintf(extract_cmd, sizeof(extract_cmd), "tar -xf \"%s\" -C \"%s\\natives\" >nul 2>&1", jar_full_path, config->mc_dir);
                    system(extract_cmd);
                }
                continue;
            }

            // 3. 普通库：拼进 classpath
            if (GetFileAttributesA(jar_full_path) == INVALID_FILE_ATTRIBUTES) {
                printf("警告：缺少库 %s\n", path->valuestring);
                continue;
            }
            append_str(classpath, sizeof(classpath), config->mc_dir);
            append_str(classpath, sizeof(classpath), "\\libraries\\");
            append_str(classpath, sizeof(classpath), path->valuestring);
            append_str(classpath, sizeof(classpath), ";");
        }
    }

    // 拼上游戏本体 jar
    snprintf(version_jar, sizeof(version_jar), "%s\\versions\\%s\\%s.jar", config->mc_dir, config->version, config->version);
    snprintf(out_classpath, CLASSPATH_MAX, "%s;%s", version_jar, classpath);
    fix_path(out_classpath);

    // 解压完把 x64 的 DLL 摊平，java.library.path 指向 natives 根目录就能直接加载
    snprintf(natives_x64, sizeof(natives_x64), "%s\\windows\\x64", natives_win);
    if (GetFileAttributesA(natives_x64) != INVALID_FILE_ATTRIBUTES) {
        flatten_dlls(natives_x64, natives_win);
    }
    else {
        flatten_dlls(natives_win, natives_win);
    }

    return true; // 这里可以增加校验逻辑
}

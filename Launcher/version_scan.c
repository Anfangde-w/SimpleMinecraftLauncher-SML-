#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "launcher_config.h"
#include "file_util.h"
#include "platform_rules.h"
#include "version_scan.h"

static void copy_field(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static const char* json_string(cJSON* obj, const char* key) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

// 统计当前平台需要的库里缺了多少个文件
static int count_missing_libs(const char* mc_dir, cJSON* root) {
    cJSON* libraries = cJSON_GetObjectItemCaseSensitive(root, "libraries");
    cJSON* lib = NULL;
    int missing = 0;

    if (!cJSON_IsArray(libraries)) return 0;

    cJSON_ArrayForEach(lib, libraries) {
        cJSON* downloads;
        cJSON* artifact;
        cJSON* path;
        char full[1024];

        if (!rules_allow(lib)) continue;

        downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");
        artifact = downloads ? cJSON_GetObjectItemCaseSensitive(downloads, "artifact") : NULL;
        path = artifact ? cJSON_GetObjectItemCaseSensitive(artifact, "path") : NULL;
        if (!cJSON_IsString(path)) continue;

        snprintf(full, sizeof(full), "%s\\libraries\\%s", mc_dir, path->valuestring);
        if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) missing++;
    }
    return missing;
}

static int type_rank(const char* type) {
    if (strcmp(type, "release") == 0) return 0;
    if (strcmp(type, "snapshot") == 0) return 1;
    if (strcmp(type, "old_beta") == 0) return 2;
    if (strcmp(type, "old_alpha") == 0) return 3;
    return 4;
}

// 只解析 1.21.3 这种纯数字点分版本；快照名返回 0，走字符串比较
static int parse_version(const char* s, int* parts, int max) {
    int n = 0;
    const char* p = s;

    while (*p) {
        int v = 0;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            if (v > 1000000) return 0;
            p++;
        }
        if (n >= max) return 0;
        parts[n++] = v;
        if (*p == '.') {
            p++;
            if (!*p) return 0;
        }
        else if (*p) {
            return 0;
        }
    }
    return n;
}

// 返回负数表示 a 比 b 新（用于降序排序）
static int compare_version_desc(const char* a, const char* b) {
    int pa[8] = { 0 }, pb[8] = { 0 };
    int na = parse_version(a, pa, 8);
    int nb = parse_version(b, pb, 8);
    int i;

    if (na > 0 && nb > 0) {
        for (i = 0; i < 8; i++) {
            if (pa[i] != pb[i]) return pb[i] - pa[i];
        }
        return 0;
    }
    return -strcmp(a, b);
}

static int compare_entry(const void* l, const void* r) {
    const VersionEntry* a = (const VersionEntry*)l;
    const VersionEntry* b = (const VersionEntry*)r;
    int ra = type_rank(a->type);
    int rb = type_rank(b->type);

    if (ra != rb) return ra - rb;
    return compare_version_desc(a->id, b->id);
}

void version_sort(VersionEntry* list, int count) {
    if (count > 1) qsort(list, (size_t)count, sizeof(VersionEntry), compare_entry);
}

const char* version_state_text(int state) {
    switch (state) {
    case VERSION_OK:        return "可用";
    case VERSION_INHERITED: return "继承版本（暂不支持）";
    case VERSION_MISSING_JAR: return "缺少客户端 jar";
    case VERSION_INCOMPLETE:  return "不完整";
    case VERSION_BAD_JSON:    return "JSON 无法解析";
    case VERSION_NO_JSON:     return "缺少版本 JSON";
    default:                return "未知状态";
    }
}

int version_scan(LauncherConfig* config, VersionEntry* out, int max) {
    char pattern[1024];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int count = 0;

    if (!config || !out || max <= 0) return 0;

    snprintf(pattern, sizeof(pattern), "%s\\versions\\*", config->mc_dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        VersionEntry* e;
        char json_path[1024];
        char jar_path[1024];
        char* text;
        cJSON* root;
        cJSON* node;
        const char* main_class;
        int jar_ok;
        int has_main;

        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (count >= max) break;

        e = &out[count];
        memset(e, 0, sizeof(*e));
        copy_field(e->id, sizeof(e->id), fd.cFileName);
        snprintf(e->dir, sizeof(e->dir), "%s\\versions\\%s", config->mc_dir, fd.cFileName);
        copy_field(e->type, sizeof(e->type), "unknown");

        snprintf(json_path, sizeof(json_path), "%s\\%s.json", e->dir, e->id);
        snprintf(jar_path, sizeof(jar_path), "%s\\%s.jar", e->dir, e->id);

        text = read_file(json_path);
        if (!text) {
            e->state = VERSION_NO_JSON;
            copy_field(e->note, sizeof(e->note), "目录里没有同名 .json");
            count++;
            continue;
        }

        root = cJSON_Parse(text);
        free(text);
        if (!root) {
            e->state = VERSION_BAD_JSON;
            copy_field(e->note, sizeof(e->note), "JSON 解析失败");
            count++;
            continue;
        }

        copy_field(e->type, sizeof(e->type), json_string(root, "type") ? json_string(root, "type") : "unknown");
        copy_field(e->release_time, sizeof(e->release_time), json_string(root, "releaseTime"));
        copy_field(e->inherits_from, sizeof(e->inherits_from), json_string(root, "inheritsFrom"));

        node = cJSON_GetObjectItemCaseSensitive(root, "assetIndex");
        copy_field(e->asset_index, sizeof(e->asset_index), json_string(node, "id"));

        node = cJSON_GetObjectItemCaseSensitive(root, "javaVersion");
        node = node ? cJSON_GetObjectItemCaseSensitive(node, "majorVersion") : NULL;
        if (cJSON_IsNumber(node)) e->java_major = (int)node->valuedouble;

        main_class = json_string(root, "mainClass");
        has_main = main_class && main_class[0];
        jar_ok = GetFileAttributesA(jar_path) != INVALID_FILE_ATTRIBUTES;
        e->missing_libs = count_missing_libs(config->mc_dir, root);

        if (e->inherits_from[0]) {
            e->state = VERSION_INHERITED;
            snprintf(e->note, sizeof(e->note), "继承自 %s", e->inherits_from);
        }
        else if (!has_main) {
            e->state = VERSION_INCOMPLETE;
            copy_field(e->note, sizeof(e->note), "缺少 mainClass");
        }
        else if (!jar_ok) {
            e->state = VERSION_MISSING_JAR;
            copy_field(e->note, sizeof(e->note), "缺少客户端 jar");
        }
        else if (e->missing_libs > 0) {
            e->state = VERSION_INCOMPLETE;
            snprintf(e->note, sizeof(e->note), "缺少 %d 个库文件", e->missing_libs);
        }
        else {
            e->state = VERSION_OK;
            copy_field(e->note, sizeof(e->note), "可用");
        }

        cJSON_Delete(root);
        count++;
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    version_sort(out, count);
    return count;
}

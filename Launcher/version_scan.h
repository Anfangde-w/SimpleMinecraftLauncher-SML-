#ifndef VERSION_SCAN_H
#define VERSION_SCAN_H

#include "launcher_config.h"

#define VERSION_MAX 256

typedef enum {
    VERSION_OK = 0,
    VERSION_INHERITED,     // 依赖父版本，本启动器暂不支持
    VERSION_MISSING_JAR,   // 有 json 但没有客户端 jar
    VERSION_INCOMPLETE,    // 缺库 / 缺 mainClass
    VERSION_BAD_JSON,      // json 解析失败
    VERSION_NO_JSON        // 目录里没有版本 json
} VersionState;

typedef struct {
    char id[128];
    char type[32];
    char release_time[32];
    char asset_index[32];
    char inherits_from[128];
    int java_major;
    int missing_libs;
    int state;
    char dir[1024];
    char note[256];
} VersionEntry;

// 扫描 <mc_dir>\versions，返回条目数（含不可用的版本，便于在选择器里说明原因）
int version_scan(LauncherConfig* config, VersionEntry* out, int max);

// 正式版在前，然后按版本号从新到旧
void version_sort(VersionEntry* list, int count);

const char* version_state_text(int state);

#endif // VERSION_SCAN_H

#ifndef DOWNLOAD_GAME_H
#define DOWNLOAD_GAME_H

#include <stddef.h>

typedef struct {
    char id[64];
    char type[32];
    char release_time[32];
    char url[512];
    char sha1[41];
    long long size;
} RemoteVersion;

typedef enum {
    DL_STAGE_PREPARE = 0,   // 获取版本信息 / 资源索引
    DL_STAGE_FILES = 1      // 校验 / 下载文件
} DlStage;

typedef struct {
    int stage;
    int file_index;
    int file_count;
    int skipped;
    const char* file_label;
    long long bytes_done;
    long long bytes_total;
} DownloadProgress;

typedef void (*DownloadProgressFn)(void* user, const DownloadProgress* progress);

// 拉取 Mojang 版本清单（失败时回退到本地缓存）。成功返回 1，list 由调用方用 download_free_versions 释放
int download_fetch_versions(const char* mc_dir, RemoteVersion** out_list, int* out_count,
                            char* latest_release, size_t lr_cap,
                            char* latest_snapshot, size_t ls_cap);
void download_free_versions(RemoteVersion* list);

// 下载/校验一个版本的完整游戏文件；成功返回 1
int download_game_run(const char* mc_dir, const RemoteVersion* version,
                      DownloadProgressFn progress, void* user,
                      char* message, size_t message_cap);

#endif // DOWNLOAD_GAME_H

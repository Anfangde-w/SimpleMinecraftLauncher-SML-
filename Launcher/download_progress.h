#ifndef DOWNLOAD_PROGRESS_H
#define DOWNLOAD_PROGRESS_H

#include <windows.h>
#include "download_game.h"

typedef struct {
    char version[64];
    int interactive;
    int last_pct;
    int last_stage;
    long long last_done;
    DWORD last_tick;
    DWORD start_tick;
    double speed;          // 字节/秒
} DownloadProgressUi;

void dl_progress_begin(DownloadProgressUi* ui, const char* version_id, int interactive);
void dl_progress_update(DownloadProgressUi* ui, const DownloadProgress* progress);
void dl_progress_finish(DownloadProgressUi* ui, int ok, const char* message);

#endif // DOWNLOAD_PROGRESS_H

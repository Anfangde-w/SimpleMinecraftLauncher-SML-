#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "download_progress.h"
#include "download_game.h"
#include "console_ui.h"
#include "text_util.h"

#define LINE_MAX 1200
#define BAR_WIDTH 40

static void fit_line(wchar_t* s, int width) {
    size_t len;
    if (width <= 0) return;
    len = wcslen(s);
    if (len < (size_t)width) return;
    if (width > 2) {
        s[width - 2] = L'…';
        s[width - 1] = L'\0';
    }
}

static void fmt_bytes(long long value, char* out, size_t cap) {
    if (value >= 1024LL * 1024 * 1024) {
        snprintf(out, cap, "%.2f GB", (double)value / (1024.0 * 1024.0 * 1024.0));
    }
    else if (value >= 1024LL * 1024) {
        snprintf(out, cap, "%.1f MB", (double)value / (1024.0 * 1024.0));
    }
    else if (value >= 1024) {
        snprintf(out, cap, "%.1f KB", (double)value / 1024.0);
    }
    else {
        snprintf(out, cap, "%lld B", value);
    }
}

static int calc_percent(long long done, long long total) {
    int pct;
    if (total <= 0) return -1;
    pct = (int)((done * 100) / total);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static void build_bar(wchar_t* bar, int pct) {
    int i;
    int filled = (pct < 0) ? 0 : pct * BAR_WIDTH / 100;
    if (filled > BAR_WIDTH) filled = BAR_WIDTH;
    for (i = 0; i < BAR_WIDTH; i++) bar[i] = (i < filled) ? L'#' : L'-';
    bar[BAR_WIDTH] = L'\0';
}

// 总长度未知时用滚动条表示“还在进行”
static void build_spinner(wchar_t* bar) {
    int i;
    int pos = (int)((GetTickCount() / 90) % (BAR_WIDTH + 12)) - 6;

    for (i = 0; i < BAR_WIDTH; i++) bar[i] = L'-';
    for (i = pos; i < pos + 10; i++) {
        if (i >= 0 && i < BAR_WIDTH) bar[i] = L'#';
    }
    bar[BAR_WIDTH] = L'\0';
}

// 补空格到整行，避免残留上一次的长文本
static void put_padded(int y, const wchar_t* text, WORD attr, int width) {
    wchar_t buf[LINE_MAX];
    size_t len;

    swprintf(buf, LINE_MAX, L"%ls", text);
    len = wcslen(buf);
    while ((int)len < width - 1 && len < LINE_MAX - 2) buf[len++] = L' ';
    buf[len] = L'\0';
    console_put_line(y, buf, attr);
}

static void fmt_eta(long long seconds, wchar_t* out, size_t cap) {
    if (seconds < 0) {
        swprintf(out, cap, L"计算中");
    }
    else if (seconds < 60) {
        swprintf(out, cap, L"约 %lld 秒", seconds);
    }
    else if (seconds < 3600) {
        swprintf(out, cap, L"约 %lld 分 %lld 秒", seconds / 60, seconds % 60);
    }
    else {
        swprintf(out, cap, L"约 %lld 小时 %lld 分", seconds / 3600, (seconds % 3600) / 60);
    }
}

static void draw_prepare(DownloadProgressUi* ui, const wchar_t* label,
                         long long done, long long total) {
    int width = console_width();
    int height = console_height();
    wchar_t line[LINE_MAX];
    wchar_t bar[BAR_WIDTH + 1];
    wchar_t* version = utf8_to_wide(ui->version);
    char done_text[64];
    char total_text[64];
    int pct = calc_percent(done, total);

    if (pct >= 0) build_bar(bar, pct);
    else build_spinner(bar);

    fmt_bytes(done, done_text, sizeof(done_text));
    fmt_bytes(total > 0 ? total : 0, total_text, sizeof(total_text));

    console_put_line(1, L"下载游戏", CONSOLE_ATTR_TITLE);

    swprintf(line, LINE_MAX, L"  版本：%ls", version ? version : L"?");
    fit_line(line, width);
    put_padded(3, line, CONSOLE_ATTR_NORMAL, width);

    put_padded(4, L"  阶段：正在准备…", CONSOLE_ATTR_NORMAL, width);

    swprintf(line, LINE_MAX, L"  %ls", label ? label : L"");
    fit_line(line, width);
    put_padded(5, line, CONSOLE_ATTR_NORMAL, width);

    if (pct >= 0) {
        swprintf(line, LINE_MAX, L"  [%ls]  %d%%", bar, pct);
    }
    else {
        swprintf(line, LINE_MAX, L"  [%ls]", bar);
    }
    put_padded(7, line, CONSOLE_ATTR_NORMAL, width);

    if (total > 0) {
        swprintf(line, LINE_MAX, L"  已获取 %hs / %hs", done_text, total_text);
    }
    else {
        swprintf(line, LINE_MAX, L"  已获取 %hs", done_text);
    }
    put_padded(8, line, CONSOLE_ATTR_NORMAL, width);

    if (height >= 3) {
        put_padded(height - 2, L"  正在获取版本信息与资源索引，请稍候…", CONSOLE_ATTR_HINT, width);
    }

    free(version);
}

static void draw_files(DownloadProgressUi* ui, const DownloadProgress* p) {
    int width = console_width();
    int height = console_height();
    wchar_t line[LINE_MAX];
    wchar_t file_bar[BAR_WIDTH + 1];
    wchar_t data_bar[BAR_WIDTH + 1];
    wchar_t* version = utf8_to_wide(ui->version);
    wchar_t* label = utf8_to_wide(p->file_label ? p->file_label : "");
    char done_text[64];
    char total_text[64];
    char speed_text[64];
    wchar_t eta_text[128];
    char elapsed_text[64];
    long long elapsed;
    long long remaining = -1;
    int data_pct = calc_percent(p->bytes_done, p->bytes_total);
    int file_pct = calc_percent(p->file_index, p->file_count);

    build_bar(file_bar, file_pct >= 0 ? file_pct : 0);
    build_bar(data_bar, data_pct >= 0 ? data_pct : 0);

    elapsed = (long long)((GetTickCount() - ui->start_tick) / 1000);
    snprintf(elapsed_text, sizeof(elapsed_text), "%lld:%02lld", elapsed / 60, elapsed % 60);

    fmt_bytes(p->bytes_done, done_text, sizeof(done_text));
    fmt_bytes(p->bytes_total, total_text, sizeof(total_text));

    if (ui->speed >= 1024.0 * 1024.0) {
        snprintf(speed_text, sizeof(speed_text), "%.1f MB/s", ui->speed / (1024.0 * 1024.0));
    }
    else if (ui->speed >= 1024.0) {
        snprintf(speed_text, sizeof(speed_text), "%.1f KB/s", ui->speed / 1024.0);
    }
    else {
        snprintf(speed_text, sizeof(speed_text), "%.0f B/s", ui->speed);
    }

    if (data_pct == 100) {
        wcscpy(eta_text, L"即将完成");
    }
    else if (p->bytes_total > 0 && ui->speed > 1.0) {
        remaining = (long long)((p->bytes_total - p->bytes_done) / ui->speed);
        if (remaining < 0) remaining = 0;
        fmt_eta(remaining, eta_text, 128);
    }
    else {
        fmt_eta(-1, eta_text, 128);
    }

    console_put_line(1, L"下载游戏", CONSOLE_ATTR_TITLE);

    swprintf(line, LINE_MAX, L"  版本：%ls", version ? version : L"?");
    fit_line(line, width);
    put_padded(3, line, CONSOLE_ATTR_NORMAL, width);

    swprintf(line, LINE_MAX, L"  阶段：校验 / 下载   已用时间 %hs", elapsed_text);
    put_padded(4, line, CONSOLE_ATTR_NORMAL, width);

    swprintf(line, LINE_MAX, L"  当前：%ls", label ? label : L"");
    fit_line(line, width);
    put_padded(5, line, CONSOLE_ATTR_NORMAL, width);

    swprintf(line, LINE_MAX, L"  文件进度 [%ls]  %d%%   %d / %d（跳过 %d）",
             file_bar, file_pct >= 0 ? file_pct : 0,
             p->file_index, p->file_count, p->skipped);
    put_padded(7, line, CONSOLE_ATTR_NORMAL, width);

    swprintf(line, LINE_MAX, L"  数据进度 [%ls]  %d%%   %hs / %hs",
             data_bar, data_pct >= 0 ? data_pct : 0, done_text, total_text);
    put_padded(8, line, CONSOLE_ATTR_NORMAL, width);

    swprintf(line, LINE_MAX, L"  处理速度 %hs    预计剩余 %ls", speed_text, eta_text);
    put_padded(9, line, CONSOLE_ATTR_NORMAL, width);

    if (height >= 3) {
        put_padded(height - 2, L"  正在校验并下载游戏文件，请勿关闭窗口", CONSOLE_ATTR_HINT, width);
    }

    free(version);
    free(label);
}

void dl_progress_begin(DownloadProgressUi* ui, const char* version_id, int interactive) {
    ZeroMemory(ui, sizeof(*ui));
    snprintf(ui->version, sizeof(ui->version), "%s", version_id ? version_id : "");
    ui->interactive = interactive;
    ui->start_tick = GetTickCount();
    ui->last_tick = ui->start_tick;
    ui->last_stage = -1;

    if (!interactive) {
        printf("\n开始校验并下载 %s ...\n", ui->version);
        fflush(stdout);
        return;
    }

    console_hide_cursor(1);
    console_clear();
    draw_prepare(ui, L"正在获取版本信息…", 0, -1);
}

void dl_progress_update(DownloadProgressUi* ui, const DownloadProgress* p) {
    DWORD now;
    DWORD delta_ms;
    long long delta_bytes;
    int pct;

    if (!ui || !p) return;

    now = GetTickCount();

    if (!ui->interactive) {
        if (p->stage == DL_STAGE_PREPARE) {
            if (ui->last_stage != p->stage) {
                printf("  %s\n", p->file_label ? p->file_label : "正在准备…");
                fflush(stdout);
                ui->last_stage = p->stage;
            }
            return;
        }

        pct = calc_percent(p->file_index, p->file_count);
        if (pct < 0) pct = 0;
        if (pct >= ui->last_pct + 10 || (p->file_index > 0 && p->file_index == p->file_count)) {
            ui->last_pct = pct;
            printf("  下载进度 %d%%（文件 %d/%d，跳过 %d）\n",
                   pct, p->file_index, p->file_count, p->skipped);
            fflush(stdout);
        }
        return;
    }

    if (p->stage != ui->last_stage) {
        console_clear();
        ui->last_stage = p->stage;
    }

    if (p->stage == DL_STAGE_PREPARE) {
        wchar_t* label = utf8_to_wide(p->file_label ? p->file_label : "");
        draw_prepare(ui, label, p->bytes_done, p->bytes_total);
        free(label);
        return;
    }

    delta_ms = now - ui->last_tick;
    delta_bytes = p->bytes_done - ui->last_done;
    if (delta_ms >= 200) {
        double instant = (double)delta_bytes * 1000.0 / (double)delta_ms;
        if (instant < 0) instant = 0;
        ui->speed = (ui->speed > 0.0) ? (ui->speed * 0.6 + instant * 0.4) : instant;
        ui->last_tick = now;
        ui->last_done = p->bytes_done;
    }

    draw_files(ui, p);
}

void dl_progress_finish(DownloadProgressUi* ui, int ok, const char* message) {
    if (!ui->interactive) {
        printf("%s\n", message ? message : "");
        return;
    }

    {
        wchar_t* wmsg = utf8_to_wide(message);
        console_clear();
        console_put_line(2, ok ? L"下载完成" : L"下载失败", CONSOLE_ATTR_TITLE);
        console_put_line(4, wmsg ? wmsg : L"", CONSOLE_ATTR_NORMAL);
        console_put_line(6, L"按任意键返回", CONSOLE_ATTR_HINT);
        console_hide_cursor(0);
        console_wait_key();
        console_clear();
        free(wmsg);
    }
}

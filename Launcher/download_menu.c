#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "download_menu.h"
#include "download_game.h"
#include "download_progress.h"
#include "http_client.h"
#include "launcher_settings.h"
#include "ui_list.h"
#include "console_ui.h"
#include "text_util.h"

#define LINE_MAX 1200

static RemoteVersion* g_versions = NULL;
static int g_version_count = 0;
static int g_loaded = 0;
static char g_latest_release[64];
static char g_latest_snapshot[64];

static int* g_filter = NULL;
static int g_filter_count = 0;

static const wchar_t* CATEGORY_LABELS[6] = {
    L"原版游戏", L"Mod", L"整合包", L"数据包", L"资源包", L"光影包"
};

static const wchar_t* VANILLA_LABELS[5] = {
    L"下载最新正式版", L"正式版", L"快照版",
    L"远古版（开发中）", L"愚人节版（开发中）"
};

static const wchar_t* PAGE_LABELS[6] = {
    L"1. 下载原版",
    L"2. 添加 Forge（开发中）",
    L"3. 添加 NeoForge（开发中）",
    L"4. 添加 Fabric（开发中）",
    L"5. 添加 Optifine（开发中）",
    L"0. 返回"
};

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

static const wchar_t* type_label(const char* type) {
    if (!type) return L"未知";
    if (strcmp(type, "release") == 0) return L"正式版";
    if (strcmp(type, "snapshot") == 0) return L"快照版";
    if (strcmp(type, "old_beta") == 0) return L"旧测试版";
    if (strcmp(type, "old_alpha") == 0) return L"远古版";
    return L"其他";
}

static void show_message(const wchar_t* title, const char* text) {
    if (!console_is_interactive()) {
        printf("\n%s\n\n", text);
        return;
    }

    {
        wchar_t* wtext = utf8_to_wide(text);
        console_hide_cursor(1);
        console_clear();
        console_put_line(3, title, CONSOLE_ATTR_TITLE);
        console_put_line(5, wtext ? wtext : L"", CONSOLE_ATTR_NORMAL);
        console_put_line(7, L"按任意键返回", CONSOLE_ATTR_HINT);
        console_hide_cursor(0);
        console_wait_key();
        console_clear();
        free(wtext);
    }
}

static void show_todo_page(const wchar_t* title) {
    if (!console_is_interactive()) {
        char* t = wide_to_utf8(title);
        printf("\n%s\n\n  开发中……\n\n", t ? t : "");
        free(t);
        return;
    }

    console_hide_cursor(1);
    console_clear();
    console_put_line(2, title, CONSOLE_ATTR_TITLE);
    console_put_line(4, L"  开发中……", CONSOLE_ATTR_NORMAL);
    console_put_line(6, L"按任意键返回", CONSOLE_ATTR_HINT);
    console_hide_cursor(0);
    console_wait_key();
    console_clear();
}

static int ensure_versions(LauncherConfig* config) {
    if (g_loaded) return 1;

    if (console_is_interactive()) {
        console_clear();
        console_put_line(2, L"正在获取游戏版本列表...", CONSOLE_ATTR_NORMAL);
    }
    else {
        printf("正在获取游戏版本列表...\n");
    }

    if (!download_fetch_versions(config->mc_dir, &g_versions, &g_version_count,
                                 g_latest_release, sizeof(g_latest_release),
                                 g_latest_snapshot, sizeof(g_latest_snapshot))) {
        show_message(L"获取版本列表失败", "无法获取版本列表：网络不可用，且本地没有缓存。");
        return 0;
    }

    g_loaded = 1;
    if (console_is_interactive()) console_clear();
    return 1;
}

static void build_filter(const char* type) {
    int i;

    free(g_filter);
    g_filter = NULL;
    g_filter_count = 0;

    if (!g_versions || g_version_count <= 0) return;

    g_filter = (int*)malloc((size_t)g_version_count * sizeof(int));
    if (!g_filter) return;

    for (i = 0; i < g_version_count; i++) {
        if (strcmp(g_versions[i].type, type) == 0) {
            g_filter[g_filter_count++] = i;
        }
    }
}

static void category_row(int i, wchar_t* buf, size_t cap) {
    swprintf(buf, cap, L"%d. %ls", i + 1, CATEGORY_LABELS[i]);
}

static void vanilla_row(int i, wchar_t* buf, size_t cap) {
    swprintf(buf, cap, L"%d. %ls", i + 1, VANILLA_LABELS[i]);
}

static void version_row(int i, wchar_t* buf, size_t cap) {
    RemoteVersion* v = &g_versions[g_filter[i]];
    wchar_t* id = utf8_to_wide(v->id);
    wchar_t* date = utf8_to_wide(v->release_time);

    swprintf(buf, cap, L"%-14ls %-6ls %.10ls",
             id ? id : L"?", type_label(v->type), date ? date : L"");
    free(id);
    free(date);
}

static void version_detail(int i, wchar_t* buf, size_t cap) {
    RemoteVersion* v = &g_versions[g_filter[i]];
    wchar_t* id = utf8_to_wide(v->id);
    wchar_t* date = utf8_to_wide(v->release_time);

    swprintf(buf, cap, L"%ls\n类型：%ls · 发布：%.10ls\n选中后进入下载页，可校验/下载该版本",
             id ? id : L"?", type_label(v->type), date ? date : L"");
    free(id);
    free(date);
}

static void draw_page(const RemoteVersion* v, int sel) {
    wchar_t line[LINE_MAX];
    wchar_t* id = utf8_to_wide(v->id);
    int width = console_width();
    int height = console_height();
    int y = 2;
    int i;

    console_clear();

    swprintf(line, LINE_MAX, L"准备下载游戏版本：%ls", id ? id : L"?");
    fit_line(line, width);
    console_put_line(y++, line, CONSOLE_ATTR_TITLE);
    y++;

    console_put_line(y++, L"  已添加的加载器：", CONSOLE_ATTR_NORMAL);
    console_put_line(y++, L"    Forge：未添加", CONSOLE_ATTR_NORMAL);
    console_put_line(y++, L"    NeoForge：未添加", CONSOLE_ATTR_NORMAL);
    console_put_line(y++, L"    Fabric：未添加", CONSOLE_ATTR_NORMAL);
    console_put_line(y++, L"    Optifine：未添加", CONSOLE_ATTR_NORMAL);
    y++;

    for (i = 0; i < 6; i++) {
        if (i == sel) {
            swprintf(line, LINE_MAX, L"> %ls", PAGE_LABELS[i]);
        }
        else {
            swprintf(line, LINE_MAX, L"  %ls", PAGE_LABELS[i]);
        }
        fit_line(line, width);
        console_put_line(y++, line, (i == sel) ? CONSOLE_ATTR_SELECTED : CONSOLE_ATTR_NORMAL);
    }

    if (height >= 2) {
        console_put_line(height - 2, L"  ↑↓ 选择   Enter 确认   数字键直达   Esc 返回", CONSOLE_ATTR_HINT);
    }
    free(id);
}

static void perform_download(LauncherConfig* config, const RemoteVersion* v) {
    DownloadProgressUi ui;
    char message[1024];
    int ok;

    dl_progress_begin(&ui, v->id, console_is_interactive());
    ok = download_game_run(config->mc_dir, v, dl_progress_update, &ui, message, sizeof(message));
    dl_progress_finish(&ui, ok, message);
}

// 返回 0 表示离开下载页
static int handle_page_choice(LauncherConfig* config, const RemoteVersion* v, int choice) {
    if (choice == 0) return 0;
    if (choice == 1) {
        perform_download(config, v);
        return 1;
    }
    show_todo_page(PAGE_LABELS[choice - 1]);
    return 1;
}

static void page_text(const RemoteVersion* v) {
    printf("\n准备下载游戏版本：%s\n\n", v->id);
    printf("  已添加的加载器：\n");
    printf("    Forge：未添加\n");
    printf("    NeoForge：未添加\n");
    printf("    Fabric：未添加\n");
    printf("    Optifine：未添加\n\n");
    printf("  1. 下载原版\n");
    printf("  2. 添加 Forge（开发中）\n");
    printf("  3. 添加 NeoForge（开发中）\n");
    printf("  4. 添加 Fabric（开发中）\n");
    printf("  5. 添加 Optifine（开发中）\n");
    printf("  0. 返回\n");
}

static void download_page(LauncherConfig* config, const RemoteVersion* v, LauncherSettings* settings) {
    if (!console_is_interactive()) {
        for (;;) {
            char line[64];
            int n;

            page_text(v);
            printf("\n选择 [0-5]: ");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) return;
            n = atoi(line);
            if (n < 0 || n > 5) continue;
            if (!handle_page_choice(config, v, n)) return;
        }
    }

    {
        int sel = settings ? settings->download_page_index : 0;

        if (sel < 0 || sel >= 6) sel = 0;

        console_hide_cursor(1);
        console_clear();
        for (;;) {
            int vk;
            int choice = (sel + 1) % 6;   // 列表第 1 项是“1”，最后一项是“0”

            draw_page(v, sel);
            vk = console_read_key();
            if (vk < 0) break;

            if (vk == VK_UP) {
                sel = (sel + 5) % 6;
                continue;
            }
            if (vk == VK_DOWN) {
                sel = (sel + 1) % 6;
                continue;
            }
            if (vk == VK_ESCAPE) break;
            if (vk == VK_RETURN) {
                if (!handle_page_choice(config, v, choice)) break;
                continue;
            }
            if (vk >= '0' && vk <= '5') {
                if (!handle_page_choice(config, v, vk - '0')) break;
                continue;
            }
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD5) {
                if (!handle_page_choice(config, v, vk - VK_NUMPAD0)) break;
                continue;
            }
        }
        console_hide_cursor(0);
        console_clear();

        if (settings) {
            settings->download_page_index = sel;
            settings_save(config->mc_dir, settings);
        }
    }
}

static void open_download_page(LauncherConfig* config, LauncherSettings* settings, int version_index) {
    if (version_index < 0 || version_index >= g_version_count) return;
    download_page(config, &g_versions[version_index], settings);
}

static void pick_version(LauncherConfig* config, LauncherSettings* settings,
                         const char* type, const wchar_t* title) {
    UiList ui;
    int picked;
    int* saved = (strcmp(type, "release") == 0)
                 ? &settings->release_index
                 : &settings->snapshot_index;

    if (!ensure_versions(config)) return;
    build_filter(type);
    if (g_filter_count <= 0) {
        show_message(L"没有版本", "这个分类下没有可用版本。");
        return;
    }

    ui.title = title;
    ui.hint = L"↑↓ 选择   Enter 确认   Esc 返回";
    ui.count = g_filter_count;
    ui.start = (*saved >= 0 && *saved < g_filter_count) ? *saved : 0;
    ui.row = version_row;
    ui.detail = version_detail;

    picked = ui_list_run(&ui);
    if (picked < 0) return;

    *saved = picked;
    settings_save(config->mc_dir, settings);
    open_download_page(config, settings, g_filter[picked]);
}

static void open_latest(LauncherConfig* config, LauncherSettings* settings) {
    int i;
    int index = -1;

    if (!ensure_versions(config)) return;

    for (i = 0; i < g_version_count; i++) {
        if (strcmp(g_versions[i].id, g_latest_release) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        for (i = 0; i < g_version_count; i++) {
            if (strcmp(g_versions[i].type, "release") == 0) {
                index = i;
                break;
            }
        }
    }
    if (index < 0) {
        show_message(L"没有正式版", "版本列表里没有正式版。");
        return;
    }
    open_download_page(config, settings, index);
}

static void vanilla_menu(LauncherConfig* config, LauncherSettings* settings) {
    UiList ui;

    ui.title = L"原版游戏";
    ui.hint = L"↑↓ 选择   Enter 确认   Esc 返回";
    ui.count = 5;
    ui.start = (settings->vanilla_index >= 0 && settings->vanilla_index < 5)
               ? settings->vanilla_index : 0;
    ui.row = vanilla_row;
    ui.detail = NULL;

    for (;;) {
        int picked = ui_list_run(&ui);
        if (picked < 0) return;

        settings->vanilla_index = picked;
        settings_save(config->mc_dir, settings);

        switch (picked) {
        case 0: open_latest(config, settings); break;
        case 1: pick_version(config, settings, "release", L"选择正式版"); break;
        case 2: pick_version(config, settings, "snapshot", L"选择快照版"); break;
        default: show_todo_page(VANILLA_LABELS[picked]); break;
        }
    }
}

void download_menu_run(LauncherConfig* config, LauncherSettings* settings) {
    UiList ui;

    ui.title = L"下载游戏";
    ui.hint = L"↑↓ 选择   Enter 确认   Esc 返回";
    ui.count = 6;
    ui.start = (settings->download_category >= 0 && settings->download_category < 6)
               ? settings->download_category : 0;
    ui.row = category_row;
    ui.detail = NULL;

    for (;;) {
        int picked = ui_list_run(&ui);
        if (picked < 0) return;

        settings->download_category = picked;
        settings_save(config->mc_dir, settings);

        if (picked == 0) {
            vanilla_menu(config, settings);
        }
        else {
            show_todo_page(CATEGORY_LABELS[picked]);
        }
    }
}

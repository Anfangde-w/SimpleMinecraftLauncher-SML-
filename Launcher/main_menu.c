#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "main_menu.h"
#include "console_ui.h"
#include "text_util.h"

#define LINE_MAX 1024
#define ITEM_COUNT 6

// 6 行高的 SML 点阵，绘制时逐行缩进，形成斜体效果
static const wchar_t* BANNER[6] = {
    L"######  ##   ##  ##",
    L"##      ### ###  ##",
    L"######  ## # ##  ##",
    L"    ##  ##   ##  ##",
    L"    ##  ##   ##  ##",
    L"######  ##   ##  #######"
};

static const wchar_t* SPACES = L"          ";   // 10 个空格

static const wchar_t* ITEM_LABELS[ITEM_COUNT] = {
    L"开始游戏",
    L"版本选择",
    L"环境配置",
    L"更多设置",
    L"关于",
    L"退出"
};

static const wchar_t* ITEM_KEYS[ITEM_COUNT] = {
    L"1", L"2", L"3", L"4", L"5", L"0"
};

static const char* ITEM_KEYS_NARROW[ITEM_COUNT] = {
    "1", "2", "3", "4", "5", "0"
};

static const MenuAction ITEM_ACTIONS[ITEM_COUNT] = {
    MENU_START_GAME,
    MENU_SELECT_VERSION,
    MENU_SELECT_JAVA,
    MENU_MORE,
    MENU_ABOUT,
    MENU_EXIT
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
    else {
        s[0] = L'\0';
    }
}

// 把点阵行按倍数放大（每个字符重复 scale 次）
static void make_scaled(const wchar_t* src, int scale, wchar_t* out, size_t cap) {
    size_t o = 0;
    const wchar_t* p;

    for (p = src; *p; p++) {
        int k;
        for (k = 0; k < scale; k++) {
            if (o + 1 < cap) out[o++] = *p;
        }
    }
    out[o] = L'\0';
}

static void draw_menu(const MenuStatus* status, int sel) {
    int width = console_width();
    int height = console_height();
    int y = 0;
    int i;
    wchar_t line[LINE_MAX];
    wchar_t* version = utf8_to_wide(status->version);
    wchar_t* java = utf8_to_wide(status->java);
    wchar_t* login = utf8_to_wide(status->login_mode);
    wchar_t* user = utf8_to_wide(status->username);

    console_clear();

    if (height >= 28 && width >= 60) {
        // 2 倍放大：12 行高
        for (i = 0; i < 6; i++) {
            wchar_t row[LINE_MAX];
            int d = (5 - i) * 3 / 5;
            int r;

            make_scaled(BANNER[i], 2, row, LINE_MAX);
            swprintf(line, LINE_MAX, L"%ls%ls", SPACES + (10 - d * 2), row);
            fit_line(line, width);
            for (r = 0; r < 2; r++) console_put_line(y++, line, CONSOLE_ATTR_TITLE);
        }
        y++;
    }
    else if (height >= 22 && width >= 44) {
        for (i = 0; i < 6; i++) {
            swprintf(line, LINE_MAX, L"%ls%ls", SPACES + (10 - (5 - i) * 3 / 5), BANNER[i]);
            fit_line(line, width);
            console_put_line(y++, line, CONSOLE_ATTR_TITLE);
        }
        y++;
    }
    else {
        console_put_line(y++, L"SML   Minecraft Launcher v0.1", CONSOLE_ATTR_TITLE);
        y++;
    }

    swprintf(line, LINE_MAX, L"  当前版本：%ls", version ? version : L"?");
    fit_line(line, width);
    console_put_line(y++, line, CONSOLE_ATTR_NORMAL);

    swprintf(line, LINE_MAX, L"  当前 Java：%ls", java ? java : L"?");
    fit_line(line, width);
    console_put_line(y++, line, CONSOLE_ATTR_NORMAL);

    swprintf(line, LINE_MAX, L"  登录模式：%ls", login ? login : L"?");
    fit_line(line, width);
    console_put_line(y++, line, CONSOLE_ATTR_NORMAL);

    swprintf(line, LINE_MAX, L"  用户名：%ls", user ? user : L"?");
    fit_line(line, width);
    console_put_line(y++, line, CONSOLE_ATTR_NORMAL);

    y++;

    for (i = 0; i < ITEM_COUNT; i++) {
        if (y >= height - 1) break;
        if (i == sel) {
            swprintf(line, LINE_MAX, L"> %ls. %ls", ITEM_KEYS[i], ITEM_LABELS[i]);
        }
        else {
            swprintf(line, LINE_MAX, L"  %ls. %ls", ITEM_KEYS[i], ITEM_LABELS[i]);
        }
        fit_line(line, width);
        console_put_line(y++, line, (i == sel) ? CONSOLE_ATTR_SELECTED : CONSOLE_ATTR_NORMAL);
    }

    if (height >= 2) {
        swprintf(line, LINE_MAX, L"  ↑↓ 选择   Enter 确认   数字键快捷   Esc 退出");
        fit_line(line, width);
        console_put_line(height - 2, line, CONSOLE_ATTR_HINT);
    }

    free(version);
    free(java);
    free(login);
    free(user);
}

static MenuAction run_text(const MenuStatus* status) {
    char line[64];
    int i;

    for (i = 0; i < 6; i++) {
        wchar_t buf[LINE_MAX];
        char* s;
        swprintf(buf, LINE_MAX, L"%ls%ls", SPACES + (10 - (5 - i) * 3 / 5), BANNER[i]);
        s = wide_to_utf8(buf);
        printf("%s\n", s ? s : "");
        free(s);
    }

    printf("\n  当前版本：%s\n", status->version ? status->version : "?");
    printf("  当前 Java：%s\n", status->java ? status->java : "?");
    printf("  登录模式：%s\n", status->login_mode ? status->login_mode : "?");
    printf("  用户名：%s\n\n", status->username ? status->username : "?");

    for (i = 0; i < ITEM_COUNT; i++) {
        char* s = wide_to_utf8(ITEM_LABELS[i]);
        printf("  %s. %s\n", ITEM_KEYS_NARROW[i], s ? s : "");
        free(s);
    }

    for (;;) {
        int n;
        printf("\n选择 [0-5]（回车默认 1）: ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) return MENU_EXIT;
        n = atoi(line);
        if (line[0] == '\n' || line[0] == '\r') n = 1;
        switch (n) {
        case 0: return MENU_EXIT;
        case 1: return MENU_START_GAME;
        case 2: return MENU_SELECT_VERSION;
        case 3: return MENU_SELECT_JAVA;
        case 4: return MENU_MORE;
        case 5: return MENU_ABOUT;
        default: break;
        }
    }
}

MenuAction main_menu_run(const MenuStatus* status) {
    int sel = 0;

    if (!status) return MENU_EXIT;
    if (!console_is_interactive()) return run_text(status);

    console_hide_cursor(1);
    console_clear();

    for (;;) {
        int vk;

        draw_menu(status, sel);
        vk = console_read_key();
        if (vk < 0) {
            sel = ITEM_COUNT - 1;
            break;
        }

        if (vk == VK_UP) {
            sel = (sel + ITEM_COUNT - 1) % ITEM_COUNT;
        }
        else if (vk == VK_DOWN) {
            sel = (sel + 1) % ITEM_COUNT;
        }
        else if (vk == VK_RETURN) {
            break;
        }
        else if (vk == VK_ESCAPE || vk == '0' || vk == VK_NUMPAD0) {
            sel = ITEM_COUNT - 1;
            break;
        }
        else if (vk >= '1' && vk <= '5') {
            sel = vk - '1';
            break;
        }
        else if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD5) {
            sel = vk - VK_NUMPAD1;
            break;
        }
    }

    console_hide_cursor(0);
    console_clear();

    return ITEM_ACTIONS[sel];
}

void main_menu_show_todo(void) {
    if (!console_is_interactive()) {
        printf("\n更多设置\n\n  开发中……\n\n");
        return;
    }

    console_hide_cursor(1);
    console_clear();
    console_put_line(2, L"更多设置", CONSOLE_ATTR_TITLE);
    console_put_line(4, L"  开发中……", CONSOLE_ATTR_NORMAL);
    console_put_line(6, L"按任意键返回", CONSOLE_ATTR_HINT);
    console_hide_cursor(0);
    console_wait_key();
    console_clear();
}

void main_menu_show_about(const char* mc_dir) {
    wchar_t* dir = utf8_to_wide(mc_dir);

    if (!console_is_interactive()) {
        printf("\nMinecraft Launcher v0.1\n纯 C + Win32 控制台启动器\n数据目录：%s\n\n", mc_dir);
        free(dir);
        return;
    }

    console_hide_cursor(1);
    console_clear();
    console_put_line(2, L"Minecraft Launcher v0.1", CONSOLE_ATTR_TITLE);
    console_put_line(4, L"  纯 C + Win32 控制台启动器", CONSOLE_ATTR_NORMAL);
    console_put_line(5, L"  JSON 解析：cJSON", CONSOLE_ATTR_NORMAL);
    console_put_line(6, L"  功能：版本选择 / Java 环境选择 / 离线启动", CONSOLE_ATTR_NORMAL);
    console_put_line(8, L"  数据目录：", CONSOLE_ATTR_NORMAL);
    console_put_line(9, dir ? dir : L"?", CONSOLE_ATTR_HINT);
    console_put_line(11, L"按任意键返回", CONSOLE_ATTR_HINT);
    console_hide_cursor(0);
    console_wait_key();
    console_clear();
    free(dir);
}

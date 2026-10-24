#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "ui_list.h"
#include "text_util.h"
#include "console_ui.h"

#define LINE_MAX 1024
#define DETAIL_MAX 3

static void pad_line(wchar_t* buf, int width) {
    size_t len = wcslen(buf);
    while ((int)len < width - 1) buf[len++] = L' ';
    buf[len] = L'\0';
}

// 把 detail 文本按 \n 切成多行
static void split_detail(const wchar_t* src, wchar_t lines[][LINE_MAX], int max_lines, int* out_count) {
    int n = 0;
    const wchar_t* p = src;

    while (n < max_lines) {
        const wchar_t* e = wcschr(p, L'\n');
        size_t len = e ? (size_t)(e - p) : wcslen(p);
        if (len >= LINE_MAX) len = LINE_MAX - 1;
        wmemcpy(lines[n], p, len);
        lines[n][len] = L'\0';
        n++;
        if (!e) break;
        p = e + 1;
    }
    *out_count = n;
}

static void draw(const UiList* list, int sel, int top,
                 int page, int width, int height) {
    wchar_t line[LINE_MAX];
    wchar_t detail[LINE_MAX];
    wchar_t dlines[DETAIL_MAX][LINE_MAX];
    int dcount = 0;
    int i, y = 0;

    swprintf(line, LINE_MAX, L"%ls", list->title);
    pad_line(line, width);
    console_put_line(y++, line, CONSOLE_ATTR_TITLE);

    line[0] = L'\0';
    pad_line(line, width);
    console_put_line(y++, line, CONSOLE_ATTR_NORMAL);

    for (i = 0; i < page && y < height - 1; i++) {
        int idx = top + i;
        if (idx < list->count) {
            wchar_t item[LINE_MAX];
            list->row(idx, item, LINE_MAX);
            swprintf(line, LINE_MAX, (idx == sel) ? L"> %ls" : L"  %ls", item);
        }
        else {
            line[0] = L'\0';
        }
        pad_line(line, width);
        console_put_line(y++, line, (idx == sel) ? CONSOLE_ATTR_SELECTED : CONSOLE_ATTR_NORMAL);
    }

    detail[0] = L'\0';
    if (list->detail) list->detail(sel, detail, LINE_MAX);
    split_detail(detail, dlines, DETAIL_MAX, &dcount);
    for (i = 0; i < DETAIL_MAX && y < height - 1; i++) {
        if (i < dcount) wcscpy(line, dlines[i]);
        else line[0] = L'\0';
        pad_line(line, width);
        console_put_line(y++, line, CONSOLE_ATTR_HINT);
    }

    while (y < height - 2) {
        line[0] = L'\0';
        pad_line(line, width);
        console_put_line(y++, line, CONSOLE_ATTR_NORMAL);
    }
    if (height >= 2) {
        swprintf(line, LINE_MAX, L"%ls", list->hint);
        pad_line(line, width);
        console_put_line(height - 2, line, CONSOLE_ATTR_HINT);
    }
    if (height >= 1) {
        line[0] = L'\0';
        pad_line(line, width);
        console_put_line(height - 1, line, CONSOLE_ATTR_NORMAL);
    }
}

// 没有真实控制台时（输入被重定向），退化成编号选择
static int run_text(const UiList* list) {
    char line[64];
    int i, n;

    {
        char* t = wide_to_utf8(list->title);
        printf("\n%s\n", t ? t : "");
        free(t);
    }

    for (i = 0; i < list->count; i++) {
        wchar_t buf[LINE_MAX];
        char* s;
        list->row(i, buf, LINE_MAX);
        s = wide_to_utf8(buf);
        printf("  %2d) %s\n", i + 1, s ? s : "");
        free(s);
    }

    printf("\n输入编号 [1-%d]（0 = 取消，回车 = %d）: ", list->count, list->start + 1);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) return -1;
    n = atoi(line);
    if (n == 0) return -1;
    if (n < 0 || n > list->count) return list->start;
    return n - 1;
}

int ui_list_run(const UiList* list) {
    int sel, top = 0, page, width, height;
    int result = -1;

    if (!list || list->count <= 0) return -1;

    if (!console_is_interactive()) return run_text(list);

    width = console_width();
    height = console_height();
    if (width > LINE_MAX - 1) width = LINE_MAX - 1;
    if (width < 40) width = 40;
    if (height < 9) height = 9;

    page = height - 8;
    if (page < 3) page = 3;

    sel = list->start;
    if (sel < 0) sel = 0;
    if (sel >= list->count) sel = list->count - 1;

    console_hide_cursor(1);
    console_clear();

    for (;;) {
        int vk;

        if (sel < top) top = sel;
        if (sel >= top + page) top = sel - page + 1;

        draw(list, sel, top, page, width, height);

        vk = console_read_key();
        if (vk < 0) break;

        if (vk == VK_UP) {
            if (sel > 0) sel--;
        }
        else if (vk == VK_DOWN) {
            if (sel < list->count - 1) sel++;
        }
        else if (vk == VK_PRIOR) {
            sel -= page;
            if (sel < 0) sel = 0;
        }
        else if (vk == VK_NEXT) {
            sel += page;
            if (sel >= list->count) sel = list->count - 1;
        }
        else if (vk == VK_HOME) {
            sel = 0;
        }
        else if (vk == VK_END) {
            sel = list->count - 1;
        }
        else if (vk == VK_RETURN) {
            result = sel;
            break;
        }
        else if (vk == VK_ESCAPE) {
            result = -1;
            break;
        }
    }

    console_hide_cursor(0);
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), CONSOLE_ATTR_NORMAL);
    console_clear();

    return result;
}

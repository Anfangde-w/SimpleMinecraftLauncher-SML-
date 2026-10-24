#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "java_scan.h"
#include "java_select.h"
#include "ui_list.h"
#include "text_util.h"

#pragma comment(lib, "comdlg32.lib")

static JavaEntry* g_list;
static int g_count;

static void java_row(int i, wchar_t* buf, size_t cap) {
    JavaEntry* e;
    wchar_t* version;
    wchar_t* vendor;
    wchar_t* state;
    const wchar_t* mark;

    if (i == g_count) {
        swprintf(buf, cap, L"+  手动选择 java.exe …");
        return;
    }

    e = &g_list[i];
    version = utf8_to_wide(e->version);
    vendor = utf8_to_wide(e->vendor);
    state = utf8_to_wide(java_state_text(e->state));
    mark = (e->state == JAVA_OK) ? L"★" : L" ";

    swprintf(buf, cap, L"%ls %-9ls %-4ls %-24ls %ls",
             mark,
             version ? version : L"?",
             e->bits == 64 ? L"64 位" : L"32 位",
             vendor ? vendor : L"",
             state ? state : L"");

    free(version);
    free(vendor);
    free(state);
}

static void java_detail(int i, wchar_t* buf, size_t cap) {
    JavaEntry* e;
    wchar_t* exe;
    wchar_t* version;
    wchar_t* vendor;
    wchar_t* note;

    if (i == g_count) {
        swprintf(buf, cap, L"打开文件对话框，直接指定 java.exe 或 javaw.exe\n选中的路径会先验证能否运行，再加入列表");
        return;
    }

    e = &g_list[i];
    exe = utf8_to_wide(e->exe);
    version = utf8_to_wide(e->version);
    vendor = utf8_to_wide(e->vendor);
    note = utf8_to_wide(e->note);

    swprintf(buf, cap, L"%ls\nJava %ls · %ls · %ls · %ls",
             exe ? exe : L"?",
             version ? version : L"?",
             vendor ? vendor : L"?",
             e->bits == 64 ? L"64 位" : L"32 位",
             note ? note : L"");

    free(exe);
    free(version);
    free(vendor);
    free(note);
}

static int pick_java_path(char* out, size_t cap) {
    OPENFILENAMEW ofn;
    wchar_t file[2048];
    char* utf8;

    file[0] = L'\0';
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Java 可执行文件 (java.exe; javaw.exe)\0java.exe;javaw.exe\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    ofn.lpstrTitle = L"选择 java.exe";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return 0;

    utf8 = wide_to_utf8(file);
    if (!utf8) return 0;
    snprintf(out, cap, "%s", utf8);
    free(utf8);
    return 1;
}

int java_select_run(JavaEntry* list, int count, int required_major, int default_index,
                    JavaEntry* out_entry) {
    wchar_t title[128];

    if (!out_entry) return -1;

    g_list = list;
    g_count = count;

    if (required_major > 0) {
        swprintf(title, 128, L"选择 Java（游戏需要 Java %d）", required_major);
    }
    else {
        wcscpy(title, L"选择 Java");
    }

    for (;;) {
        UiList ui;
        int index;

        ui.title = title;
        ui.hint = L"↑↓ 选择   Enter 确认   Esc 使用默认 Java";
        ui.count = count + 1;
        ui.start = default_index;
        ui.row = java_row;
        ui.detail = java_detail;

        index = ui_list_run(&ui);
        if (index < 0) return -1;

        if (index < count) {
            *out_entry = list[index];
            return 0;
        }

        {
            char manual[1024];
            JavaEntry probe;

            if (!pick_java_path(manual, sizeof(manual))) continue;

            if (!java_probe(manual, &probe, required_major)) {
                printf("\n无法运行这个 java.exe：%s\n", manual);
                Sleep(1200);
                continue;
            }
            *out_entry = probe;
            return 0;
        }
    }
}

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "version_scan.h"
#include "version_select.h"
#include "ui_list.h"
#include "text_util.h"

static VersionEntry* g_list;

static const wchar_t* type_label(const char* type) {
    if (strcmp(type, "release") == 0) return L"正式版";
    if (strcmp(type, "snapshot") == 0) return L"快照";
    if (strcmp(type, "old_beta") == 0) return L"旧测试版";
    if (strcmp(type, "old_alpha") == 0) return L"远古版";
    return L"未知";
}

static void version_row(int i, wchar_t* buf, size_t cap) {
    VersionEntry* v = &g_list[i];
    wchar_t* id = utf8_to_wide(v->id);
    wchar_t* state = utf8_to_wide(version_state_text(v->state));
    const wchar_t* mark = (v->state == VERSION_OK) ? L"●" : L"○";

    swprintf(buf, cap, L"%ls %-12ls %-6ls Java %-3d %ls",
             mark, id ? id : L"?", type_label(v->type), v->java_major, state ? state : L"?");

    free(id);
    free(state);
}

static void version_detail(int i, wchar_t* buf, size_t cap) {
    VersionEntry* v = &g_list[i];
    wchar_t* dir = utf8_to_wide(v->dir);
    wchar_t* note = utf8_to_wide(v->note);
    wchar_t* asset = utf8_to_wide(v->asset_index);
    wchar_t* state = utf8_to_wide(version_state_text(v->state));

    swprintf(buf, cap, L"目录: %ls\nJava %d · 资源索引 %ls · 缺失库 %d · %ls（%ls）",
             dir ? dir : L"?", v->java_major, asset && asset[0] ? asset : L"?",
             v->missing_libs, state ? state : L"?", note ? note : L"");

    free(dir);
    free(note);
    free(asset);
    free(state);
}

int version_select_run(VersionEntry* list, int count, int default_index) {
    UiList ui;

    if (!list || count <= 0) return -1;

    g_list = list;

    ui.title = L"选择游戏版本";
    ui.hint = L"↑↓ 选择   PgUp/PgDn 翻页   Enter 确认   Esc 取消";
    ui.count = count;
    ui.start = default_index;
    ui.row = version_row;
    ui.detail = version_detail;

    return ui_list_run(&ui);
}

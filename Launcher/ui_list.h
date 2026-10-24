#ifndef UI_LIST_H
#define UI_LIST_H

#include <stddef.h>
#include <wchar.h>

// 一个通用的控制台列表菜单。
// row 回调负责生成每一项的一行文本；detail 回调生成详情（可用 \n 分成多行，最多 3 行）。
typedef struct {
    const wchar_t* title;
    const wchar_t* hint;
    int count;
    int start;   // 初始选中项
    void (*row)(int index, wchar_t* buf, size_t cap);
    void (*detail)(int index, wchar_t* buf, size_t cap);
} UiList;

// 返回选中的下标；Esc 或取消返回 -1
int ui_list_run(const UiList* list);

#endif // UI_LIST_H

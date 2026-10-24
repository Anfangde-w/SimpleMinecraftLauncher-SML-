#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include "console_ui.h"

int console_is_interactive(void) {
    DWORD in_mode, out_mode;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    return GetConsoleMode(in, &in_mode) && GetConsoleMode(out, &out_mode);
}

int console_width(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) return 80;
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

int console_height(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) return 25;
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

void console_clear(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD home;
    DWORD cells;
    DWORD written = 0;

    if (!GetConsoleScreenBufferInfo(out, &csbi)) return;
    home.X = 0;
    home.Y = 0;
    cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
    FillConsoleOutputCharacterW(out, L' ', cells, home, &written);
    FillConsoleOutputAttribute(out, CONSOLE_ATTR_NORMAL, cells, home, &written);
    SetConsoleCursorPosition(out, home);
}

void console_put_line(int y, const wchar_t* text, WORD attr) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos;
    DWORD written = 0;

    if (y < 0) return;
    pos.X = 0;
    pos.Y = (SHORT)y;
    if (!SetConsoleCursorPosition(out, pos)) return;
    SetConsoleTextAttribute(out, attr);
    WriteConsoleW(out, text, (DWORD)wcslen(text), &written, NULL);
}

int console_read_key(void) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);

    for (;;) {
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(in, &rec, 1, &n)) return -1;
        if (rec.EventType != KEY_EVENT) continue;
        if (!rec.Event.KeyEvent.bKeyDown) continue;
        return (int)rec.Event.KeyEvent.wVirtualKeyCode;
    }
}

void console_hide_cursor(int hide) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci;

    if (!GetConsoleCursorInfo(out, &ci)) return;
    ci.bVisible = hide ? FALSE : TRUE;
    SetConsoleCursorInfo(out, &ci);
}

void console_wait_key(void) {
    if (!console_is_interactive()) return;
    fflush(stdout);
    console_read_key();
}

#ifndef CONSOLE_UI_H
#define CONSOLE_UI_H

#include <windows.h>
#include <wchar.h>

#define CONSOLE_ATTR_NORMAL   0x07
#define CONSOLE_ATTR_TITLE    0x0B
#define CONSOLE_ATTR_HINT     0x08
#define CONSOLE_ATTR_SELECTED (BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)

// stdin/stdout 都是真实控制台时才返回 1（被重定向时菜单会退化成编号输入）
int console_is_interactive(void);
int console_width(void);
int console_height(void);
void console_clear(void);
void console_put_line(int y, const wchar_t* text, WORD attr);
int console_read_key(void);   // 返回虚拟键码；失败返回 -1
void console_hide_cursor(int hide);
void console_wait_key(void);  // 交互模式等一个按键，否则读一行/一个字符

#endif // CONSOLE_UI_H

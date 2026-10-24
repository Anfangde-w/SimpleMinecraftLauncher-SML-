#ifndef MAIN_MENU_H
#define MAIN_MENU_H

typedef enum {
    MENU_START_GAME = 1,
    MENU_SELECT_VERSION = 2,
    MENU_SELECT_JAVA = 3,
    MENU_DOWNLOAD = 4,
    MENU_MORE = 5,
    MENU_ABOUT = 6,
    MENU_EXIT = 0
} MenuAction;

typedef struct {
    const char* version;
    const char* java;
    const char* login_mode;
    const char* username;
} MenuStatus;

MenuAction main_menu_run(const MenuStatus* status, int* cursor);
void main_menu_show_todo(void);
void main_menu_show_about(const char* mc_dir);

#endif // MAIN_MENU_H

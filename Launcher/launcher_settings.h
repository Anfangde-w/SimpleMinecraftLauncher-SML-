#ifndef LAUNCHER_SETTINGS_H
#define LAUNCHER_SETTINGS_H

typedef struct {
    char version[128];
    char java_path[1024];
    int main_menu_index;
    int download_category;
    int vanilla_index;
    int download_page_index;
    int release_index;
    int snapshot_index;
} LauncherSettings;

// 文件位置：<mc_dir>\launcher.cfg
void settings_defaults(LauncherSettings* settings);
int settings_load(const char* mc_dir, LauncherSettings* settings);
int settings_save(const char* mc_dir, const LauncherSettings* settings);

#endif // LAUNCHER_SETTINGS_H

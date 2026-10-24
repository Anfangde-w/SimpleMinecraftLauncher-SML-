#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"
#include "launcher_config.h"
#include "file_util.h"
#include "classpath.h"
#include "args.h"
#include "launch.h"
#include "version_scan.h"
#include "version_select.h"
#include "java_scan.h"
#include "java_select.h"
#include "main_menu.h"
#include "console_ui.h"
#include "download_menu.h"
#include "launcher_settings.h"

static VersionEntry g_versions[VERSION_MAX];
static JavaEntry g_java[JAVA_MAX];
static JavaEntry g_current_java;
static char g_version_id[128];
static int g_java_valid = 0;
static int g_version_count = 0;
static int g_version_index = 0;
static int g_java_count = 0;
static int g_java_index = 0;
static LauncherSettings g_settings;

static int find_version_index(VersionEntry* list, int count, const char* id) {
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(list[i].id, id) == 0) return i;
    }
    return -1;
}

static int first_usable_version(VersionEntry* list, int count) {
    int i;
    for (i = 0; i < count; i++) {
        if (list[i].state == VERSION_OK) return i;
    }
    return -1;
}

static int find_java_index(JavaEntry* list, int count, const char* exe) {
    int i;
    for (i = 0; i < count; i++) {
        if (_stricmp(list[i].exe, exe) == 0) return i;
    }
    return -1;
}

static int first_usable_java(JavaEntry* list, int count) {
    int i;
    for (i = 0; i < count; i++) {
        if (list[i].state == JAVA_OK) return i;
    }
    return count > 0 ? 0 : -1;
}

static int java_satisfies(const JavaEntry* e, int required) {
    if (!e || e->bits != 64) return 0;
    if (required > 0 && e->major < required) return 0;
    return 1;
}

// 重新扫描版本；优先保持 prefer_id，找不到就用第一个可用版本
static int rescan_versions(LauncherConfig* config, const char* prefer_id) {
    char prefer[128];

    snprintf(prefer, sizeof(prefer), "%s", prefer_id ? prefer_id : "");
    g_version_count = version_scan(config, g_versions, VERSION_MAX);
    g_version_index = prefer[0] ? find_version_index(g_versions, g_version_count, prefer) : -1;
    if (g_version_index < 0) g_version_index = first_usable_version(g_versions, g_version_count);
    if (g_version_index < 0) g_version_index = 0;

    if (g_version_count <= 0) {
        g_version_id[0] = '\0';
        return 0;
    }

    snprintf(g_version_id, sizeof(g_version_id), "%s", g_versions[g_version_index].id);
    config->version = g_version_id;
    return 1;
}

// 重新扫描 Java；优先保持当前 exe，其次用配置里持久化的路径
static void rescan_java(LauncherConfig* config, int required) {
    char keep[1024] = "";

    if (g_java_valid) {
        snprintf(keep, sizeof(keep), "%s", g_current_java.exe);
    }
    else if (config->java_path && config->java_path[0]) {
        snprintf(keep, sizeof(keep), "%s", config->java_path);
    }

    g_java_count = java_scan(g_java, JAVA_MAX, required);
    g_java_index = keep[0] ? find_java_index(g_java, g_java_count, keep) : -1;
    if (g_java_index < 0) g_java_index = first_usable_java(g_java, g_java_count);
    if (g_java_index < 0) g_java_index = 0;

    if (g_java_count > 0) {
        g_current_java = g_java[g_java_index];
        g_java_valid = 1;
        config->java_path = g_current_java.exe;
    }
    else {
        g_java_valid = 0;
    }
}

static void build_java_display(char* out, size_t cap) {
    if (g_java_valid) {
        snprintf(out, cap, "Java %s", g_current_java.version);
    }
    else {
        snprintf(out, cap, "未检测到（将使用默认路径）");
    }
}

// 把当前版本 / Java / 菜单位置写回 launcher.cfg
static void persist_settings(LauncherConfig* config) {
    char version[128];
    char java_path[1024];

    snprintf(version, sizeof(version), "%s", config->version ? config->version : "");
    snprintf(java_path, sizeof(java_path), "%s", config->java_path ? config->java_path : "");

    snprintf(g_settings.version, sizeof(g_settings.version), "%s", version);
    snprintf(g_settings.java_path, sizeof(g_settings.java_path), "%s", java_path);

    settings_save(config->mc_dir, &g_settings);
}

static int start_game(LauncherConfig* config) {
    char json_path[512];
    char version_jar[1024];
    char classpath[65536];
    char natives_dir[512];
    char* json_str;
    cJSON* root;
    const char* main_class;
    const char* asset_id;
    int rc = 1;

    if (!config->java_path || GetFileAttributesA(config->java_path) == INVALID_FILE_ATTRIBUTES) {
        printf("\n当前 Java 不可用，请先在「环境配置」里选择。\n");
        return 1;
    }

    snprintf(json_path, sizeof(json_path), "%s\\versions\\%s\\%s.json",
             config->mc_dir, config->version, config->version);
    json_str = read_file(json_path);
    if (!json_str) {
        printf("\n找不到版本 JSON：%s\n", json_path);
        return 1;
    }

    root = cJSON_Parse(json_str);
    if (!root) {
        printf("\n版本 JSON 解析失败。\n");
        free(json_str);
        return 1;
    }

    {
        cJSON* mc = cJSON_GetObjectItemCaseSensitive(root, "mainClass");
        cJSON* ai = cJSON_GetObjectItemCaseSensitive(root, "assetIndex");
        cJSON* id = cJSON_GetObjectItemCaseSensitive(ai, "id");

        main_class = cJSON_IsString(mc) ? mc->valuestring : "net.minecraft.client.main.Main";
        asset_id = cJSON_IsString(id) ? id->valuestring : "17";
    }

    snprintf(version_jar, sizeof(version_jar), "%s\\versions\\%s\\%s.jar",
             config->mc_dir, config->version, config->version);
    {
        FILE* check_jar = fopen(version_jar, "rb");
        if (!check_jar) {
            printf("\n找不到客户端 jar：%s\n", version_jar);
            cJSON_Delete(root);
            free(json_str);
            return 1;
        }
        fclose(check_jar);
    }

    build_classpath_and_natives(config, root, classpath, natives_dir);

    if (!generate_args_file(config, classpath, natives_dir, main_class, asset_id)) {
        printf("\n无法创建 args.txt。\n");
    }
    else {
        launch_game(config);
        rc = 0;
    }

    cJSON_Delete(root);
    free(json_str);
    return rc;
}

// ================= 4. 主函数 =================
int main(void) {
    LauncherConfig config = {
        .java_path = "C:\\Program Files\\Java\\jdk-21.0.10\\bin\\java.exe",
        .mc_dir = "D:\\Code\\C-C++\\2026-10\\MinecraftLauncher_v0.1",
        .version = "1.21.3",
        .username = "Steve",
        .memory = "-Xmx2G"
    };
    MenuStatus status;
    char java_display[1024];
    int required_java;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Minecraft Launcher v0.1");

    settings_defaults(&g_settings);
    settings_load(config.mc_dir, &g_settings);
    if (g_settings.version[0]) config.version = g_settings.version;
    if (g_settings.java_path[0]) config.java_path = g_settings.java_path;

    printf("正在扫描游戏版本与 Java...\n");

    if (!rescan_versions(&config, config.version)) {
        printf("在 %s\\versions 下没有找到任何版本。\n", config.mc_dir);
        system("pause");
        return 1;
    }

    required_java = g_versions[g_version_index].java_major;
    rescan_java(&config, required_java);

    for (;;) {
        MenuAction action;

        build_java_display(java_display, sizeof(java_display));
        status.version = config.version;
        status.java = java_display;
        status.login_mode = "离线";
        status.username = config.username;

        action = main_menu_run(&status, &g_settings.main_menu_index);
        persist_settings(&config);

        if (action == MENU_EXIT) break;

        if (action == MENU_START_GAME) {
            if (start_game(&config) != 0) {
                printf("\n按任意键返回主菜单...\n");
                console_wait_key();
            }
            continue;
        }

        if (action == MENU_SELECT_VERSION) {
            char prefer[128];
            int picked;

            snprintf(prefer, sizeof(prefer), "%s", config.version);
            if (!rescan_versions(&config, prefer)) {
                printf("没有找到版本。\n");
                console_wait_key();
                continue;
            }

            picked = version_select_run(g_versions, g_version_count, g_version_index);
            if (picked >= 0) {
                g_version_index = picked;
                snprintf(g_version_id, sizeof(g_version_id), "%s", g_versions[picked].id);
                config.version = g_version_id;
            }

            required_java = g_versions[g_version_index].java_major;
            if (!g_java_valid || !java_satisfies(&g_current_java, required_java)) {
                printf("当前 Java 不满足该版本要求，正在重新检测...\n");
                rescan_java(&config, required_java);
            }
            persist_settings(&config);
            continue;
        }

        if (action == MENU_SELECT_JAVA) {
            JavaEntry chosen;

            required_java = g_versions[g_version_index].java_major;
            printf("正在重新检测 Java...\n");
            rescan_java(&config, required_java);

            if (java_select_run(g_java, g_java_count, required_java, g_java_index, &chosen) == 0) {
                g_current_java = chosen;
                g_java_valid = 1;
                config.java_path = g_current_java.exe;
                persist_settings(&config);
            }
            continue;
        }

        if (action == MENU_DOWNLOAD) {
            download_menu_run(&config, &g_settings);
            persist_settings(&config);
            continue;
        }

        if (action == MENU_MORE) {
            main_menu_show_todo();
            continue;
        }

        if (action == MENU_ABOUT) {
            main_menu_show_about(config.mc_dir);
            continue;
        }
    }

    printf("再见。\n");
    return 0;
}

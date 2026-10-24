#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "launcher_settings.h"

#define SETTINGS_FILE "launcher.cfg"

static void make_path(const char* mc_dir, char* out, size_t cap) {
    snprintf(out, cap, "%s\\%s", mc_dir, SETTINGS_FILE);
}

static void trim(char* s) {
    char* start = s;
    char* end;

    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);

    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
        *end = '\0';
    }
}

void settings_defaults(LauncherSettings* settings) {
    memset(settings, 0, sizeof(*settings));
}

int settings_load(const char* mc_dir, LauncherSettings* settings) {
    char path[1024];
    char line[2048];
    FILE* file;

    if (!mc_dir || !settings) return 0;
    make_path(mc_dir, path, sizeof(path));

    file = fopen(path, "rb");
    if (!file) return 0;

    while (fgets(line, sizeof(line), file)) {
        char* eq;
        char* key;
        char* value;

        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        key = line;
        value = eq + 1;
        trim(key);
        trim(value);

        if (strcmp(key, "version") == 0) {
            snprintf(settings->version, sizeof(settings->version), "%s", value);
        }
        else if (strcmp(key, "java") == 0) {
            snprintf(settings->java_path, sizeof(settings->java_path), "%s", value);
        }
        else if (strcmp(key, "main_menu_index") == 0) {
            settings->main_menu_index = atoi(value);
        }
        else if (strcmp(key, "download_category") == 0) {
            settings->download_category = atoi(value);
        }
        else if (strcmp(key, "vanilla_index") == 0) {
            settings->vanilla_index = atoi(value);
        }
        else if (strcmp(key, "download_page_index") == 0) {
            settings->download_page_index = atoi(value);
        }
        else if (strcmp(key, "release_index") == 0) {
            settings->release_index = atoi(value);
        }
        else if (strcmp(key, "snapshot_index") == 0) {
            settings->snapshot_index = atoi(value);
        }
    }

    fclose(file);
    return 1;
}

int settings_save(const char* mc_dir, const LauncherSettings* settings) {
    char path[1024];
    FILE* file;

    if (!mc_dir || !settings) return 0;
    make_path(mc_dir, path, sizeof(path));

    file = fopen(path, "wb");
    if (!file) return 0;

    fprintf(file, "# SML Launcher settings\n");
    fprintf(file, "version=%s\n", settings->version);
    fprintf(file, "java=%s\n", settings->java_path);
    fprintf(file, "main_menu_index=%d\n", settings->main_menu_index);
    fprintf(file, "download_category=%d\n", settings->download_category);
    fprintf(file, "vanilla_index=%d\n", settings->vanilla_index);
    fprintf(file, "download_page_index=%d\n", settings->download_page_index);
    fprintf(file, "release_index=%d\n", settings->release_index);
    fprintf(file, "snapshot_index=%d\n", settings->snapshot_index);
    fclose(file);
    return 1;
}

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "launcher_config.h"
#include "file_util.h"
#include "args.h"

// 模块 B：生成 args.txt
bool generate_args_file(LauncherConfig* config, const char* classpath, const char* natives_dir, const char* main_class, const char* asset_id) {
    FILE* f = fopen("args.txt", "w");
    if (!f) return false;

    fprintf(f, "%s\n", config->memory);
    fprintf(f, "-Djava.library.path=%s\n", natives_dir);
    fprintf(f, "-cp\n%s\n", classpath);
    fprintf(f, "%s\n", main_class);

    // 游戏参数
    fprintf(f, "--username\n%s\n", config->username);
    fprintf(f, "--version\n%s\n", config->version);
    fprintf(f, "--versionType\nrelease\n");

    char path_buf[512];
    sprintf(path_buf, "%s", config->mc_dir); fix_path(path_buf);
    fprintf(f, "--gameDir\n%s\n", path_buf);

    sprintf(path_buf, "%s/assets", config->mc_dir); fix_path(path_buf);
    fprintf(f, "--assetsDir\n%s\n", path_buf);

    fprintf(f, "--assetIndex\n%s\n", asset_id);
    fprintf(f, "--uuid\n00000000000000000000000000000000\n");
    fprintf(f, "--accessToken\n0\n");
    fprintf(f, "--userType\nlegacy\n");

    fclose(f);
    return true;
}

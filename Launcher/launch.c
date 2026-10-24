#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include "launcher_config.h"
#include "launch.h"

// 模块 C：拉起进程
void launch_game(LauncherConfig* config) {
    char final_cmd[2048];
    // "" 用于防止路径带空格报错
    sprintf(final_cmd, "\"\"%s\" @args.txt\"", config->java_path);

    printf("\n=== 正在启动 Minecraft %s ===\n", config->version);
    printf("如果游戏崩溃，请看下方日志：\n\n");

    system(final_cmd);
}

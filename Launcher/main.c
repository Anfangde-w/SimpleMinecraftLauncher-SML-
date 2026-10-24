#define _CRT_SECURE_NO_WARNINGS
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

// ================= 4. 主函数 =================
int main(void) {
    // 1. 初始化配置
    LauncherConfig config = {
        .java_path = "C:\\Program Files\\Java\\jdk-21.0.10\\bin\\java.exe",
        .mc_dir = "D:\\Code\\C-C++\\2026-10\\MinecraftLauncher_v0.1",
        .version = "1.21.3",
        .username = "Steve",
        .memory = "-Xmx2G"
    };

    // 2. 读取 JSON
    char json_path[512];
    sprintf(json_path, "%s\\versions\\%s\\%s.json", config.mc_dir, config.version, config.version);
    char* json_str = read_file(json_path);
    if (!json_str) {
        printf("找不到 JSON 文件：%s\n", json_path);
        system("pause");
        return 1;
    }

    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        printf("JSON 解析失败！\n");
        free(json_str);
        system("pause");
        return 1;
    }

    // 3. 提取基础信息
    cJSON* main_class_obj = cJSON_GetObjectItemCaseSensitive(root, "mainClass");
    const char* main_class = cJSON_IsString(main_class_obj) ? main_class_obj->valuestring : "net.minecraft.client.main.Main";

    cJSON* asset_index_obj = cJSON_GetObjectItemCaseSensitive(root, "assetIndex");
    cJSON* asset_id_obj = cJSON_GetObjectItemCaseSensitive(asset_index_obj, "id");
    const char* asset_id = cJSON_IsString(asset_id_obj) ? asset_id_obj->valuestring : "17";

    // 4. 检查核心 jar
    char version_jar[1024];
    sprintf(version_jar, "%s\\versions\\%s\\%s.jar", config.mc_dir, config.version, config.version);
    FILE* check_jar = fopen(version_jar, "rb");
    if (!check_jar) {
        printf("\n[致命错误] 找不到核心文件: %s\n", version_jar);
        system("pause");
        return 1;
    }
    fclose(check_jar);

    // 5. 执行模块
    char classpath[65536];
    char natives_dir[512];
    build_classpath_and_natives(&config, root, classpath, natives_dir);

    if (!generate_args_file(&config, classpath, natives_dir, main_class, asset_id)) {
        printf("无法创建 args.txt!\n");
    }
    else {
        launch_game(&config);
    }

    // 6. 清理内存
    cJSON_Delete(root);
    free(json_str);

    printf("\n游戏进程已结束。\n");
    system("pause");
    return 0;
}

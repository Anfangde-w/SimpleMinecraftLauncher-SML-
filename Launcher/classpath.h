#ifndef CLASSPATH_H
#define CLASSPATH_H

#include <stdbool.h>
#include "cJSON.h"
#include "launcher_config.h"

// 模块 A：处理 Libraries 和 Natives，生成 Classpath
bool build_classpath_and_natives(LauncherConfig* config, cJSON* root, char* out_classpath, char* out_natives_dir);

#endif // CLASSPATH_H

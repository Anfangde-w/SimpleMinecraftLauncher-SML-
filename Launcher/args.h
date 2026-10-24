#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>
#include "launcher_config.h"

// 模块 B：生成 args.txt
bool generate_args_file(LauncherConfig* config, const char* classpath, const char* natives_dir, const char* main_class, const char* asset_id);

#endif // ARGS_H

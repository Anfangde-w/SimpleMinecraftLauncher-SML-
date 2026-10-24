#ifndef PLATFORM_RULES_H
#define PLATFORM_RULES_H

#include <stdbool.h>
#include "cJSON.h"

// 按 version json 里的 rules 判断一个库/参数是否适用于当前环境（Windows x64，feature 全部关闭）
bool rules_allow(cJSON* item);

#endif // PLATFORM_RULES_H

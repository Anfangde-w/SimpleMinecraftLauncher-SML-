#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"
#include "platform_rules.h"

// 判断一条 rule 是否适用于当前环境（Windows x64，未启用任何 feature）
static bool rule_matches(cJSON* rule) {
    cJSON* os = cJSON_GetObjectItemCaseSensitive(rule, "os");
    if (os) {
        cJSON* name = cJSON_GetObjectItemCaseSensitive(os, "name");
        cJSON* arch = cJSON_GetObjectItemCaseSensitive(os, "arch");
        if (cJSON_IsString(name) && strcmp(name->valuestring, "windows") != 0) return false;
        if (cJSON_IsString(arch) && strcmp(arch->valuestring, "x86_64") != 0) return false;
    }
    // 带 features 的规则一律不匹配（本启动器不支持 demo / 自定义分辨率等）
    if (cJSON_GetObjectItemCaseSensitive(rule, "features")) return false;
    return true;
}

bool rules_allow(cJSON* item) {
    cJSON* rules = cJSON_GetObjectItemCaseSensitive(item, "rules");
    bool allow = false;
    cJSON* rule = NULL;

    if (!cJSON_IsArray(rules)) return true;

    cJSON_ArrayForEach(rule, rules) {
        if (!rule_matches(rule)) continue;
        cJSON* action = cJSON_GetObjectItemCaseSensitive(rule, "action");
        allow = cJSON_IsString(action) && strcmp(action->valuestring, "allow") == 0;
    }
    return allow;
}

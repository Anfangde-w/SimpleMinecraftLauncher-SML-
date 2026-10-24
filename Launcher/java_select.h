#ifndef JAVA_SELECT_H
#define JAVA_SELECT_H

#include <stddef.h>
#include "java_scan.h"

// 显示 Java 列表，返回 0 表示选中（结果写入 out_entry），Esc 返回 -1
int java_select_run(JavaEntry* list, int count, int required_major, int default_index,
                    JavaEntry* out_entry);

#endif // JAVA_SELECT_H

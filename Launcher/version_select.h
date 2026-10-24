#ifndef VERSION_SELECT_H
#define VERSION_SELECT_H

#include "version_scan.h"

// 显示版本列表，返回选中的下标；Esc 返回 -1
int version_select_run(VersionEntry* list, int count, int default_index);

#endif // VERSION_SELECT_H

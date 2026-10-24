#ifndef LAUNCHER_CONFIG_H
#define LAUNCHER_CONFIG_H

// ================= 1. 配置结构体 =================
typedef struct {
    const char* java_path;
    const char* mc_dir;
    const char* version;
    const char* username;
    const char* memory; // 例如 "-Xmx2G"
} LauncherConfig;

#endif // LAUNCHER_CONFIG_H

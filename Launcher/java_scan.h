#ifndef JAVA_SCAN_H
#define JAVA_SCAN_H

#define JAVA_MAX 64

typedef enum {
    JAVA_OK = 0,        // 主版本正好符合要求
    JAVA_NEWER,         // 高于要求，一般可用
    JAVA_OLD,           // 低于要求
    JAVA_WRONG_ARCH,    // 32 位 Java
    JAVA_BROKEN         // 探测失败
} JavaState;

typedef struct {
    char exe[1024];
    char javaw[1024];
    char home[1024];
    char version[64];
    char vendor[128];
    char arch[32];
    int major;
    int bits;
    int state;
    int score;
    char note[256];
} JavaEntry;

// 多来源扫描：JAVA_HOME、PATH、注册表、各厂商常见安装目录
int java_scan(JavaEntry* out, int max, int required_major);

// 探测单个 java.exe（也接受 javaw.exe / JAVA_HOME 目录），成功返回 1
int java_probe(const char* java_exe, JavaEntry* out, int required_major);

// 推荐/兼容的排在前面
void java_sort(JavaEntry* list, int count);

const char* java_state_text(int state);

#endif // JAVA_SCAN_H

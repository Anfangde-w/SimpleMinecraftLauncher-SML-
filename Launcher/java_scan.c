#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "java_scan.h"
#include "text_util.h"

#define PROBE_TIMEOUT_MS 6000

static void copy_str(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static int file_exists_a(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void path_dirname(const char* path, char* out, size_t cap) {
    const char* p = strrchr(path, '\\');
    if (!p) p = strrchr(path, '/');
    if (!p) {
        copy_str(out, cap, path);
        return;
    }
    {
        size_t n = (size_t)(p - path);
        if (n >= cap) n = cap - 1;
        memcpy(out, path, n);
        out[n] = '\0';
    }
}

static const char* path_basename(const char* path) {
    const char* p = strrchr(path, '\\');
    return p ? p + 1 : path;
}

// 运行 java -XshowSettings:properties -version，把 stdout + stderr 一起读回来
static int run_capture(const wchar_t* exe, const wchar_t* cmd, char* out, size_t cap, DWORD timeout_ms) {
    SECURITY_ATTRIBUTES sa;
    HANDLE rd = NULL, wr = NULL;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD start;
    size_t used = 0;

    out[0] = '\0';
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = NULL;

    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(exe, (LPWSTR)cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return 0;
    }
    CloseHandle(wr);

    start = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            DWORD want = (DWORD)(cap - 1 - used);
            DWORD got = 0;
            if (want > avail) want = avail;
            if (want > 4096) want = 4096;
            if (want > 0 && ReadFile(rd, out + used, want, &got, NULL) && got > 0) {
                used += got;
                continue;
            }
        }
        if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) break;
        if (GetTickCount() - start > timeout_ms) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    // 把管道里剩下的读干净
    for (;;) {
        DWORD want = (DWORD)(cap - 1 - used);
        DWORD got = 0;
        if (want > 4096) want = 4096;
        if (want == 0) break;
        if (!ReadFile(rd, out + used, want, &got, NULL) || got == 0) break;
        used += got;
    }

    out[used] = '\0';
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return used > 0;
}

// 在 -XshowSettings 输出里找 "key = value"
static int find_prop(const char* text, const char* key, char* out, size_t cap) {
    const char* p = text;
    size_t klen = strlen(key);

    out[0] = '\0';
    while ((p = strstr(p, key)) != NULL) {
        const char* q = p + klen;
        if (q[0] == ' ' && q[1] == '=' && q[2] == ' ') {
            const char* e;
            size_t n;
            q += 3;
            while (*q == ' ') q++;
            e = q;
            while (*e && *e != '\r' && *e != '\n') e++;
            n = (size_t)(e - q);
            while (n > 0 && (q[n - 1] == ' ' || q[n - 1] == '\t')) n--;
            if (n >= cap) n = cap - 1;
            memcpy(out, q, n);
            out[n] = '\0';
            return 1;
        }
        p = q;
    }
    return 0;
}

static int parse_major(const char* version) {
    if (version[0] == '1' && version[1] == '.') return atoi(version + 2);
    return atoi(version);
}

int java_probe(const char* java_exe, JavaEntry* out, int required_major) {
    char exe[1024];
    char text[16384];
    char version[64];
    char home[1024];
    char vendor[128];
    char arch[32];
    wchar_t* exe_w;
    wchar_t cmd[2048];

    if (!java_exe || !java_exe[0] || !out) return 0;
    memset(out, 0, sizeof(*out));
    copy_str(exe, sizeof(exe), java_exe);

    // javaw.exe 没有输出，统一换成同目录的 java.exe
    if (_stricmp(path_basename(exe), "javaw.exe") == 0) {
        char* base = (char*)path_basename(exe);
        snprintf(base, sizeof(exe) - (size_t)(base - exe), "java.exe");
    }
    if (!file_exists_a(exe)) return 0;

    exe_w = utf8_to_wide(exe);
    if (!exe_w) return 0;
    swprintf(cmd, 2048, L"\"%ls\" -XshowSettings:properties -version", exe_w);
    if (!run_capture(exe_w, cmd, text, sizeof(text), PROBE_TIMEOUT_MS)) {
        free(exe_w);
        return 0;
    }
    free(exe_w);

    if (!find_prop(text, "java.version", version, sizeof(version))) return 0;
    find_prop(text, "java.home", home, sizeof(home));
    find_prop(text, "java.vendor", vendor, sizeof(vendor));
    find_prop(text, "os.arch", arch, sizeof(arch));

    copy_str(out->exe, sizeof(out->exe), exe);
    copy_str(out->version, sizeof(out->version), version);
    copy_str(out->vendor, sizeof(out->vendor), vendor[0] ? vendor : "未知厂商");
    copy_str(out->arch, sizeof(out->arch), arch[0] ? arch : "unknown");
    out->major = parse_major(version);
    out->bits = (_stricmp(arch, "x86") == 0 || _stricmp(arch, "i386") == 0) ? 32 : 64;

    if (home[0]) {
        copy_str(out->home, sizeof(out->home), home);
    }
    else {
        char dir[1024];
        path_dirname(exe, dir, sizeof(dir));
        path_dirname(dir, out->home, sizeof(out->home));
    }

    {
        char javaw[1024];
        char* base;
        copy_str(javaw, sizeof(javaw), exe);
        base = (char*)path_basename(javaw);
        snprintf(base, sizeof(javaw) - (size_t)(base - javaw), "javaw.exe");
        copy_str(out->javaw, sizeof(out->javaw),
                 file_exists_a(javaw) ? javaw : exe);
    }

    if (out->bits == 32) {
        out->state = JAVA_WRONG_ARCH;
        out->score = 100;
        copy_str(out->note, sizeof(out->note), "32 位 Java，无法运行 64 位游戏");
    }
    else if (required_major > 0 && out->major < required_major) {
        out->state = JAVA_OLD;
        out->score = 400 + out->major;
        snprintf(out->note, sizeof(out->note), "低于游戏所需的 Java %d", required_major);
    }
    else if (required_major > 0 && out->major > required_major) {
        out->state = JAVA_NEWER;
        out->score = 800 + out->major;
        snprintf(out->note, sizeof(out->note), "高于所需的 Java %d，一般可用", required_major);
    }
    else {
        out->state = JAVA_OK;
        out->score = 1000 + out->major;
        copy_str(out->note, sizeof(out->note), required_major > 0 ? "推荐" : "可用");
    }
    return 1;
}

typedef struct {
    JavaEntry* out;
    int max;
    int count;
    int required;
    int budget;
    char seen[128][1024];
    int seen_count;
} JavaCtx;

static int already_seen(JavaCtx* ctx, const char* exe) {
    int i;
    for (i = 0; i < ctx->seen_count; i++) {
        if (_stricmp(ctx->seen[i], exe) == 0) return 1;
    }
    return 0;
}

static void remember_exe(JavaCtx* ctx, const char* exe) {
    if (ctx->seen_count < (int)(sizeof(ctx->seen) / sizeof(ctx->seen[0]))) {
        copy_str(ctx->seen[ctx->seen_count++], sizeof(ctx->seen[0]), exe);
    }
}

static void add_candidate(JavaCtx* ctx, const char* home_dir) {
    char exe[1024];
    JavaEntry entry;
    int i;

    if (ctx->count >= ctx->max) return;

    snprintf(exe, sizeof(exe), "%s\\bin\\java.exe", home_dir);
    if (!file_exists_a(exe)) return;
    if (already_seen(ctx, exe)) return;
    remember_exe(ctx, exe);

    if (!java_probe(exe, &entry, ctx->required)) return;

    // 同一个 java.home 只保留一份（javapath 之类会重复指向同一个 JDK）
    for (i = 0; i < ctx->count; i++) {
        if (entry.home[0] && _stricmp(ctx->out[i].home, entry.home) == 0) return;
    }
    ctx->out[ctx->count++] = entry;
}

// PATH 里找到的是 ...\bin\java.exe，去掉一层 bin
static void add_from_exe(JavaCtx* ctx, const char* exe) {
    char dir[1024];
    char parent[1024];

    path_dirname(exe, dir, sizeof(dir));
    if (_stricmp(path_basename(dir), "bin") == 0) {
        path_dirname(dir, parent, sizeof(parent));
        add_candidate(ctx, parent);
    }
    else {
        add_candidate(ctx, dir);
    }
}

static void scan_dir(JavaCtx* ctx, const char* root, int depth) {
    char pattern[1024];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    if (depth < 0 || ctx->budget <= 0) return;
    ctx->budget--;

    add_candidate(ctx, root);

    snprintf(pattern, sizeof(pattern), "%s\\*", root);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        char full[1024];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (ctx->budget <= 0) break;

        snprintf(full, sizeof(full), "%s\\%s", root, fd.cFileName);
        scan_dir(ctx, full, depth - 1);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

static void read_reg_string(HKEY hive, const char* subkey, const char* value, JavaCtx* ctx) {
    HKEY key;
    char buf[1024];
    DWORD len = sizeof(buf);
    DWORD type = 0;

    if (RegOpenKeyExA(hive, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExA(key, value, NULL, &type, (LPBYTE)buf, &len) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) &&
        len > 0 && len < sizeof(buf)) {
        buf[len] = '\0';
        add_candidate(ctx, buf);
    }
    RegCloseKey(key);
}

static void enum_javasoft(HKEY hive, const char* base, JavaCtx* ctx) {
    HKEY key;
    DWORD index = 0;
    char name[256];

    if (RegOpenKeyExA(hive, base, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    for (;;) {
        char subkey[1024];
        DWORD name_len = sizeof(name) - 1;
        if (RegEnumKeyExA(key, index++, name, &name_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        name[name_len] = '\0';
        snprintf(subkey, sizeof(subkey), "%s\\%s", base, name);
        read_reg_string(hive, subkey, "JavaHome", ctx);
    }
    RegCloseKey(key);
}

static void enum_adoptium(HKEY hive, const char* base, JavaCtx* ctx) {
    HKEY key;
    DWORD index = 0;
    char name[256];

    if (RegOpenKeyExA(hive, base, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    for (;;) {
        char subkey[1024];
        DWORD name_len = sizeof(name) - 1;
        if (RegEnumKeyExA(key, index++, name, &name_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        name[name_len] = '\0';
        snprintf(subkey, sizeof(subkey), "%s\\%s\\hotspot\\MSI", base, name);
        read_reg_string(hive, subkey, "Path", ctx);
    }
    RegCloseKey(key);
}

static void scan_env_dir(JavaCtx* ctx, const char* env_name, const char* suffix, int depth) {
    char base[1024];
    char path[2048];

    if (GetEnvironmentVariableA(env_name, base, sizeof(base)) == 0) return;
    snprintf(path, sizeof(path), "%s\\%s", base, suffix);
    scan_dir(ctx, path, depth);
}

static int compare_java(const void* l, const void* r) {
    const JavaEntry* a = (const JavaEntry*)l;
    const JavaEntry* b = (const JavaEntry*)r;
    if (a->score != b->score) return b->score - a->score;
    if (a->major != b->major) return b->major - a->major;
    return _stricmp(a->exe, b->exe);
}

void java_sort(JavaEntry* list, int count) {
    if (count > 1) qsort(list, (size_t)count, sizeof(JavaEntry), compare_java);
}

const char* java_state_text(int state) {
    switch (state) {
    case JAVA_OK:         return "推荐";
    case JAVA_NEWER:      return "可用（较新）";
    case JAVA_OLD:        return "版本过低";
    case JAVA_WRONG_ARCH: return "32 位";
    case JAVA_BROKEN:     return "无法运行";
    default:              return "未知";
    }
}

int java_scan(JavaEntry* out, int max, int required_major) {
    JavaCtx ctx;
    char buf[2048];
    HKEY hives[2];
    const char* javasoft[] = {
        "SOFTWARE\\JavaSoft\\JDK",
        "SOFTWARE\\JavaSoft\\JRE",
        "SOFTWARE\\JavaSoft\\Java Development Kit",
        "SOFTWARE\\JavaSoft\\Java Runtime Environment",
        "SOFTWARE\\WOW6432Node\\JavaSoft\\JDK",
        "SOFTWARE\\WOW6432Node\\JavaSoft\\JRE"
    };
    const char* adoptium[] = {
        "SOFTWARE\\Eclipse Adoptium\\JDK",
        "SOFTWARE\\Eclipse Foundation\\JDK",
        "SOFTWARE\\WOW6432Node\\Eclipse Adoptium\\JDK"
    };
    int i, h;

    if (!out || max <= 0) return 0;

    ZeroMemory(&ctx, sizeof(ctx));
    ctx.out = out;
    ctx.max = max;
    ctx.required = required_major;
    ctx.budget = 2000;

    // 1. 环境变量
    if (GetEnvironmentVariableA("JAVA_HOME", buf, sizeof(buf)) > 0) {
        add_candidate(&ctx, buf);
    }
    if (SearchPathA(NULL, "java.exe", NULL, sizeof(buf), buf, NULL)) {
        add_from_exe(&ctx, buf);
    }

    // 2. 注册表
    hives[0] = HKEY_LOCAL_MACHINE;
    hives[1] = HKEY_CURRENT_USER;
    for (h = 0; h < 2; h++) {
        for (i = 0; i < (int)(sizeof(javasoft) / sizeof(javasoft[0])); i++) {
            enum_javasoft(hives[h], javasoft[i], &ctx);
        }
        for (i = 0; i < (int)(sizeof(adoptium) / sizeof(adoptium[0])); i++) {
            enum_adoptium(hives[h], adoptium[i], &ctx);
        }
    }

    // 3. 常见安装目录
    scan_env_dir(&ctx, "ProgramFiles", "Java", 2);
    scan_env_dir(&ctx, "ProgramFiles", "Eclipse Adoptium", 2);
    scan_env_dir(&ctx, "ProgramFiles", "Eclipse Foundation", 2);
    scan_env_dir(&ctx, "ProgramFiles", "Microsoft", 2);
    scan_env_dir(&ctx, "ProgramFiles", "Amazon Corretto", 2);
    scan_env_dir(&ctx, "ProgramFiles", "Zulu", 2);
    scan_env_dir(&ctx, "ProgramFiles", "BellSoft", 3);
    scan_env_dir(&ctx, "ProgramFiles", "Semeru", 2);
    scan_env_dir(&ctx, "ProgramFiles", "IBM", 3);
    scan_env_dir(&ctx, "ProgramFiles", "GraalVM", 3);
    scan_env_dir(&ctx, "ProgramFiles", "JetBrains", 4);
    scan_env_dir(&ctx, "ProgramFiles(x86)", "Java", 2);
    scan_env_dir(&ctx, "LOCALAPPDATA", "Programs\\Eclipse Adoptium", 2);
    scan_env_dir(&ctx, "USERPROFILE", ".jdks", 2);
    scan_env_dir(&ctx, "ProgramFiles", "Android\\Android Studio\\jbr", 1);

    java_sort(out, ctx.count);
    return ctx.count;
}

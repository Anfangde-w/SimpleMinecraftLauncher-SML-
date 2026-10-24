#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "http_client.h"
#include "text_util.h"
#include "file_util.h"
#include "sha1_util.h"

#pragma comment(lib, "winhttp.lib")

#define READ_CHUNK 16384

static char g_error[1024];

const char* http_last_error(void) {
    return g_error[0] ? g_error : "未知网络错误";
}

static void set_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof(g_error), fmt, ap);
    va_end(ap);
    g_error[sizeof(g_error) - 1] = '\0';
}

typedef struct {
    wchar_t host[256];
    wchar_t path[2048];
    INTERNET_PORT port;
    int secure;
} UrlParts;

static int parse_url(const char* url, UrlParts* parts) {
    wchar_t* wurl;
    wchar_t scheme[32];
    wchar_t path_buf[2048];
    wchar_t extra[1024];
    URL_COMPONENTS uc;
    int ok;

    if (!url || !parts) return 0;
    wurl = utf8_to_wide(url);
    if (!wurl) return 0;

    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszScheme = scheme;
    uc.dwSchemeLength = (DWORD)(sizeof(scheme) / sizeof(scheme[0]));
    uc.lpszHostName = parts->host;
    uc.dwHostNameLength = (DWORD)(sizeof(parts->host) / sizeof(parts->host[0]));
    uc.lpszUrlPath = path_buf;
    uc.dwUrlPathLength = (DWORD)(sizeof(path_buf) / sizeof(path_buf[0]));
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = (DWORD)(sizeof(extra) / sizeof(extra[0]));

    ok = WinHttpCrackUrl(wurl, 0, 0, &uc);
    free(wurl);
    if (!ok) {
        set_error("无法解析 URL: %s", url);
        return 0;
    }

    parts->secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    parts->port = uc.nPort ? uc.nPort : (parts->secure ? 443 : 80);

    wcsncpy(parts->path, path_buf, 2047);
    parts->path[2047] = L'\0';
    if (extra[0]) {
        size_t used = wcslen(parts->path);
        wcsncat(parts->path, extra, 2047 - used);
    }
    if (parts->path[0] == L'\0') wcscpy(parts->path, L"/");
    return 1;
}

typedef int (*BodySink)(void* ctx, const void* data, DWORD len);

typedef struct {
    HANDLE file;
    long long* total;
    long long done;
    HttpProgressFn cb;
    void* user;
    DWORD last_tick;
} FileSink;

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} TextSink;

static int text_sink(void* ctx, const void* data, DWORD len) {
    TextSink* s = (TextSink*)ctx;
    if (s->len + len + 1 > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 8192;
        char* p;
        while (cap < s->len + len + 1) cap *= 2;
        p = (char*)realloc(s->data, cap);
        if (!p) return 0;
        s->data = p;
        s->cap = cap;
    }
    memcpy(s->data + s->len, data, len);
    s->len += len;
    s->data[s->len] = '\0';
    return 1;
}

static int file_sink(void* ctx, const void* data, DWORD len) {
    FileSink* s = (FileSink*)ctx;
    DWORD written = 0;

    if (!WriteFile(s->file, data, len, &written, NULL) || written != len) {
        set_error("写入文件失败（磁盘空间不足？）");
        return 0;
    }
    s->done += written;
    if (s->cb) {
        DWORD now = GetTickCount();
        if (now - s->last_tick > 100 || (s->total && *s->total > 0 && s->done >= *s->total)) {
            s->last_tick = now;
            s->cb(s->user, s->done, s->total ? *s->total : -1);
        }
    }
    return 1;
}

// 通用 GET：把响应体交给 sink。*total_io 用于传入/返回总字节数（-1 表示未知）
static int http_request(const char* url, BodySink sink, void* ctx, long long* total_io) {
    UrlParts parts;
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    DWORD content_len = 0;
    DWORD content_len_size = sizeof(content_len);
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    char* buffer = NULL;
    int result = -1;

    g_error[0] = '\0';
    if (!parse_url(url, &parts)) return -1;

    session = WinHttpOpen(L"SML Launcher/0.1",
                          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        set_error("初始化网络失败（错误码 %lu）", GetLastError());
        goto done;
    }

    WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(secure_protocols));

    connect = WinHttpConnect(session, parts.host, parts.port, 0);
    if (!connect) {
        set_error("连接服务器失败：%ls（错误码 %lu）", parts.host, GetLastError());
        goto done;
    }

    request = WinHttpOpenRequest(connect, L"GET", parts.path, NULL,
                                 WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        set_error("创建请求失败（错误码 %lu）", GetLastError());
        goto done;
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            NULL, 0, 0, 0)) {
        set_error("发送请求失败（错误码 %lu）", GetLastError());
        goto done;
    }
    if (!WinHttpReceiveResponse(request, NULL)) {
        set_error("接收响应失败（错误码 %lu）", GetLastError());
        goto done;
    }

    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status,
                             &status_size, WINHTTP_NO_HEADER_INDEX)) {
        set_error("读取 HTTP 状态码失败");
        goto done;
    }
    if (status < 200 || status >= 300) {
        set_error("服务器返回 HTTP %lu", status);
        goto done;
    }

    if (WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &content_len,
                            &content_len_size, WINHTTP_NO_HEADER_INDEX)) {
        if (total_io && *total_io <= 0) *total_io = (long long)content_len;
    }
    else if (total_io && *total_io <= 0) {
        *total_io = -1;
    }

    buffer = (char*)malloc(READ_CHUNK);
    if (!buffer) {
        set_error("内存不足");
        goto done;
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            set_error("读取数据失败（错误码 %lu）", GetLastError());
            goto done;
        }
        if (avail == 0) break;

        while (avail > 0) {
            DWORD want = avail > READ_CHUNK ? READ_CHUNK : avail;
            DWORD got = 0;
            if (!WinHttpReadData(request, buffer, want, &got)) {
                set_error("读取数据失败（错误码 %lu）", GetLastError());
                goto done;
            }
            if (got == 0) break;
            if (sink && !sink(ctx, buffer, got)) goto done;
            avail -= got;
        }
    }

    result = 0;

done:
    free(buffer);
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return result;
}

int http_get_text(const char* url, char** out_text) {
    TextSink sink;
    long long total = -1;

    if (!out_text) return -1;
    *out_text = NULL;

    ZeroMemory(&sink, sizeof(sink));
    if (http_request(url, text_sink, &sink, &total) != 0) {
        free(sink.data);
        return -1;
    }
    if (!sink.data) {
        sink.data = (char*)malloc(1);
        if (!sink.data) return -1;
        sink.data[0] = '\0';
    }
    *out_text = sink.data;
    return 0;
}

int http_download_file(const char* url, const char* dest_path,
                       long long expected_size, const char* expected_sha1,
                       HttpProgressFn cb, void* user) {
    wchar_t* wdest;
    HANDLE file;
    FileSink sink;
    long long total = expected_size > 0 ? expected_size : -1;
    int result;

    if (!url || !dest_path) return -1;
    g_error[0] = '\0';

    {
        char parent[1024];
        size_t len;
        snprintf(parent, sizeof(parent), "%s", dest_path);
        len = strlen(parent);
        while (len > 0 && parent[len - 1] != '\\' && parent[len - 1] != '/') len--;
        if (len > 0) {
            parent[len - 1] = '\0';
            ensure_dir_utf8(parent);
        }
    }

    wdest = utf8_to_wide(dest_path);
    if (!wdest) return -1;

    // 已有文件可能是只读的，先清掉属性再覆盖
    {
        DWORD attr = GetFileAttributesW(wdest);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(wdest, attr & ~FILE_ATTRIBUTE_READONLY);
        }
    }

    file = CreateFileW(wdest, GENERIC_WRITE, 0, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        set_error("无法创建文件：%s（错误码 %lu）", dest_path, GetLastError());
        free(wdest);
        return -1;
    }

    ZeroMemory(&sink, sizeof(sink));
    sink.file = file;
    sink.total = &total;
    sink.cb = cb;
    sink.user = user;

    result = http_request(url, file_sink, &sink, &total);
    CloseHandle(file);

    if (result != 0) {
        DeleteFileW(wdest);
        free(wdest);
        return -1;
    }

    if (expected_size > 0 && sink.done != expected_size) {
        set_error("文件大小不符：期望 %lld，实际 %lld", expected_size, sink.done);
        DeleteFileW(wdest);
        free(wdest);
        return -1;
    }

    if (expected_sha1 && expected_sha1[0]) {
        char actual[41];
        if (!sha1_file_hex(dest_path, actual)) {
            set_error("无法计算文件 SHA1：%s", dest_path);
            DeleteFileW(wdest);
            free(wdest);
            return -1;
        }
        if (_stricmp(actual, expected_sha1) != 0) {
            set_error("SHA1 校验失败：%s", dest_path);
            DeleteFileW(wdest);
            free(wdest);
            return -1;
        }
    }

    free(wdest);
    return 0;
}

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "download_game.h"
#include "http_client.h"
#include "sha1_util.h"
#include "file_util.h"
#include "platform_rules.h"

#define MANIFEST_URL "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
#define ASSET_BASE   "https://resources.download.minecraft.net/"

static const char* json_str(cJSON* obj, const char* key) {
    cJSON* v = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static long long json_size(cJSON* obj) {
    cJSON* v = obj ? cJSON_GetObjectItemCaseSensitive(obj, "size") : NULL;
    return cJSON_IsNumber(v) ? (long long)v->valuedouble : 0;
}

static void copy_str(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

// rel 用 '/'，转成 Windows 路径
static void rel_to_path(const char* mc_dir, const char* rel, char* out, size_t cap) {
    size_t i;
    snprintf(out, cap, "%s\\", mc_dir);
    for (i = strlen(out); *rel && i + 1 < cap; rel++, i++) {
        out[i] = (*rel == '/') ? '\\' : *rel;
    }
    out[i] = '\0';
}

// 只建目录（不把最后一段当目录）
static void ensure_parent_dir(const char* file_path) {
    char parent[1024];
    size_t len;

    snprintf(parent, sizeof(parent), "%s", file_path);
    len = strlen(parent);
    while (len > 0 && parent[len - 1] != '\\' && parent[len - 1] != '/') len--;
    if (len > 0) {
        parent[len - 1] = '\0';
        ensure_dir_utf8(parent);
    }
}

static int load_manifest_text(const char* mc_dir, char** out_text) {
    char cache_path[1024];
    char* text = NULL;
    char* cached = NULL;
    FILE* f;

    snprintf(cache_path, sizeof(cache_path), "%s\\versions\\version_manifest_v2.json", mc_dir);

    if (http_get_text(MANIFEST_URL, &text) == 0) {
        ensure_parent_dir(cache_path);
        f = fopen(cache_path, "wb");
        if (f) {
            fwrite(text, 1, strlen(text), f);
            fclose(f);
        }
        *out_text = text;
        return 1;
    }

    cached = read_file(cache_path);
    if (cached) {
        *out_text = cached;
        return 1;
    }
    return 0;
}

int download_fetch_versions(const char* mc_dir, RemoteVersion** out_list, int* out_count,
                            char* latest_release, size_t lr_cap,
                            char* latest_snapshot, size_t ls_cap) {
    char* text = NULL;
    cJSON* root;
    cJSON* latest;
    cJSON* versions;
    int count, i;

    *out_list = NULL;
    *out_count = 0;
    if (latest_release && lr_cap) latest_release[0] = '\0';
    if (latest_snapshot && ls_cap) latest_snapshot[0] = '\0';

    if (!load_manifest_text(mc_dir, &text)) return 0;

    root = cJSON_Parse(text);
    free(text);
    if (!root) return 0;

    latest = cJSON_GetObjectItemCaseSensitive(root, "latest");
    copy_str(latest_release, lr_cap, json_str(latest, "release"));
    copy_str(latest_snapshot, ls_cap, json_str(latest, "snapshot"));

    versions = cJSON_GetObjectItemCaseSensitive(root, "versions");
    if (!cJSON_IsArray(versions)) {
        cJSON_Delete(root);
        return 0;
    }

    count = cJSON_GetArraySize(versions);
    if (count <= 0) {
        cJSON_Delete(root);
        return 0;
    }

    *out_list = (RemoteVersion*)calloc((size_t)count, sizeof(RemoteVersion));
    if (!*out_list) {
        cJSON_Delete(root);
        return 0;
    }

    for (i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(versions, i);
        RemoteVersion* v = &(*out_list)[i];
        copy_str(v->id, sizeof(v->id), json_str(item, "id"));
        copy_str(v->type, sizeof(v->type), json_str(item, "type"));
        copy_str(v->release_time, sizeof(v->release_time), json_str(item, "releaseTime"));
        copy_str(v->url, sizeof(v->url), json_str(item, "url"));
        copy_str(v->sha1, sizeof(v->sha1), json_str(item, "sha1"));
        v->size = json_size(item);
    }
    *out_count = count;

    cJSON_Delete(root);
    return 1;
}

void download_free_versions(RemoteVersion* list) {
    free(list);
}

typedef struct {
    DownloadProgressFn fn;
    void* user;
    long long base_done;
    long long inflight;
    long long bytes_total;
    int index;
    int file_count;
    int skipped;
    const char* current;
    DWORD last;
    int stage;
    const char* prep_label;
    long long prep_done;
    long long prep_total;
} DlCtx;

static void report(DlCtx* c, int force) {
    DownloadProgress p;
    DWORD now = GetTickCount();

    if (!c->fn) return;
    if (!force && now - c->last < 120) return;
    c->last = now;

    p.stage = c->stage;
    if (c->stage == DL_STAGE_PREPARE) {
        p.file_index = 0;
        p.file_count = 0;
        p.skipped = 0;
        p.file_label = c->prep_label;
        p.bytes_done = c->prep_done;
        p.bytes_total = c->prep_total;
    }
    else {
        p.file_index = c->index;
        p.file_count = c->file_count;
        p.skipped = c->skipped;
        p.file_label = c->current;
        p.bytes_done = c->base_done + c->inflight;
        p.bytes_total = c->bytes_total;
    }
    c->fn(c->user, &p);
}

static void http_progress(void* user, long long done, long long total) {
    DlCtx* c = (DlCtx*)user;
    c->inflight = done;
    report(c, 0);
}

static void http_prep_progress(void* user, long long done, long long total) {
    DlCtx* c = (DlCtx*)user;
    c->prep_done = done;
    if (total > 0) c->prep_total = total;
    report(c, 0);
}

// 校验并下载一个文件
static int process_file(DlCtx* c, const char* mc_dir, const char* rel,
                        const char* url, const char* sha1, long long size,
                        char* message, size_t message_cap) {
    char dest[2048];
    int existed;

    c->index++;
    c->current = rel;
    c->inflight = 0;

    if (!url || !url[0]) {
        snprintf(message, message_cap, "下载信息不完整：%s", rel);
        return 0;
    }

    rel_to_path(mc_dir, rel, dest, sizeof(dest));

    existed = file_matches_sha1(dest, sha1, size);
    if (existed) {
        c->skipped++;
        c->base_done += size > 0 ? size : 0;
        report(c, 1);
        return 1;
    }

    ensure_parent_dir(dest);
    report(c, 1);

    if (http_download_file(url, dest, size, sha1, http_progress, c) != 0) {
        snprintf(message, message_cap, "下载失败：%s（%s）", rel, http_last_error());
        return 0;
    }

    c->base_done += size > 0 ? size : file_size_utf8(dest);
    c->inflight = 0;
    report(c, 1);
    return 1;
}

static int fetch_text_to_file(const char* mc_dir, const char* rel, const char* url,
                              const char* sha1, long long size, char** out_text,
                              DlCtx* ctx, const char* label,
                              char* message, size_t message_cap) {
    char dest[2048];

    ctx->stage = DL_STAGE_PREPARE;
    ctx->prep_label = label;
    ctx->prep_done = 0;
    ctx->prep_total = size > 0 ? size : -1;
    report(ctx, 1);

    rel_to_path(mc_dir, rel, dest, sizeof(dest));
    if (!file_matches_sha1(dest, sha1, size)) {
        ensure_parent_dir(dest);
        if (http_download_file(url, dest, size, sha1, http_prep_progress, ctx) != 0) {
            snprintf(message, message_cap, "下载失败：%s（%s）", rel, http_last_error());
            return 0;
        }
    }

    ctx->prep_done = size > 0 ? size : 0;
    ctx->prep_total = size > 0 ? size : 0;
    report(ctx, 1);

    *out_text = read_file(dest);
    if (!*out_text) {
        snprintf(message, message_cap, "无法读取：%s", rel);
        return 0;
    }
    return 1;
}

int download_game_run(const char* mc_dir, const RemoteVersion* version,
                      DownloadProgressFn progress, void* user,
                      char* message, size_t message_cap) {
    char* vtext = NULL;
    char* atext = NULL;
    cJSON* vroot = NULL;
    cJSON* aroot = NULL;
    cJSON* client;
    cJSON* libraries;
    cJSON* logging;
    cJSON* asset_index;
    cJSON* objects;
    char version_rel[512];
    char client_rel[512];
    char index_rel[512];
    char log_rel[512];
    const char* lib_path;
    const char* lib_url;
    const char* lib_sha1;
    long long lib_size;
    DlCtx ctx;
    int ok = 0;

    if (message_cap) message[0] = '\0';

    ZeroMemory(&ctx, sizeof(ctx));
    ctx.fn = progress;
    ctx.user = user;
    ctx.stage = DL_STAGE_PREPARE;

    snprintf(version_rel, sizeof(version_rel), "versions/%s/%s.json", version->id, version->id);

    if (!fetch_text_to_file(mc_dir, version_rel, version->url, version->sha1, version->size,
                            &vtext, &ctx, "正在获取版本信息…", message, message_cap)) {
        return 0;
    }

    vroot = cJSON_Parse(vtext);
    free(vtext);
    if (!vroot) {
        snprintf(message, message_cap, "版本 JSON 无法解析");
        return 0;
    }

    asset_index = cJSON_GetObjectItemCaseSensitive(vroot, "assetIndex");
    if (asset_index) {
        const char* ai_id = json_str(asset_index, "id");
        const char* ai_url = json_str(asset_index, "url");
        const char* ai_sha1 = json_str(asset_index, "sha1");
        long long ai_size = json_size(asset_index);

        if (!ai_id || !ai_id[0]) {
            snprintf(message, message_cap, "版本 JSON 缺少资源索引 id");
            goto done;
        }
        snprintf(index_rel, sizeof(index_rel), "assets/indexes/%s.json", ai_id);
        if (!fetch_text_to_file(mc_dir, index_rel, ai_url, ai_sha1, ai_size,
                                &atext, &ctx, "正在获取资源索引…", message, message_cap)) {
            goto done;
        }
        aroot = cJSON_Parse(atext);
        free(atext);
        atext = NULL;
    }

    // ---- 统计总数 ----
    ctx.file_count = 1;   // 版本 json
    ctx.bytes_total = 0;

    client = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(vroot, "downloads"), "client");
    if (client) {
        ctx.file_count++;
        ctx.bytes_total += json_size(client);
    }

    libraries = cJSON_GetObjectItemCaseSensitive(vroot, "libraries");
    if (cJSON_IsArray(libraries)) {
        cJSON* lib = NULL;
        cJSON_ArrayForEach(lib, libraries) {
            cJSON* downloads;
            cJSON* artifact;
            if (!rules_allow(lib)) continue;
            downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");
            artifact = downloads ? cJSON_GetObjectItemCaseSensitive(downloads, "artifact") : NULL;
            if (!artifact || !json_str(artifact, "path") || !json_str(artifact, "url")) continue;
            ctx.file_count++;
            ctx.bytes_total += json_size(artifact);
        }
    }

    logging = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(vroot, "logging"), "client");
    if (logging) {
        cJSON* file = cJSON_GetObjectItemCaseSensitive(logging, "file");
        if (file && json_str(file, "url") && json_str(file, "id")) {
            ctx.file_count++;
            ctx.bytes_total += json_size(file);
        }
    }

    if (asset_index) {
        ctx.file_count++;
        ctx.bytes_total += json_size(asset_index);
    }

    objects = aroot ? cJSON_GetObjectItemCaseSensitive(aroot, "objects") : NULL;
    if (objects) {
        cJSON* obj = NULL;
        cJSON_ArrayForEach(obj, objects) {
            if (!obj || !obj->valuestring) continue;
            ctx.file_count++;
            ctx.bytes_total += json_size(obj);
        }
    }

    ctx.stage = DL_STAGE_FILES;
    ctx.last = 0;
    ctx.index = 0;
    ctx.base_done = 0;
    ctx.inflight = 0;
    report(&ctx, 1);

    // ---- 逐个校验 / 下载 ----
    if (!process_file(&ctx, mc_dir, version_rel, version->url, version->sha1, version->size,
                      message, message_cap)) {
        goto done;
    }

    if (client) {
        snprintf(client_rel, sizeof(client_rel), "versions/%s/%s.jar", version->id, version->id);
        if (!process_file(&ctx, mc_dir, client_rel, json_str(client, "url"),
                          json_str(client, "sha1"), json_size(client), message, message_cap)) {
            goto done;
        }
    }

    if (cJSON_IsArray(libraries)) {
        cJSON* lib = NULL;
        cJSON_ArrayForEach(lib, libraries) {
            cJSON* downloads;
            cJSON* artifact;
            char rel[1024];
            if (!rules_allow(lib)) continue;
            downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");
            artifact = downloads ? cJSON_GetObjectItemCaseSensitive(downloads, "artifact") : NULL;
            if (!artifact) continue;
            lib_path = json_str(artifact, "path");
            lib_url = json_str(artifact, "url");
            lib_sha1 = json_str(artifact, "sha1");
            lib_size = json_size(artifact);
            if (!lib_path || !lib_url) continue;
            snprintf(rel, sizeof(rel), "libraries/%s", lib_path);
            if (!process_file(&ctx, mc_dir, rel, lib_url, lib_sha1, lib_size,
                              message, message_cap)) {
                goto done;
            }
        }
    }

    if (logging) {
        cJSON* file = cJSON_GetObjectItemCaseSensitive(logging, "file");
        const char* id = json_str(file, "id");
        if (file && id && json_str(file, "url")) {
            snprintf(log_rel, sizeof(log_rel), "assets/log_configs/%s", id);
            if (!process_file(&ctx, mc_dir, log_rel, json_str(file, "url"),
                              json_str(file, "sha1"), json_size(file),
                              message, message_cap)) {
                goto done;
            }
        }
    }

    if (asset_index) {
        const char* ai_id = json_str(asset_index, "id");
        snprintf(index_rel, sizeof(index_rel), "assets/indexes/%s.json", ai_id);
        if (!process_file(&ctx, mc_dir, index_rel, json_str(asset_index, "url"),
                          json_str(asset_index, "sha1"), json_size(asset_index),
                          message, message_cap)) {
            goto done;
        }
    }

    if (objects) {
        cJSON* obj = NULL;
        cJSON_ArrayForEach(obj, objects) {
            const char* hash = obj ? json_str(obj, "hash") : NULL;
            long long size = json_size(obj);
            char rel[1024];
            char url[1024];
            if (!hash || strlen(hash) < 3) continue;
            snprintf(rel, sizeof(rel), "assets/objects/%.2s/%s", hash, hash);
            snprintf(url, sizeof(url), ASSET_BASE "%.2s/%s", hash, hash);
            if (!process_file(&ctx, mc_dir, rel, url, hash, size, message, message_cap)) {
                goto done;
            }
        }
    }

    ctx.base_done = ctx.bytes_total;
    ctx.inflight = 0;
    ctx.index = ctx.file_count;
    report(&ctx, 1);

    snprintf(message, message_cap, "下载完成：共 %d 个文件，跳过 %d 个（已存在且校验通过）",
             ctx.file_count, ctx.skipped);
    ok = 1;

done:
    cJSON_Delete(vroot);
    cJSON_Delete(aroot);
    return ok;
}

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include "sha1_util.h"
#include "text_util.h"

#pragma comment(lib, "advapi32.lib")

int sha1_file_hex(const char* path, char out_hex[41]) {
    wchar_t* wpath;
    HANDLE file;
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    BYTE digest[20];
    DWORD digest_len = sizeof(digest);
    BYTE buffer[65536];
    DWORD got = 0;
    int ok = 0;
    int i;

    if (!path || !out_hex) return 0;

    wpath = utf8_to_wide(path);
    if (!wpath) return 0;
    file = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (file == INVALID_HANDLE_VALUE) return 0;

    if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        CloseHandle(file);
        return 0;
    }
    if (!CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        CloseHandle(file);
        return 0;
    }

    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, NULL)) break;
        if (got == 0) { ok = 1; break; }
        if (!CryptHashData(hash, buffer, got, 0)) { ok = 0; break; }
    }

    if (ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_len, 0)) {
        for (i = 0; i < 20; i++) {
            sprintf(out_hex + i * 2, "%02x", digest[i]);
        }
        out_hex[40] = '\0';
    }
    else {
        ok = 0;
    }

    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    CloseHandle(file);
    return ok;
}

int file_matches_sha1(const char* path, const char* sha1_hex, long long expected_size) {
    wchar_t* wpath;
    HANDLE file;
    LARGE_INTEGER size;
    char actual[41];

    if (!path) return 0;

    wpath = utf8_to_wide(path);
    if (!wpath) return 0;
    file = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (file == INVALID_HANDLE_VALUE) return 0;

    if (expected_size > 0) {
        if (!GetFileSizeEx(file, &size) || size.QuadPart != expected_size) {
            CloseHandle(file);
            return 0;
        }
    }
    CloseHandle(file);

    if (!sha1_hex || !sha1_hex[0]) return 1;

    if (!sha1_file_hex(path, actual)) return 0;
    return _stricmp(actual, sha1_hex) == 0;
}

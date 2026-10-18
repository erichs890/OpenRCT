#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rct.h"
#include "hooks.h"

int rct_failures;

static uint32_t rng = 1;
uint32_t rnd(uint32_t n) { rng = rng * 1664525 + 1013904223; return (rng >> 8) % n; }

/* A fresh name every time: reusing one races with the pending delete of the last file. */
static const char *temp_path(const char *name)
{
    static char dir[MAX_PATH], path[MAX_PATH + 64];
    static unsigned n;
    if (!dir[0]) GetTempPathA(sizeof dir, dir);
    snprintf(path, sizeof path, "%srct_%lu_%u_%s", dir, GetCurrentProcessId(), n++, name);
    return path;
}

void *rct_temp_rw(void)
{
    return CreateFileA(temp_path("rw.bin"), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_FLAG_DELETE_ON_CLOSE, NULL);
}

size_t rct_file_contents(void *handle, void *buf, size_t cap)
{
    DWORD pos = SetFilePointer(handle, 0, NULL, FILE_CURRENT), got = 0;
    SetFilePointer(handle, 0, NULL, FILE_BEGIN);
    ReadFile(handle, buf, (DWORD)cap, &got, NULL);
    SetFilePointer(handle, pos, NULL, FILE_BEGIN);
    return got;
}

void *rct_temp_file(const void *data, size_t len)
{
    const char *path = temp_path("ro.bin");
    DWORD w;
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(h, data, (DWORD)len, &w, NULL);
    CloseHandle(h);
    return CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
}

void rct_harness_init(int argc, char **argv)
{
    const char *path = "original/RCT.unpacked.exe";
    int hook = -2;   /* none */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hook") && i + 1 < argc)
            hook = strcmp(argv[++i], "all") ? atoi(argv[i]) : -1;
        else
            path = argv[i];
    }
    RctImage img;
    const char *err = rct_load_image(path, &img);
    if (err) {
        printf("%s\n", err);
        exit(2);
    }
    if (hook != -2) {
        rct_install_hooks(hook);
        printf("hooks: %s\n", hook < 0 ? "all" : rct_hooks[hook].name);
    }
}

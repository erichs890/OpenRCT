#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"

#define BASE ((uint8_t *)0x400000)
#define IMAGE_END 0xC50000            /* end of .rctimg (runtime/image_space.S) */
#define NEOLITE_MAGIC 0x04181996
#define NEOLITE_HEADER_PTR 0xC4629F   /* file offset of the NeoLite header */
#define NEOLITE_DECOMPRESS 0xC47150   /* (src, dst, size, work) -> new src or 0 */
#define NEOLITE_SECTION 0xC47C57      /* stage 2, unpacked by NEOLITE_DECOMPRESS */
#define NEOLITE_VIRTUALALLOC 0xC46536 /* API pointers the stage 2 code calls */
#define NEOLITE_VIRTUALFREE 0xC4653A

typedef uint8_t *(__cdecl *decompress_fn)(uint8_t *src, uint8_t *dst, uint32_t size, void *work);
typedef void(__cdecl *section_fn)(const char *name, uint8_t **src, uint8_t **dst, uint32_t arg, uint32_t size,
                                  void *w1, void *w2, void *w3, uint32_t flag);

static uint32_t import_rva;

static const char *bind_imports(uint32_t rva)
{
    static char err[300];
    import_rva = rva;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(BASE + rva); d->Name; d++) {
        HMODULE m = LoadLibraryA((char *)BASE + d->Name);
        if (!m) {
            snprintf(err, sizeof err, "cannot load %s", (char *)BASE + d->Name);
            return err;
        }
        DWORD *thunk = (DWORD *)(BASE + d->FirstThunk);
        DWORD *names = d->OriginalFirstThunk ? (DWORD *)(BASE + d->OriginalFirstThunk) : thunk;
        for (; *names; names++, thunk++) {
            LPCSTR sym = IMAGE_SNAP_BY_ORDINAL32(*names) ? (LPCSTR)(uintptr_t)IMAGE_ORDINAL32(*names)
                                                        : (LPCSTR)((IMAGE_IMPORT_BY_NAME *)(BASE + *names))->Name;
            *thunk = (DWORD)(uintptr_t)GetProcAddress(m, sym);
        }
    }
    return NULL;
}

/* Mirror of the wrapper's loader (0xC465A9) minus its file mapping, relocation
   and protection steps; see docs/unpacking.md and tools/unpack.py. */
static const char *unpack_in_place(const uint8_t *raw, size_t len, const IMAGE_NT_HEADERS32 *nt, RctImage *out)
{
    uint32_t hoff = *(uint32_t *)NEOLITE_HEADER_PTR;
    if (hoff + 0x54 > len || *(uint32_t *)(raw + hoff) != NEOLITE_MAGIC)
        return "NeoLite header not found";
    const uint32_t *h = (const uint32_t *)(raw + hoff);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uint8_t *ep = BASE + nt->OptionalHeader.AddressOfEntryPoint;
    uint8_t *w1 = VirtualAlloc(NULL, 0x10004, MEM_COMMIT, PAGE_READWRITE);
    uint8_t *w2 = VirtualAlloc(NULL, 0x10000, MEM_COMMIT, PAGE_READWRITE);
    uint8_t *w3 = VirtualAlloc(NULL, 0x10000, MEM_COMMIT, PAGE_READWRITE);
    uint8_t *src = (uint8_t *)raw + hoff + h[0x1C / 4] * 0x10 + 0x54;

    src = ((decompress_fn)NEOLITE_DECOMPRESS)(src, ep + h[0x08 / 4], h[0x0C / 4], w1);
    if (src)
        src = ((decompress_fn)NEOLITE_DECOMPRESS)(src, ep + h[0x10 / 4], h[0x14 / 4], w1);
    if (!src)
        return "NeoLite stage 2 decompression failed";
    src += h[0x18 / 4];
    *(void **)NEOLITE_VIRTUALALLOC = (void *)VirtualAlloc;
    *(void **)NEOLITE_VIRTUALFREE = (void *)VirtualFree;
    for (uint32_t i = 0; i < h[0x1C / 4]; i++) {
        const uint32_t *e = h + (0x54 + i * 0x10) / 4;   /* {section, flags, size, arg} */
        uint8_t *dst = BASE + sec[e[0]].VirtualAddress + (e[0] == h[0x3C / 4] ? h[0x40 / 4] : 0);
        ((section_fn)NEOLITE_SECTION)("RCT.EXE", &src, &dst, e[3], e[2], w1, w2, w3, (e[1] >> 5) & 1);
    }
    VirtualFree(w1, 0, MEM_RELEASE);
    VirtualFree(w2, 0, MEM_RELEASE);
    VirtualFree(w3, 0, MEM_RELEASE);
    out->entry = (uint32_t)(uintptr_t)BASE + h[0x20 / 4];
    return bind_imports(h[0x24 / 4]);
}

const char *rct_load_image(const char *path, RctImage *out)
{
    static char err[MAX_PATH + 64];
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, sizeof err, "cannot open %s", path);
        return err;
    }
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *raw = malloc(len);
    size_t got = raw ? fread(raw, 1, len, f) : 0;
    fclose(f);
    if (got != len || len < 0x400) {
        free(raw);
        return "cannot read the game executable";
    }
    const IMAGE_NT_HEADERS32 *nt = (void *)(raw + ((IMAGE_DOS_HEADER *)raw)->e_lfanew);
    const IMAGE_OPTIONAL_HEADER32 *oh = &nt->OptionalHeader;
    if (nt->Signature != IMAGE_NT_SIGNATURE || oh->ImageBase != 0x400000 || oh->SizeOfHeaders != 0x1000 ||
        0x400000 + oh->SizeOfImage > IMAGE_END) {
        free(raw);
        return "not a supported RCT.EXE (unexpected PE layout)";
    }
    /* Sections only: this exe's own headers stay at 0x400000. */
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].PointerToRawData && sec[i].PointerToRawData < len) {
            size_t n = sec[i].SizeOfRawData;
            if (n > len - sec[i].PointerToRawData) n = len - sec[i].PointerToRawData;
            memcpy(BASE + sec[i].VirtualAddress, raw + sec[i].PointerToRawData, n);
        }
    }
    out->resource_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
    out->resource_size = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size;
    /* Wrapped iff the NeoLite header the stub points at is really there
       (tools/unpack.py output keeps the stub, but not the compressed overlay). */
    uint32_t hoff = *(uint32_t *)NEOLITE_HEADER_PTR;
    int packed = hoff + 4 <= len && *(uint32_t *)(raw + hoff) == NEOLITE_MAGIC;
    const char *e;
    if (packed) {
        e = unpack_in_place(raw, len, nt, out);
    } else {
        out->entry = 0x400000 + oh->AddressOfEntryPoint;
        e = bind_imports(oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    }
    free(raw);
    return e;
}

void **rct_import_slot(const char *name)
{
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(BASE + import_rva); import_rva && d->Name; d++) {
        DWORD *thunk = (DWORD *)(BASE + d->FirstThunk);
        DWORD *names = d->OriginalFirstThunk ? (DWORD *)(BASE + d->OriginalFirstThunk) : NULL;
        for (int i = 0; names && names[i]; i++)
            if (!IMAGE_SNAP_BY_ORDINAL32(names[i]) &&
                !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(BASE + names[i]))->Name, name))
                return (void **)&thunk[i];
    }
    return NULL;
}

void rct_use_game_resources(const RctImage *img)
{
    IMAGE_NT_HEADERS32 *nt = (void *)(BASE + ((IMAGE_DOS_HEADER *)BASE)->e_lfanew);
    IMAGE_DATA_DIRECTORY *d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    DWORD old;
    VirtualProtect(d, sizeof *d, PAGE_READWRITE, &old);
    d->VirtualAddress = img->resource_rva;
    d->Size = img->resource_size;
    VirtualProtect(d, sizeof *d, old, &old);
}

/* void rct_call(uint32_t addr, Regs *r) */
uint32_t rct_call_target __attribute__((used));
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _rct_call\n"
    "_rct_call:\n"
    "  push ebp\n  push ebx\n  push esi\n  push edi\n"
    "  mov eax, [esp+20]\n"
    "  mov [_rct_call_target], eax\n"
    "  mov eax, [esp+24]\n"
    "  push eax\n"
    "  mov ebx, [eax+4]\n  mov ecx, [eax+8]\n  mov edx, [eax+12]\n"
    "  mov esi, [eax+16]\n  mov edi, [eax+20]\n  mov ebp, [eax+24]\n"
    "  mov eax, [eax]\n"
    "  call [_rct_call_target]\n"
    "  push eax\n"
    "  mov eax, [esp+4]\n"          /* Regs* (mov/push/pop keep the callee's flags) */
    "  pop dword ptr [eax]\n"
    "  mov [eax+4], ebx\n  mov [eax+8], ecx\n  mov [eax+12], edx\n"
    "  mov [eax+16], esi\n  mov [eax+20], edi\n  mov [eax+24], ebp\n"
    "  pushfd\n  pop dword ptr [eax+28]\n"
    "  add esp, 4\n"
    "  pop edi\n  pop esi\n  pop ebx\n  pop ebp\n"
    "  ret\n"
    ".att_syntax\n");

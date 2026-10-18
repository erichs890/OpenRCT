/* Loading the original RCT image into this process, and calling into it. */
#ifndef RCT_IMAGE_H
#define RCT_IMAGE_H
#include <stdint.h>

typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, eflags;
} Regs;

typedef struct {
    uint32_t entry;              /* original WinMainCRTStartup (0x58E06D) */
    uint32_t resource_rva, resource_size;
} RctImage;

/* Map RCT at 0x400000. `path` is either the shipped ExeLock/NeoLite-wrapped
   RCT.EXE (decompressed in place by the wrapper's own routines, which runs
   only 0xC47150 / 0xC47C57) or tools/unpack.py output. Binds the imports to
   the real DLLs. Returns NULL, or an error message. Requires the caller's exe
   to reserve the range (see runtime/image_space.S). */
const char *rct_load_image(const char *path, RctImage *out);

/* Point this exe's resource directory at the game's, so LoadIcon/FindResource
   on the shared hInstance (0x400000) find the original resources. */
void rct_use_game_resources(const RctImage *img);

/* The game's IAT slot for an imported function (by name), or NULL. Needs the
   import names, i.e. OriginalFirstThunk, which both builds keep. */
void **rct_import_slot(const char *name);

/* Call an original function with explicit registers; registers and flags
   after its `ret` are written back into *r. */
void rct_call(uint32_t addr, Regs *r);

#endif

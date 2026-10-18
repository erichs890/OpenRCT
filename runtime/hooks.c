#include <windows.h>
#include <string.h>
#include "hooks.h"
#include "platform_file.h"
#include "sawyer_io.h"
#include "paint.h"
#include "platform_display.h"
#include "viewport.h"

/* Adapters from hooks.S. Plain cdecl functions whose original clobbers the
   same registers as C (eax/ecx/edx) are targeted directly. */
void hook_419F53(void), hook_419FF6(void), hook_419FEB(void), hook_419CC0(void), hook_419CEC(void),
    hook_419DB1(void), hook_419EDE(void), hook_419CB5(void), hook_419D6B(void), hook_41A0E4(void),
    hook_41A215(void), hook_40C3E1(void), hook_40C860(void), hook_40BC98(void),
    hook_40C038(void), hook_425E26(void), hook_4288AF(void), hook_4288E9(void), hook_425D00(void),
    hook_4263B8(void), hook_42653B(void), hook_4266CC(void), hook_42685F(void), hook_426A04(void),
    hook_426BA8(void), hook_426D5D(void), hook_426F17(void), hook_4270E0(void), hook_42723E(void),
    hook_4273A8(void), hook_427514(void), hook_42635F(void), hook_42839E(void), hook_42860C(void);
void *tramp_425D00;

const RctHook rct_hooks[] = {
    {0x584F20, (void *)file_seek_set, "file_seek_set"},
    {0x584F37, (void *)file_seek_cur, "file_seek_cur"},
    {0x584F4E, (void *)file_seek_end, "file_seek_end"},
    {0x584F65, (void *)file_read, "file_read"},
    {0x585000, (void *)file_write, "file_write"},
    {0x419F53, (void *)hook_419F53, "sawyer_read_byte"},
    {0x419FF6, (void *)hook_419FF6, "sawyer_rle_read_byte"},
    {0x419FEB, (void *)hook_419FEB, "sawyer_rle_read"},
    {0x419FB6, (void *)sawyer_read_rewind, "sawyer_read_rewind"},
    {0x419CC0, (void *)hook_419CC0, "sawyer_write_begin"},
    {0x419CEC, (void *)hook_419CEC, "sawyer_write_byte"},
    {0x419DB1, (void *)hook_419DB1, "sawyer_rle_write_byte"},
    {0x419EDE, (void *)hook_419EDE, "sawyer_rle_flush"},
    {0x419D6B, (void *)hook_419D6B, "sawyer_write_end"},
    {0x419CB5, (void *)hook_419CB5, "sawyer_rle_write"},
    {0x41A05F, (void *)sawyer_write_checksum_sv4, "sawyer_write_checksum_sv4"},
    {0x41A0AB, (void *)sawyer_write_checksum_sc4, "sawyer_write_checksum_sc4"},
    {0x41A0E4, (void *)hook_41A0E4, "sawyer_check_sv4"},
    {0x41A215, (void *)hook_41A215, "sawyer_check_sc4"},
    {0x40C3E1, (void *)hook_40C3E1, "metal_supports_paint(0)"},
    {0x40C860, (void *)hook_40C860, "metal_supports_paint(1)"},
    {0x40BC98, (void *)hook_40BC98, "wooden_supports_paint(0)"},
    {0x40C038, (void *)hook_40C038, "wooden_supports_paint(1)"},
    {0x57FAB6, (void *)gfx_invalidate_rect, "gfx_invalidate_rect"},
    {0x425E26, (void *)hook_425E26, "viewport_render"},
    {0x4288AF, (void *)hook_4288AF, "window_zoom_in"},
    {0x4288E9, (void *)hook_4288E9, "window_zoom_out"},
    {0x4263B8, (void *)hook_4263B8, "paint_add_parent(rot 0)"},
    {0x42653B, (void *)hook_42653B, "paint_add_parent(rot 1)"},
    {0x4266CC, (void *)hook_4266CC, "paint_add_parent(rot 2)"},
    {0x42685F, (void *)hook_42685F, "paint_add_parent(rot 3)"},
    {0x426A04, (void *)hook_426A04, "paint_add_child(rot 0)"},
    {0x426BA8, (void *)hook_426BA8, "paint_add_child(rot 1)"},
    {0x426D5D, (void *)hook_426D5D, "paint_add_child(rot 2)"},
    {0x426F17, (void *)hook_426F17, "paint_add_child(rot 3)"},
    {0x4270E0, (void *)hook_4270E0, "paint_add_attached(rot 0)"},
    {0x42723E, (void *)hook_42723E, "paint_add_attached(rot 1)"},
    {0x4273A8, (void *)hook_4273A8, "paint_add_attached(rot 2)"},
    {0x427514, (void *)hook_427514, "paint_add_attached(rot 3)"},
    {0x42635F, (void *)hook_42635F, "paint_session_begin"},
    {0x42839E, (void *)hook_42839E, "paint_arrange"},
    {0x42860C, (void *)hook_42860C, "paint_draw"},
    /* not a reconstruction: entry adapter for the map magnification (no-op at 1x) */
    {0x425D00, (void *)hook_425D00, "map_pick (magnify prologue)", 8, &tramp_425D00},
};
const int rct_hook_count = sizeof rct_hooks / sizeof *rct_hooks;

void rct_patch_jmp(uint32_t addr, void *target)
{
    uint8_t jmp[5] = {0xE9};
    int32_t rel = (int32_t)((uintptr_t)target - (addr + 5));
    memcpy(jmp + 1, &rel, 4);
    memcpy((void *)(uintptr_t)addr, jmp, 5);   /* the image range is RWX */
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)addr, 5);
}

/* Copy `n` whole instructions from addr (position-independent ones only) to
   an executable stub that jumps back to addr + n. */
static void *make_trampoline(uint32_t addr, int n)
{
    uint8_t *t = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(t, (void *)(uintptr_t)addr, n);
    t[n] = 0xE9;
    int32_t rel = (int32_t)((addr + n) - ((uintptr_t)t + n + 5));
    memcpy(t + n + 1, &rel, 4);
    return t;
}

void rct_install_hooks(int i)
{
    for (int k = 0; k < rct_hook_count; k++)
        if (i < 0 || i == k) {
            if (rct_hooks[k].moved)
                *rct_hooks[k].trampoline = make_trampoline(rct_hooks[k].addr, rct_hooks[k].moved);
            rct_patch_jmp(rct_hooks[k].addr, rct_hooks[k].target);
        }
}

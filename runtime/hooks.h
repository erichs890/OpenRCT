/* Redirect original functions to their reconstructions (jmp at the entry). */
#ifndef RCT_HOOKS_H
#define RCT_HOOKS_H
#include <stdint.h>

typedef struct {
    uint32_t addr;
    void *target;     /* the reconstruction, or its register adapter (runtime/hooks.S) */
    const char *name;
    uint8_t moved;    /* > 0: these entry bytes run from a trampoline (the hook continues there) */
    void **trampoline;
} RctHook;

extern const RctHook rct_hooks[];
extern const int rct_hook_count;

/* Write a jmp to target at an original function's entry. */
void rct_patch_jmp(uint32_t addr, void *target);

/* Install hook i, or all of them with i < 0. */
void rct_install_hooks(int i);

#endif

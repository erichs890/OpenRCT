/* Register-convention adapters: the original asm callers jump here (via the
   jmp written by runtime/hooks.c) and we call the cdecl reconstruction while
   honouring the contract documented in docs/functions/. */
.intel_syntax noprefix
.text

/* out: al = fn(); every other register (incl. eax[31:8]) preserved */
.macro AL_OUT name, fn
.globl _\name
_\name:
    push ecx
    push edx
    push eax
    call _\fn
    mov [esp], al
    pop eax
    pop edx
    pop ecx
    ret
.endm

/* in: al; every register preserved */
.macro AL_IN name, fn
.globl _\name
_\name:
    push eax
    push ecx
    push edx
    movzx eax, al
    push eax
    call _\fn
    add esp, 4
    pop edx
    pop ecx
    pop eax
    ret
.endm

/* no arguments; every register preserved */
.macro KEEP_ALL name, fn
.globl _\name
_\name:
    push eax
    push ecx
    push edx
    call _\fn
    pop edx
    pop ecx
    pop eax
    ret
.endm

/* in: esi = buffer, ecx = n; out: esi += n, ecx = 0, al = buffer[n-1] (the
   last byte read or written, as the original's loop leaves it) */
.macro ESI_ECX name, fn
.globl _\name
_\name:
    push eax
    push edx
    push ecx
    push esi
    call _\fn
    pop esi
    pop ecx
    pop edx
    pop eax
    add esi, ecx
    mov al, [esi-1]
    xor ecx, ecx
    ret
.endm

/* out: CF clear when fn() returns nonzero; ecx/edx preserved */
.macro CF_OUT name, fn
.globl _\name
_\name:
    push ecx
    push edx
    call _\fn
    pop edx
    pop ecx
    cmp eax, 1          /* CF = (eax < 1) */
    ret
.endm

AL_OUT   hook_419F53, sawyer_read_byte
AL_OUT   hook_419FF6, sawyer_rle_read_byte
ESI_ECX  hook_419FEB, sawyer_rle_read
KEEP_ALL hook_419CC0, sawyer_write_begin
AL_IN    hook_419CEC, sawyer_write_byte
AL_IN    hook_419DB1, sawyer_rle_write_byte
KEEP_ALL hook_419EDE, sawyer_rle_flush
ESI_ECX  hook_419CB5, sawyer_rle_write
CF_OUT   hook_41A0E4, sawyer_check_sv4
CF_OUT   hook_41A215, sawyer_check_sc4

/* 0x419D6B leaves ecx = buf_count (movzx) */
.globl _hook_419D6B
_hook_419D6B:
    push eax
    push edx
    call _sawyer_write_end
    pop edx
    pop eax
    movzx ecx, word ptr [_g_sawyer_io + 0x0A]
    ret

/* metal supports: in edi type, ebx segment, ax special, dx height, ebp image
   flags; out CF = blocked; ecx clobbered, everything else preserved */
.macro METAL name, variant
.globl _\name
_\name:
    push eax
    push edx
    push ebp
    movzx ecx, dx
    push ecx
    movsx ecx, ax
    push ecx
    push ebx
    push edi
    push \variant
    call _metal_supports_paint
    add esp, 24
    mov ecx, eax
    pop edx
    pop eax
    bt ecx, 0
    ret
.endm

METAL hook_40C3E1, 0
METAL hook_40C860, 1

/* wooden supports: in edi type, ax special, dx height, ebp image flags;
   out al = drawn, CF = blocked; everything else preserved (ecx clobbered) */
.macro WOODEN name, variant
.globl _\name
_\name:
    push eax
    push edx
    push ebp
    movzx ecx, dx
    push ecx
    movsx ecx, ax
    push ecx
    push edi
    push \variant
    call _wooden_supports_paint
    add esp, 20
    mov ecx, eax
    pop edx
    pop eax
    mov al, cl
    bt ecx, 8
    ret
.endm

WOODEN hook_40BC98, 0
WOODEN hook_40C038, 1

/* 0x425E26 viewport_render: esi vp, edi dpi, ax/bx/dx/bp = x1,y1,x2,y2; preserves all */
.globl _hook_425E26
_hook_425E26:
    pushad
    movsx ecx, bp
    push ecx
    movsx ecx, dx
    push ecx
    movsx ecx, bx
    push ecx
    movsx ecx, ax
    push ecx
    push edi
    push esi
    call _viewport_render
    add esp, 24
    popad
    ret

/* 0x4288AF / 0x4288E9 window_zoom_in / out: esi window; preserves all */
.macro ESI_ARG name, fn
.globl _\name
_\name:
    pushad
    push esi
    call _\fn
    add esp, 4
    popad
    ret
.endm
ESI_ARG hook_4288AF, window_zoom_in
ESI_ARG hook_4288E9, window_zoom_out

/* prologue of 0x425D00 (screen -> map picking, ax/bx = screen x/y): map the
   position through the magnified view, then run the original from a
   trampoline (runtime/hooks.c) */
.globl _hook_425D00
_hook_425D00:
    pushad
    lea eax, [esp + 16]     /* saved ebx */
    push eax
    lea eax, [esp + 32]     /* saved eax */
    push eax
    call _map_magnify_unproject
    add esp, 8
    popad
    jmp dword ptr [_tramp_425D00]

/* paint_add_parent / paint_add_child (0x4263B8 / 0x426A04 + rotation): in ebx image, al/cl x/y offset,
   di/si bound x/y, ah bound z, dx z. Out: every register and CF/ZF/SF as the
   original leaves them (paint_add_parent_regs): when added, the flags of its
   final `and ax, ax`; when not, those of the comparison that decided it, + CF.
   Block on the stack: eax ebx ecx edx esi edi ebp, culled, cmp a, cmp b, cmp32. */
.macro LOAD_REGS
    mov eax, [esp]
    mov ebx, [esp + 4]
    mov ecx, [esp + 8]
    mov edx, [esp + 12]
    mov esi, [esp + 16]
    mov edi, [esp + 20]
    mov ebp, [esp + 24]
.endm
.macro PAINT_ADD name, kind, rot
.globl _\name
_\name:
    sub esp, 44
    mov [esp], eax
    mov [esp + 4], ebx
    mov [esp + 8], ecx
    mov [esp + 12], edx
    mov [esp + 16], esi
    mov [esp + 20], edi
    mov [esp + 24], ebp
    mov eax, esp
    push eax
    push \rot
    push \kind
    call _paint_add_regs
    add esp, 12
    cmp dword ptr [esp + 28], 0
    jne 1f
    LOAD_REGS
    lea esp, [esp + 44]
    and ax, ax
    ret
1:  cmp dword ptr [esp + 40], 0
    jne 2f
    mov ax, [esp + 32]
    cmp ax, [esp + 36]
    jmp 3f
2:  mov eax, [esp + 32]
    cmp eax, [esp + 36]
3:  LOAD_REGS
    lea esp, [esp + 44]
    stc
    ret
.endm
PAINT_ADD hook_4263B8, 0, 0
PAINT_ADD hook_42653B, 0, 1
PAINT_ADD hook_4266CC, 0, 2
PAINT_ADD hook_42685F, 0, 3
PAINT_ADD hook_426A04, 1, 0
PAINT_ADD hook_426BA8, 1, 1
PAINT_ADD hook_426D5D, 1, 2
PAINT_ADD hook_426F17, 1, 3
PAINT_ADD hook_4270E0, 2, 0
PAINT_ADD hook_42723E, 2, 1
PAINT_ADD hook_4273A8, 2, 2
PAINT_ADD hook_427514, 2, 3

/* 0x42635F paint_session_begin: in ebp pool. The original clears the buckets
   with rep stosd: out eax = 0, ecx = 0, edi = the end of the buckets. */
.globl _hook_42635F
_hook_42635F:
    push edx
    push ebp
    call _paint_session_begin
    add esp, 4
    pop edx
    xor eax, eax
    xor ecx, ecx
    mov edi, 0x8FEFF0
    ret

/* 0x42839E paint_arrange: every register as the original leaves it */
.globl _hook_42839E
_hook_42839E:
    sub esp, 28
    mov [esp], eax
    mov [esp + 4], ebx
    mov [esp + 8], ecx
    mov [esp + 12], edx
    mov [esp + 16], esi
    mov [esp + 20], edi
    mov [esp + 24], ebp
    mov eax, esp
    push eax
    call _paint_arrange_regs
    add esp, 4
    LOAD_REGS
    add esp, 28
    ret

/* 0x42860C paint_draw: preserves all (what the original leaves comes from
   0x41DA8B and only reaches 0x425EE3, whose caller restores everything) */
.globl _hook_42860C
_hook_42860C:
    pushad
    call _paint_draw
    popad
    ret

# Differential harness

`make test` builds each `tests/test_*.c` as a 32-bit exe (msys2 `mingw-w64-i686-gcc`) and runs it.

## How it works

- **Address space**: the test exe is linked at `0x400000`, with an uninitialised RWX
  section `.rctimg` covering `0x401000–0xC50000` (`runtime/image_space.S`) and
  its own code at `0xC50000`. The original image range is thus reserved before the
  CRT, heaps or stacks exist. Allocating it at runtime fails: Windows already has
  stacks, heaps and NLS views there.
- **Loading**: `rct_load_image()` (`runtime/image.c`) copies the sections of `original/RCT.unpacked.exe` (or of the shipped `RCT.EXE`, decompressed in place)
  into that range and binds its IAT to the real DLLs. Neither the CRT nor WinMain is
  run, so only functions that do not need game initialisation can be called yet.
- **Calling**: `rct_call(addr, &regs)` loads `eax..ebp`, calls the address, and
  stores the registers and `eflags` after `ret`. VC6 C functions can also be
  called directly through a function pointer (cdecl).
- **Shared globals**: reconstructed code uses ordinary `extern` globals.
  `runtime/globals.ld` binds them to their original addresses, so the original
  and the reconstruction run against *the same memory*. Each test resets that
  memory to an identical (pseudo-random) state before each side runs, then
  compares outputs, the full state block and side effects (file position).
  A standalone build will define the same globals in C.

## Conventions for a test

1. Reset state identically (including stale buffers) → run the original → snapshot.
2. Reset again → run the reconstruction → snapshot → compare everything.
3. Check the original's register contract (what it preserves and returns). The
   reconstruction is plain C, but asm callers depend on that contract, so it is
   documented per function.
4. A deliberately mutated reconstruction must fail (checked by hand when the test is written).

## Hooks

`runtime/hooks.c` lists every reconstructed function, and `rct_install_hooks()`
writes a `jmp` at each original entry. Asm functions go through the adapters in
`runtime/hooks.S`, which translate the register contract (al in/out,
esi/ecx blocks, CF results, preserved registers) to cdecl. With `--hook all`
the "original" side of every test runs through the hooks; `make test-hooks`
installs one hook at a time, so each adapter is also exercised by the
original asm callers. A broken adapter (an unrestored `ecx`) is caught.

## Register leftovers (`tools/regflow.py`)

Asm functions leave intermediate values in registers, and an adapter that preserves them instead is only
equivalent if no caller reads them. `tools/regflow.py --targets ADDR[,..] [--tables TABLE] --regs ...` walks
every path after every call site, byte by byte (`al`, `ah`, upper half), following `push`/`pop` and
`pushal`/`popal`. It ignores masked bits (`and esi, 0x1f`), and with `--stop-at-calls` it descends into direct callees
instead of assuming a calling convention. It reports reads of the callee's leftovers and what is still live at
`ret`. Where readers exist, the adapter reproduces the registers exactly (`paint_add_parent`);
otherwise the analysis is recorded in the function's doc.

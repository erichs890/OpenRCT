"""Static unpacker for the ExeLock/NeoLite-wrapped RCT.EXE (Steam build).

Re-implements the stub loader at 0xC465A9 in Python and runs only its pure
decompression routines (0xC47150 and the stage-2 routine 0xC47C57) under
Unicorn. The game itself is never executed. See docs/unpacking.md.

usage: python tools/unpack.py original/RCT.EXE original/RCT.unpacked.exe
"""
import struct
import sys

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

BASE = 0x400000
FILE = 0x10000000      # file view (MapViewOfFile in the original stub)
WORK = 0x20000000      # three work buffers the stub VirtualAllocs
HEAP = 0x30000000      # bump allocator for VirtualAlloc calls from stage 2
API = 0x3F000000       # fake API thunks
STACK = 0x3FF00000
RET = 0x3FFFFFF0       # sentinel return address
MAGIC = 0x04181996
ERROR_HANDLERS = (0xC4653E, 0xC46566, 0xC46E1E, 0xC46E46, 0xC46E9E, 0xC46FB2, 0xC46FDA)
G_VIRTUALALLOC, G_VIRTUALFREE = 0xC46536, 0xC4653A  # globals the stub fills in


def page(n):
    return (n + 0xFFF) & ~0xFFF


def unpack(src_path, dst_path):
    raw = open(src_path, "rb").read()
    pe = pefile.PE(data=raw)
    oh = pe.OPTIONAL_HEADER
    va = lambda a: struct.unpack_from("<I", pe.get_data(a - BASE, 4))[0]
    hdr_off = va(0xC4629F)
    h = lambda o: struct.unpack_from("<I", raw, hdr_off + o)[0]
    if h(0) != MAGIC:
        sys.exit("NeoLite header magic not found")

    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(BASE, page(oh.SizeOfImage))
    uc.mem_write(BASE, raw[: oh.SizeOfHeaders])
    for s in pe.sections:  # PointerToRawData == 0 means zero-filled, as the Windows loader does
        if s.PointerToRawData:
            uc.mem_write(BASE + s.VirtualAddress, raw[s.PointerToRawData : s.PointerToRawData + s.SizeOfRawData])
    uc.mem_map(FILE, page(len(raw)))
    uc.mem_write(FILE, raw)
    uc.mem_map(WORK, 0x40000)
    uc.mem_map(HEAP, 0x1000000)
    uc.mem_map(API, 0x1000)
    uc.mem_map(STACK, 0x100000)
    heap = [HEAP]

    # Fake VirtualAlloc(addr,size,type,prot) / VirtualFree(addr,size,type): stdcall, 3/4 args.
    uc.mem_write(API, b"\xc2\x10\x00")       # ret 16
    uc.mem_write(API + 0x10, b"\xc2\x0c\x00")  # ret 12
    uc.mem_write(G_VIRTUALALLOC, struct.pack("<I", API))
    uc.mem_write(G_VIRTUALFREE, struct.pack("<I", API + 0x10))

    def on_code(uc, addr, size, _):
        if addr == API:
            n = struct.unpack("<I", uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 8, 4))[0]
            uc.reg_write(UC_X86_REG_EAX, heap[0])
            heap[0] += page(n)
        elif addr == API + 0x10:
            uc.reg_write(UC_X86_REG_EAX, 1)
        elif addr in ERROR_HANDLERS:
            raise RuntimeError("stub error handler 0x%X reached" % addr)

    def on_unmapped(uc, access, addr, size, value, _):
        raise RuntimeError("unmapped access 0x%X (eip 0x%X)" % (addr, uc.reg_read(0x1A)))  # 0x1A = EIP

    uc.hook_add(UC_HOOK_CODE, on_code, begin=API, end=API + 0x20)
    for a in ERROR_HANDLERS:
        uc.hook_add(UC_HOOK_CODE, on_code, begin=a, end=a)
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)

    def call(fn, *args):
        esp = STACK + 0xF0000 - 4 * (len(args) + 1)
        uc.mem_write(esp, struct.pack("<%dI" % (len(args) + 1), RET, *args))
        uc.reg_write(UC_X86_REG_ESP, esp)
        uc.emu_start(fn, RET)
        return uc.reg_read(UC_X86_REG_EAX)

    prot = {}  # section idx -> characteristics the stub VirtualProtects (header ones are bogus)
    w1, w2, w3 = WORK, WORK + 0x14000, WORK + 0x28000
    ep = BASE + oh.AddressOfEntryPoint
    src = FILE + hdr_off + h(0x1C) * 0x10 + 0x54
    # Stage 2 code and data of the stub itself (0xC4669B / 0xC466BF in the original).
    for off, size in ((h(0x08), h(0x0C)), (h(0x10), h(0x14))):
        src = call(0xC47150, src, ep + off, size, w1)
        if not src:
            sys.exit("stage-2 decompression failed")
    src += h(0x18)  # skip stub relocation block (not applied: image loads at its preferred base)

    # Per-section decompression (loop at 0xC469A8).
    vars_ = STACK + 0xF8000  # &src, &dst, scratch filename
    uc.mem_write(vars_ + 8, b"RCT.EXE\0")
    for i in range(h(0x1C)):
        e = hdr_off + 0x54 + i * 0x10
        idx, flags, usize, arg = struct.unpack_from("<4I", raw, e)
        dst = BASE + pe.sections[idx].VirtualAddress + (h(0x40) if idx == h(0x3C) else 0)
        uc.mem_write(vars_, struct.pack("<II", src, dst))
        call(0xC47C57, vars_ + 8, vars_, vars_ + 4, arg, usize, w1, w2, w3, (flags >> 5) & 1)
        src = struct.unpack("<I", uc.mem_read(vars_, 4))[0]
        prot[idx] = flags
        print("section %-8s idx=%d flags=%08X size=%d" % (pe.sections[idx].Name.rstrip(b"\0").decode(), idx, flags, usize))

    image = bytearray(uc.mem_read(BASE, oh.SizeOfImage))
    return rebuild(pe, image, h, prot)


def rebuild(pe, image, h, prot):
    """Turn the in-memory image into a PE whose file layout equals its memory layout."""
    pe2 = pefile.PE(data=bytes(image), fast_load=True)
    oh = pe2.OPTIONAL_HEADER
    oh.FileAlignment = oh.SectionAlignment
    for i, s in enumerate(pe2.sections):
        s.Characteristics = prot.get(i, s.Characteristics)
        s.PointerToRawData = s.VirtualAddress
        s.SizeOfRawData = page(s.Misc_VirtualSize)
    oh.AddressOfEntryPoint = h(0x20)
    imp = h(0x24)
    n = 0
    while any(struct.unpack_from("<5I", image, imp + n * 20)):
        n += 1
    dd = oh.DATA_DIRECTORY
    dd[1].VirtualAddress, dd[1].Size = imp, (n + 1) * 20
    dd[12].VirtualAddress, dd[12].Size = h(0x28), h(0x2C)
    hdr = pe2.write()[: oh.SizeOfHeaders]
    return bytes(hdr) + bytes(image[oh.SizeOfHeaders :])


if __name__ == "__main__":
    data = unpack(sys.argv[1], sys.argv[2])
    open(sys.argv[2], "wb").write(data)
    print("wrote", sys.argv[2], len(data))

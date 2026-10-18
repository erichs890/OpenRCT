"""Make an analysis-only copy of the unpacked exe with a synthetic export table.

Ghidra's default auto-analysis only finds code reachable by direct flow from the
entry point; Sawyer's asm is mostly reached through register/table calls, so
most of it stays undiscovered. Ghidra's PE loader creates a function at every
export, so we export each `call rel32` target (and every extra address listed
in tools/seeds.txt) as `sub_XXXXXX`, plus targets of code-pointer tables (runs of
>= 3 aligned dwords pointing into code) that decode as plausible code and do
not themselves sit inside such a table.

usage: python tools/seed_exports.py original/RCT.unpacked.exe original/RCT.analysis.exe
"""
import bisect
import collections
import os
import struct
import sys

import capstone
import pefile

CODE = (0x401000, 0x787000)  # .text + CODESEG
MIN_CALLERS = 2  # one stray E8 in data is common; two hitting the same target is not


def call_targets(img, base):
    hits = collections.Counter()
    lo, hi = CODE[0] - base, CODE[1] - base
    for o in range(lo, hi - 5):
        if img[o] == 0xE8:
            t = base + o + 5 + struct.unpack_from("<i", img, o + 1)[0]
            if CODE[0] <= t < CODE[1]:
                hits[t] += 1
    return {t for t, n in hits.items() if n >= MIN_CALLERS}


TABLE_AREAS = ((0x596000, 0x787000), (0x787000, 0x88A000), (0x88A000, 0xC3E000))  # CODESEG .rdata .data
BAD = {"in", "out", "insb", "insd", "outsb", "outsd", "hlt", "arpl", "bound", "into", "les", "lds",
       "aaa", "aas", "daa", "das", "aam", "aad", "salc", "int1", "cli", "sti", "pushal", "popal", "fwait"}


def table_targets(img, base):
    in_code = lambda v: CODE[0] <= v < CODE[1]
    targets, runs = set(), []
    for lo, hi in TABLE_AREAS:
        o = lo - base
        while o < hi - base - 4:
            n = 0
            while o + 4 * n < hi - base and in_code(struct.unpack_from("<I", img, o + 4 * n)[0]):
                n += 1
            if n >= 3:
                targets.update(struct.unpack_from("<%dI" % n, img, o))
                runs.append((base + o, base + o + 4 * n))
                o += 4 * n
            else:
                o += 4
    starts = sorted(runs)
    def in_table(t):
        i = bisect.bisect_right(starts, (t, 1 << 32)) - 1
        return i >= 0 and starts[i][0] <= t < starts[i][1]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    def plausible(t):
        ins = list(md.disasm(img[t - base : t - base + 96], t, count=12))
        for i in ins:
            if i.mnemonic in BAD or i.bytes[:2] == b"\0\0":
                return False
            if i.mnemonic in ("ret", "jmp"):
                return True
        return len(ins) == 12
    return {t for t in targets if not in_table(t) and plausible(t)}


def extra_seeds():
    path = os.path.join(os.path.dirname(__file__), "seeds.txt")
    if not os.path.exists(path):
        return set()
    return {int(l.split("#")[0], 16) for l in open(path) if l.split("#")[0].strip()}


def main(src, dst):
    pe = pefile.PE(src)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.__data__  # file layout == memory layout (see tools/unpack.py)
    seeds = sorted(call_targets(img, base) | table_targets(img, base) | extra_seeds())

    # Build the export directory in a new section at the end of the image.
    sa = pe.OPTIONAL_HEADER.SectionAlignment
    last = pe.sections[-1]
    rva = (last.VirtualAddress + last.Misc_VirtualSize + sa - 1) // sa * sa
    n = len(seeds)
    names = [b"sub_%06X\0" % s for s in seeds]
    dll = b"RCT.EXE\0"
    eat, npt, eot = rva + 40, rva + 40 + 4 * n, rva + 40 + 8 * n
    strs = eot + 2 * n
    blob = bytearray(struct.pack("<IIHHIIIIIII", 0, 0, 0, 0, strs, 1, n, n, eat, npt, eot))
    blob += struct.pack("<%dI" % n, *(s - base for s in seeds))
    off, ptrs = strs + len(dll), []
    for nm in names:
        ptrs.append(off)
        off += len(nm)
    blob += struct.pack("<%dI" % n, *ptrs)
    blob += struct.pack("<%dH" % n, *range(n))
    blob += dll + b"".join(names)
    size = (len(blob) + sa - 1) // sa * sa
    blob += b"\0" * (size - len(blob))

    # Append the section header (there is room: headers are 0x1000 bytes).
    sh = pe.sections[-1].get_file_offset() + 40
    hdr = struct.pack("<8sIIIIIIHHI", b".seeds", len(blob), rva, size, rva, 0, 0, 0, 0, 0x40000040)
    out = bytearray(img[:rva]) + blob
    out[sh : sh + 40] = hdr
    fh = pe.FILE_HEADER.get_file_offset()
    struct.pack_into("<H", out, fh + 2, pe.FILE_HEADER.NumberOfSections + 1)
    oh = pe.OPTIONAL_HEADER.get_file_offset()
    struct.pack_into("<I", out, oh + 56, rva + size)          # SizeOfImage
    struct.pack_into("<II", out, oh + 96, rva, 40 + 10 * n + len(dll) + sum(map(len, names)))  # export dir
    open(dst, "wb").write(out)
    print("%d seeds -> %s" % (n, dst))


if __name__ == "__main__":
    main(*sys.argv[1:3])

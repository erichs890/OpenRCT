"""Function/string inventory of the unpacked exe.

Inputs: original/RCT.unpacked.exe and .analysis/procedures.json (REA
list_procedures on original/RCT.analysis.exe). Outputs .analysis/functions.csv
and .analysis/strings.txt, and prints the summary used in docs/inventory.md.

Function bodies are approximated as [start, next start). That is good enough
for ranking; reconstruction uses REA's exact body ranges.
"""
import collections
import csv
import json
import re
import sys

import capstone
import pefile

B = 0x400000
TEXT, CODESEG, CODE_END = 0x401000, 0x596000, 0x787000


def main(exe="original/RCT.unpacked.exe", procs=".analysis/procedures.json"):
    pe = pefile.PE(exe, fast_load=True)
    img = pe.__data__
    named = {int(p["address"], 16): p["value"] for p in json.load(open(procs)) if not p["address"].startswith("EXT")}
    starts = sorted(a for a in named if TEXT <= a < CODE_END)
    # VC6 C code is the only code with frame prologues; take the first framed function as the boundary.
    c_start = min(a for a in starts if a < CODESEG and img[a - B : a - B + 3] == b"\x55\x8b\xec")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    rows, callers = [], collections.Counter()
    for i, a in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else CODE_END
        end = min(end, CODESEG if a < CODESEG else CODE_END, a + 0x4000)
        calls, icalls, globals_ = set(), 0, set()
        for ins in md.disasm(img[a - B : end - B], a):
            if ins.mnemonic == "call":
                op = ins.op_str
                if op.startswith("0x"):
                    calls.add(int(op, 16))
                else:
                    icalls += 1
            for m in re.findall(r"\[(0x[0-9a-f]{6,})", ins.op_str):
                v = int(m, 16)
                if 0x787000 <= v < 0xC3F000:
                    globals_.add(v)
        for c in calls:
            callers[c] += 1
        kind = "crt/c" if c_start <= a < CODESEG else "asm"
        rows.append(dict(addr="%06X" % a, name=named[a], section=".text" if a < CODESEG else "CODESEG", kind=kind,
                         size=end - a, calls=len(calls), icalls=icalls, globals=len(globals_)))
    for r in rows:
        r["callers"] = callers[int(r["addr"], 16)]
        r["leaf"] = int(r["calls"] == 0 and r["icalls"] == 0)
    with open(".analysis/functions.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)

    strs = []
    for s in pe.sections:
        if s.Name.startswith((b".rdata", b".data", b"DATASEG", b"CODESEG")):
            lo = s.VirtualAddress
            for m in re.finditer(rb"[\x20-\x7e]{5,}\x00", img[lo : lo + s.Misc_VirtualSize]):
                strs.append((B + lo + m.start(), m.group()[:-1].decode()))
    with open(".analysis/strings.txt", "w", encoding="utf-8") as f:
        for a, s in strs:
            f.write("%06X %s\n" % (a, s))

    k = collections.Counter(r["kind"] for r in rows)
    print("C/CRT region starts at 0x%06X" % c_start)
    print("functions", len(rows), dict(k), "leaves", sum(r["leaf"] for r in rows))
    print("strings", len(strs))


if __name__ == "__main__":
    main(*sys.argv[1:])

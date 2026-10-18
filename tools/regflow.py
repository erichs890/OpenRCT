"""Which registers that a callee leaves behind do its callers actually read?

For every call site of the given functions (direct `call rel32`, or indirect
`call [reg*4 + table]` for the given dispatch tables), walk all paths after
the call and report each instruction that reads a register byte (al, ah, the
upper half) that is still the callee's leftover. `push` is not a use (the status follows the value to its `pop`), and
`and reg, imm` does not read the parts the mask clears. A path ends when the byte is
overwritten, at `ret` (reported separately: the caller's caller could see it),
at an indirect jump, or after a step budget. Pushes are tracked so that a
`pop` restores the value pushed (and not the callee's).

usage: python tools/regflow.py --targets 4263B8,42653B --tables 4263A8 \
           [--regs eax,ebx,ecx,esi,edi] [--calls-read eax,ecx]
--calls-read: registers the *next* call is assumed to read (default: none;
a call is then treated as clobbering eax, ecx and edx, cdecl-style, which is
NOT safe for asm callees: use --strict-calls to report every call instead).
"""
import argparse
import collections
import re
import struct
import sys

import capstone
import pefile

B = 0x400000
CODE = (0x401000, 0x787000)
REGS = ["eax", "ebx", "ecx", "edx", "esi", "edi", "ebp"]
# register name -> (base, set of byte parts): 0 = low byte, 1 = second byte, 2 = upper half
PARTS = {}
for r in REGS:
    PARTS[r] = (r, {0, 1, 2})
for r16, r32 in (("ax", "eax"), ("bx", "ebx"), ("cx", "ecx"), ("dx", "edx"), ("si", "esi"), ("di", "edi"),
                 ("bp", "ebp")):
    PARTS[r16] = (r32, {0, 1})
for r8, r32, part in (("al", "eax", 0), ("ah", "eax", 1), ("bl", "ebx", 0), ("bh", "ebx", 1), ("cl", "ecx", 0),
                      ("ch", "ecx", 1), ("dl", "edx", 0), ("dh", "edx", 1)):
    PARTS[r8] = (r32, {part})


def load():
    return pefile.PE("original/RCT.unpacked.exe", fast_load=True).__data__


def call_sites(img, targets, tables):
    sites = []
    for o in range(CODE[0] - B, CODE[1] - B - 7):
        if img[o] == 0xE8:
            t = B + o + 5 + struct.unpack_from("<i", img, o + 1)[0]
            if t in targets:
                sites.append((B + o, 5))
        elif img[o] == 0xFF and img[o + 1] == 0x14:      # call [index*scale + disp32] (SIB)
            sib = img[o + 2]
            if (sib & 7) == 5 and struct.unpack_from("<I", img, o + 3)[0] in tables:
                sites.append((B + o, 7))
    return sites


class Walker:
    def __init__(self, img, watched, strict_calls, calls_read, budget=400, stop_at_calls=False):
        self.stop_at_calls = stop_at_calls   # asm callees may read or define anything: end the path there
        self.img = img
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.watched = watched
        self.strict = strict_calls
        self.calls_read = calls_read
        self.budget = budget
        self.cache = {}
        self.unresolved = set()
        self.summaries = {}
        self.depth = 0

    def insn(self, a):
        if a not in self.cache:
            ins = next(self.md.disasm(self.img[a - B:a - B + 16], a), None)
            self.cache[a] = ins
        return self.cache[a]

    def walk(self, start, init=None):
        """Return (reads, leaks_at_ret, notes); leaks are (reg, part) pairs still live at a ret."""
        reads, rets, notes = set(), set(), set()
        # state: frozenset of (reg, part) still holding the callee's value; stack: tuple of pushed regs-states
        if init is None:
            init = frozenset((r, p) for r in self.watched for p in (0, 1, 2))
        todo = [(start, init, (), 0)]
        seen = set()
        while todo:
            a, live, stack, steps = todo.pop()
            if not live or (a, live, stack) in seen:
                continue
            seen.add((a, live, stack))
            if steps > self.budget or not (CODE[0] <= a < CODE[1]):
                notes.add("budget/outside at %X" % a)
                continue
            ins = self.insn(a)
            if ins is None:
                notes.add("undecodable at %X" % a)
                continue
            m = ins.mnemonic
            read, written = ins.regs_access()
            rn = [ins.reg_name(r) for r in read]
            wn = [ins.reg_name(r) for r in written]
            # xor r,r / sub r,r: a write, not a read
            ops = ins.op_str.split(", ")
            if m in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1]:
                rn = []
            if m == "push" and ops[0] in PARTS:
                rn = [x for x in rn if x != ops[0]]   # saving is not a use: the status travels to the pop
            if m == "pushal":
                rn = []
            masked = set()
            if m == "and" and len(ops) == 2 and ops[0] in PARTS and re.fullmatch(r"(0x[0-9a-f]+|\d+)", ops[1]):
                imm = int(ops[1], 0) & 0xFFFFFFFF
                masked = {p for p, bits in ((0, 0xFF), (1, 0xFF00), (2, 0xFFFF0000)) if not imm & bits}
            for r in rn:
                if r in PARTS:
                    base, parts = PARTS[r]
                    if r == ops[0] and masked:
                        parts = parts - masked
                    hit = {(base, p) for p in parts} & live
                    if hit:
                        reads.add((a, ins.mnemonic + " " + ins.op_str, base))
            nlive = set(live)
            # push/pop: carry the leftover status of a pushed register to its pop
            if m == "push" and ops[0] in PARTS and ops[0].startswith("e"):
                stack = stack + (frozenset(p for (r, p) in live if r == ops[0]),)
            elif m == "pushal":
                stack = stack + tuple(frozenset(p for (r, p) in live if r == reg) for reg in REGS)
            elif m == "push":
                stack = stack + (frozenset(),)
            if m == "pop" and ops[0] in PARTS and ops[0].startswith("e"):
                carried = stack[-1] if stack else frozenset()
                stack = stack[:-1]
                nlive -= {(ops[0], p) for p in (0, 1, 2)}
                nlive |= {(ops[0], p) for p in carried}
                wn = []
            elif m == "popal":
                # values pushed before the call are the caller's own, not the callee's
                vals = stack[-7:] if len(stack) >= 7 else (frozenset(),) * 7
                stack = stack[:-7]
                for reg, carried in zip(REGS, vals):
                    nlive -= {(reg, p) for p in (0, 1, 2)}
                    nlive |= {(reg, p) for p in carried}
                wn = []
            elif m == "pop":
                stack = stack[:-1]
            for r in wn:
                if r in PARTS:
                    base, parts = PARTS[r]
                    # a 32-bit write, or movzx/movsx into a 32-bit reg, defines everything
                    nlive -= {(base, p) for p in parts}
            live = frozenset(nlive)
            if m == "ret":
                rets |= live
                continue
            nxt = ins.address + ins.size
            if m == "call" and self.stop_at_calls:
                # direct callee: analyse it from its entry with the same leftovers (memoised)
                if ins.op_str.startswith("0x") and self.depth < 6:
                    key = (int(ins.op_str, 16), live)
                    if key not in self.summaries:
                        self.summaries[key] = (set(), set())   # recursion guard
                        self.depth += 1
                        r2, rets2, _ = self.walk(int(ins.op_str, 16), live)
                        self.depth -= 1
                        self.summaries[key] = (r2, rets2)
                    r2, rets2 = self.summaries[key]
                    reads |= {(x[0], x[1] + " (in callee of %X)" % a, x[2]) for x in r2}
                    todo.append((nxt, frozenset(rets2), stack, steps + 1))
                    continue
                notes.add("indirect call with leftovers live at %X" % a)
                self.unresolved.add(a)
                continue
            if m == "call":
                if self.strict:
                    for r in {r for (r, p) in live}:
                        reads.add((a, "call " + ins.op_str + " (strict: may read)", r))
                for r in self.calls_read:
                    if any((r, p) in live for p in (0, 1, 2)):
                        reads.add((a, "call " + ins.op_str, r))
                live = frozenset(x for x in live if x[0] not in ("eax", "ecx", "edx"))
                todo.append((nxt, live, stack, steps + 1))
                continue
            if m == "jmp":
                if ins.op_str.startswith("0x"):
                    todo.append((int(ins.op_str, 16), live, stack, steps + 1))
                else:
                    notes.add("indirect jmp at %X" % a)
                continue
            if m.startswith("j") or m in ("loop", "loope", "loopne", "jecxz"):
                if ins.op_str.startswith("0x"):
                    todo.append((int(ins.op_str, 16), live, stack, steps + 1))
                todo.append((nxt, live, stack, steps + 1))
                continue
            todo.append((nxt, live, stack, steps + 1))
        return reads, rets, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--targets", default="")
    ap.add_argument("--tables", default="")
    ap.add_argument("--regs", default="eax,ebx,ecx,esi,edi")
    ap.add_argument("--calls-read", default="")
    ap.add_argument("--strict-calls", action="store_true")
    ap.add_argument("--stop-at-calls", action="store_true", help="do not assume anything about callees")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    hexes = lambda s: {int(x, 16) for x in s.split(",") if x}
    img = load()
    sites = call_sites(img, hexes(a.targets), hexes(a.tables))
    w = Walker(img, a.regs.split(","), a.strict_calls, [r for r in a.calls_read.split(",") if r],
               stop_at_calls=a.stop_at_calls)
    total_reads = collections.Counter()
    leak_ret = collections.Counter()
    flagged = 0
    for site, size in sites:
        reads, rets, notes = w.walk(site + size)
        for r in {x[2] for x in reads}:
            total_reads[r] += 1
        for r in {r for (r, p) in rets}:
            leak_ret[r] += 1
        if reads:
            flagged += 1
            if a.v or flagged <= 15:
                print("call at %X:" % site)
                for (addr, text, r) in sorted(reads)[:6]:
                    print("    %X  %-40s reads leftover %s" % (addr, text, r))
        if notes and a.v:
            print("   notes:", sorted(notes)[:4])
    print("\n%d call sites; %d read a leftover register on some path" % (len(sites), flagged))
    print("reads by register:", dict(total_reads))
    print("leftovers still live at ret (caller's caller could read them):", dict(leak_ret))
    if a.stop_at_calls:
        print("paths ending at a call with leftovers live (unresolved):", len(w.unresolved))


if __name__ == "__main__":
    main()

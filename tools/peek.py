"""Read a few game globals from a running OpenRCT.exe (debugging aid).

usage: python tools/peek.py [name=addr[:fmt] ...]   fmt: i (int32, default), h (int16), B (byte), p (pointer)
With no arguments it prints the display state.
"""
import ctypes
import struct
import subprocess
import sys

DEFAULT = ["active_w=C3C87C", "active_h=C3C86C", "wmsize_w=C3B004", "wmsize_h=C3B574", "cfg_w=8B9E18",
           "cfg_h=8B9E1C", "mode=88B2DB:B", "reinit=C387C0", "surface=C38760:p", "hwnd=C387A8:p"]

k32 = ctypes.windll.kernel32


def pid_of(name="OpenRCT.exe"):
    out = subprocess.run(["tasklist", "/fi", f"imagename eq {name}", "/fo", "csv", "/nh"], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        parts = line.strip('"').split('","')
        if len(parts) > 1 and parts[0].lower() == name.lower():
            return int(parts[1])
    sys.exit("game not running")


def read(h, addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
        return None
    return buf.raw


def main(args):
    h = k32.OpenProcess(0x10 | 0x400, False, pid_of())
    fmts = {"i": ("<i", 4), "h": ("<h", 2), "B": ("<B", 1), "p": ("<I", 4)}
    vals = {}
    for spec in args or DEFAULT:
        name, rest = spec.split("=")
        addr, _, f = rest.partition(":")
        f = f or "i"
        raw = read(h, int(addr, 16), fmts[f][1])
        v = struct.unpack(fmts[f][0], raw)[0] if raw else None
        vals[name] = v
        print(f"{name:10} {v if f != 'p' or v is None else hex(v)}")
    s = vals.get("surface")
    if s and not args:
        bmi = struct.unpack("<I", read(h, s + 0x88, 4))[0]
        w, hh = struct.unpack("<ii", read(h, bmi + 4, 8))
        bpp = struct.unpack("<H", read(h, bmi + 14, 2))[0]
        print(f"surface DIB {w}x{hh} {bpp}bpp, short@+8 = {struct.unpack('<hh', read(h, s + 6, 4))}")


if __name__ == "__main__":
    main(sys.argv[1:])

#!/usr/bin/env python3
"""Static checks that NEOFAN.EXE can load on Windows 98, 98SE, ME, 2000 and XP.

1. PE header: 32-bit x86, GUI subsystem 4.0, OS version 4.0, normal alignment,
   no TLS, no section names longer than 8 characters.
2. Imports: every DLL and function must be in tools/win98_api.txt, the list of
   calls that Windows 98 (first edition) already exports.
3. Code: no instructions newer than the Pentium (no CMOV, MMX or SSE), so the
   program also runs on Pentium, K6 and early Celeron machines. CPUID and
   RDTSC appear only behind the CPUID feature checks in hw.c.

usage: check_pe.py NEOFAN.EXE
"""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FAIL = []


def fail(msg):
    FAIL.append(msg)
    print("FAIL  " + msg)


def ok(msg):
    print("ok    " + msg)


def check(cond, good, bad):
    if cond:
        ok(good)
    else:
        fail(bad)


def load_allow():
    allow, dll = {}, None
    with open(os.path.join(HERE, "win98_api.txt")) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("[") and line.endswith("]"):
                dll = line[1:-1].upper()
                allow.setdefault(dll, set())
            else:
                allow[dll].add(line)
    return allow


def rva_to_off(sections, rva):
    for name, va, vsz, raw, rsz in sections:
        if va <= rva < va + max(vsz, rsz):
            return rva - va + raw
    raise ValueError("rva %x not in any section" % rva)


def cstr(data, off):
    end = data.index(b"\0", off)
    return data[off:end].decode("ascii")


def main():
    path = sys.argv[1]
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        fail("not a PE file")
        return
    machine, nsec, _, _, _, optsz, chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    check(machine == 0x14C and magic == 0x10B, "32-bit x86 (PE32)", "not 32-bit x86")
    image_base, sec_align, file_align = struct.unpack_from("<III", data, opt + 28)
    os_major, os_minor, _, _, sub_major, sub_minor = struct.unpack_from("<HHHHHH", data, opt + 40)
    subsystem, dllchars = struct.unpack_from("<HH", data, opt + 68)
    check(subsystem == 2, "Windows GUI subsystem", "subsystem %d" % subsystem)
    check((sub_major, sub_minor) == (4, 0), "subsystem version 4.0 (98 and ME refuse 5.0 and newer)", "subsystem version %d.%d" % (sub_major, sub_minor))
    check((os_major, os_minor) == (4, 0), "OS version 4.0", "OS version %d.%d" % (os_major, os_minor))
    check(sec_align == 0x1000, "section alignment 4 KB", "section alignment %x" % sec_align)
    check(file_align >= 0x200, "file alignment %xh" % file_align, "file alignment %x" % file_align)
    check(image_base == 0x400000, "fixed image base 400000h", "image base %x" % image_base)
    dirs = opt + 96
    ndirs = struct.unpack_from("<I", data, opt + 92)[0]
    dd = [struct.unpack_from("<II", data, dirs + 8 * i) for i in range(ndirs)]
    check(dd[9][0] == 0, "no TLS directory", "has TLS directory")
    check(dd[13][0] == 0, "no delay-load imports", "has delay-load imports")

    sections = []
    so = opt + optsz
    for i in range(nsec):
        name = data[so + 40 * i: so + 40 * i + 8].rstrip(b"\0").decode("ascii", "replace")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, so + 40 * i + 8)
        if name.startswith("/"):
            fail("long section name %s" % name)
        sections.append((name, va, vsz, raw, rsz))
    ok("sections: " + ", ".join(s[0] for s in sections))

    allow = load_allow()
    imp_rva = dd[1][0]
    off = rva_to_off(sections, imp_rva)
    total = 0
    while True:
        oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", data, off)
        if name_rva == 0:
            break
        dll = cstr(data, rva_to_off(sections, name_rva)).upper()
        if dll not in allow:
            fail("DLL not on the Windows 98 list: %s" % dll)
            allow[dll] = set()
        thunk = rva_to_off(sections, oft or ft)
        while True:
            v = struct.unpack_from("<I", data, thunk)[0]
            if v == 0:
                break
            if v & 0x80000000:
                fail("%s imported by ordinal %d" % (dll, v & 0xFFFF))
            else:
                fn = cstr(data, rva_to_off(sections, v) + 2)
                total += 1
                if fn not in allow[dll]:
                    fail("%s!%s is not on the Windows 98 list" % (dll, fn))
            thunk += 4
        off += 20
    ok("%d imported functions checked" % total)

    objdump = os.environ.get("OBJDUMP", "i686-w64-mingw32-objdump")
    try:
        dis = subprocess.run([objdump, "-d", "--no-show-raw-insn", path], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        fail("could not disassemble: %s" % e)
        dis = ""
    bad = re.compile(r"\t(cmov\w*|fcmov\w*|fcomi\w*|fucomi\w*|\w*xmm\w*|movq|movd|pxor|emms|sysenter|syscall|ud2)\b|%xmm|%mm\d")
    hits = [l.strip() for l in dis.splitlines() if bad.search(l)]
    if hits:
        fail("instructions newer than the Pentium: " + "; ".join(hits[:5]))
    elif dis:
        ok("no CMOV, MMX or SSE instructions (runs on 486 and Pentium class CPUs)")
    for ins in ("cpuid", "rdtsc"):
        n = len(re.findall(r"\t%s\b" % ins, dis))
        ok("%s used %d times, only behind feature checks" % (ins, n))

    if FAIL:
        print("\n%d check(s) failed" % len(FAIL))
        sys.exit(1)
    print("\nall checks passed")


if __name__ == "__main__":
    main()

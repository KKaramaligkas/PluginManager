#!/usr/bin/env python3
"""
Checks the import tables of a linked PSP ELF (after psp-fixup-imports).

Each imported library must own one contiguous run of stubs. When the linker
pulls a library's stubs in two chunks (the library is listed twice on the link
line and something between the two positions uses more of its functions),
psp-fixup-imports still counts every stub from the first one on, so the table
covers other libraries' stubs and the PSP binds the wrong functions. The build
still succeeds, so this script is run after linking to catch it.

Usage: check_imports.py file.elf
"""

import struct
import sys


def main(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        sys.exit("%s: not an ELF file" % path)
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    sections = []
    for i in range(shnum):
        sections.append(struct.unpack_from("<IIIIII", data, shoff + i * shentsize))
    strtab = sections[shstrndx]

    def name(off):
        start = strtab[4] + off
        return data[start:data.index(b"\0", start)].decode()

    by_name = {name(s[0]): s for s in sections}
    for needed in (".lib.stub", ".sceStub.text", ".rodata.sceNid"):
        if needed not in by_name:
            sys.exit("%s: no %s section" % (path, needed))
    _, _, _, stub_addr, stub_off, stub_size = by_name[".lib.stub"]
    text_addr, text_size = by_name[".sceStub.text"][3], by_name[".sceStub.text"][5]
    nid_addr, nid_size = by_name[".rodata.sceNid"][3], by_name[".rodata.sceNid"][5]

    def cstr(addr):
        for s in sections:
            if s[1] != 8 and s[3] <= addr < s[3] + s[5]:
                off = s[4] + addr - s[3]
                return data[off:data.index(b"\0", off)].decode(errors="replace")
        return "?"

    errors = []
    ranges = []
    total = 0
    pos = 0
    while pos + 20 <= stub_size:
        name_ptr, _ver, _flags, ent_len, _vars, funcs, nids, stubs = \
            struct.unpack_from("<IHHBBHII", data, stub_off + pos)
        if ent_len == 0:
            break
        pos += ent_len * 4
        if funcs == 0:
            continue
        lib = cstr(name_ptr)
        first = (stubs - text_addr) // 8
        if stubs < text_addr or stubs + funcs * 8 > text_addr + text_size or (stubs - text_addr) % 8:
            errors.append("%s: stubs outside .sceStub.text" % lib)
        if nids < nid_addr or nids + funcs * 4 > nid_addr + nid_size or (nids - nid_addr) // 4 != first:
            errors.append("%s: NID table does not line up with its stubs" % lib)
        ranges.append((first, first + funcs, lib))
        total += funcs

    ranges.sort()
    for (a0, a1, alib), (b0, b1, blib) in zip(ranges, ranges[1:]):
        if b0 < a1:
            errors.append("%s and %s share stubs: %s was linked in more than one piece "
                          "(list its library only once on the link line)" % (alib, blib, alib))
    if total != text_size // 8 or total != nid_size // 4:
        errors.append("import tables cover %d stubs but the ELF has %d" % (total, text_size // 8))

    if errors:
        for e in errors:
            print("%s: %s" % (path, e), file=sys.stderr)
        return 1
    print("%s: %d imported libraries, %d functions, tables OK" % (path, len(ranges), total))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))

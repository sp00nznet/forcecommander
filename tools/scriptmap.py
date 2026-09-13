"""Recover the game's script-function registry from the lifted code.

Force Commander's .pro/code.bin scripts call named engine functions, and the
exe registers every one of them at startup with an id, a name, a help string
and a handler address.  The registration is two hops:

    push <help>; push <name>; push <id>; push <mgr>; call <registrar>
    ... and inside <registrar>:  push <handler>; call [reg + 0x50]

so pairing the two gives id -> (name, handler).  With that map,

    focom.exe game/Focom.exe --run --argtrace 0x0052C300

reads as a script trace: sub_0052C300 is GamePPVisLibraryManager::GetLibrary,
whose only argument is the id of the subsystem the current script line calls.

    py -3 tools/scriptmap.py out.json      # write the map
    py -3 tools/scriptmap.py --selftest    # check it against known ids
"""
import glob, re, sys, os, json

GEN = r'G:\recomp\pc\forcecommander\src\recomp\gen'
STR = re.compile(r'PUSH32\(esp, (0x00[0-9A-F]{6})u\); /\* (0x00[0-9A-F]{6}): push')
IMM = re.compile(r'PUSH32\(esp, (0x[0-9A-F]+)u?\); /\* (0x00[0-9A-F]{6}): push')
CALL = re.compile(r'RECOMP_CALL\(sub_([0-9A-F]{8})\);')
SETFN = re.compile(r'RECOMP_ICALL\(MEM32\((?:eax|ecx|edx|ebx|esi|edi) \+ 0x50\)\);')

def read_all():
    funcs = {}
    for f in sorted(glob.glob(os.path.join(GEN, 'recomp_*.c'))):
        s = open(f, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'^void sub_([0-9A-F]{8})\(void\) \{$(.*?)(?=^void sub_|\Z)',
                             s, re.M | re.S):
            funcs[int(m.group(1), 16)] = m.group(2).split('\n')
    return funcs

def cstr(data, off):
    e = data.index(b'\0', off)
    return data[off:e].decode('latin1')

def main():
    exe = open(r'G:\recomp\pc\forcecommander\game\Focom.exe', 'rb').read()
    def s_at(va):
        o = va - 0x400000
        if 0 <= o < len(exe):
            try: return cstr(exe, o)
            except ValueError: return None
        return None

    funcs = read_all()
    # pass 2 first: registrar -> handler
    handler = {}
    for va, lines in funcs.items():
        for i, l in enumerate(lines):
            if SETFN.search(l):
                for j in range(i - 1, max(i - 6, -1), -1):
                    m = IMM.search(lines[j])
                    if m and 0x401000 <= int(m.group(1), 16) < 0x7A0000:
                        handler.setdefault(va, []).append(int(m.group(1), 16))
                        break
    # pass 1: registration call sites
    #   push <help>; push <name>; push <id>; push <mgr>; call <registrar>
    # Other lines (a stack-slot store for the EH state) can sit between the
    # pushes and the call, so pair by proximity rather than by adjacency.
    out = []
    for va, lines in funcs.items():
        pend = []                       # (line index, immediate)
        for n, l in enumerate(lines):
            m = IMM.search(l)
            if m:
                pend.append((n, int(m.group(1), 16)))
                continue
            c = CALL.search(l)
            if not c:
                continue
            near = [v for (k, v) in pend if n - k <= 10]
            if len(near) >= 3:
                help_va, name_va, fid = near[-3], near[-2], near[-1]
                nm, hp = s_at(name_va), s_at(help_va)
                if (nm and hp and fid < 0x400 and nm.isprintable()
                        and len(nm) < 64 and len(hp) > 5):
                    out.append((fid, nm, hp, int(c.group(1), 16),
                                handler.get(int(c.group(1), 16), [])))
            pend = []
    # The push-triple shape is common enough that some matches are accidents.
    # A real registration's help text names its own subsystem ("Screen
    # subsystem help", "If supports conditional if, elseif, else, endif"), so
    # prefer those and never let a later match overwrite one.
    seen = {}
    for good in (True, False):
        for fid, nm, hp, reg, hs in out:
            if (nm in hp) is not good or fid in seen:
                continue
            seen[fid] = (nm, hp, reg, hs)
    print('%d registrations, %d distinct ids' % (len(out), len(seen)))
    json.dump({'%d' % k: {'name': v[0], 'help': v[1], 'registrar': v[2],
                          'handlers': v[3] if v[3] else []} for k, v in seen.items()},
              open(sys.argv[1], 'w'), indent=1)
    for fid in sorted(seen)[:25]:
        print(fid, seen[fid][0], ['%08X' % h for h in seen[fid][3]])


KNOWN = {156: 'If', 172: 'Loop', 180: 'Switch', 384: 'String', 520: 'Int',
         900: 'Screen', 905: '3D Render', 925: 'Mouse', 940: 'RE3D'}


def selftest(m):
    """The ids the trace leans on must all be named, and named right."""
    for fid, name in KNOWN.items():
        got = m.get('%d' % fid, {}).get('name')
        assert got == name, (fid, name, got)
    assert len(m) > 100, len(m)
    print('selftest ok: %d ids, %d spot-checked' % (len(m), len(KNOWN)))


if sys.argv[1:2] == ['--selftest']:
    import tempfile
    _out = os.path.join(tempfile.gettempdir(), 'scriptmap_selftest.json')
    sys.argv = [sys.argv[0], _out]
    main()
    selftest(json.load(open(_out)))
else:
    main()

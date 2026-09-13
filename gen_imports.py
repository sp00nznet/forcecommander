#!/usr/bin/env python3
"""Generate Force Commander's import-bridge layer (pipeline Phase 4).

Lifted code reaches each Win32 import through its IAT slot VA. This emits a C
shim per import plus a {iat_va -> shim} table the runtime installs.

Unlike fury3's generator, nothing here is hand-typed. `pe/stdcall_argc.py`
derives every stack-purge count from evidence already on the machine -- the
Windows SDK import libraries, the decoration in the binary's own import name,
the calling convention in a C++ mangled name, or a C-runtime export table.
Force Commander imports 307 functions across 11 DLLs, and 248 resolve.

The 59 that do not are refusals, not gaps, and they are handled differently:

  * 58 __thiscall MSVCP60/MSVCRT members. A __thiscall callee pops its own
    arguments, so the purge count matters and it needs parameter *sizes* the
    mangled name does not reliably give (a struct passed by value has no
    knowable size). Emitting a guess would desynchronise the stack at the first
    call, so these get a shim that aborts with the symbol name instead. That is
    loud, immediate and points at the exact import to hand-write -- unlike a
    wrong purge, which corrupts everything read afterwards and shows up
    nowhere near the cause.
  * DirectInputCreateA. stdcall, but dinput.lib is not in the modern Windows
    SDK so nothing on the machine states its count. Hand-written below.

Output: src/runtime/imports_gen.c   (committed; regenerate with this script)
"""
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, '..', 'tools', 'tools', 'pe'))

from pe_analyze import analyze_pe, build_iat_map          # noqa: E402
from stdcall_argc import ArgcResolver                     # noqa: E402

EXE = os.path.join(_HERE, 'game', 'Focom.exe')
OUT = os.path.join(_HERE, 'src', 'runtime', 'imports_gen.c')

# Counts nothing on this machine can state. Each is from the published header,
# and each is written down with its signature so it can be checked by eye.
HAND_ARGC = {
    # HRESULT WINAPI DirectInputCreateA(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN)
    ('DINPUT.dll', 'DirectInputCreateA'): 4,
}

# Imports with a real body in shims_impl.c rather than a generated stub.
HOST_SHIM = set()

# A shim that returns 1 rather than 0 -- for the "did it work" APIs where a
# zero return makes the caller bail before we have anything to show.
RET_ONE = {
    'GetModuleHandleA', 'GetModuleFileNameA', 'LoadLibraryA', 'GetProcAddress',
    'CreateWindowExA', 'GetDC', 'GetStockObject', 'SelectObject',
    'CreateCompatibleDC', 'RegisterClassA', 'LoadCursorA', 'LoadIconA',
    'SetCurrentDirectoryA', 'GetCurrentDirectoryA', 'ShowWindow',
    'CoInitialize', 'GlobalAlloc', 'GlobalLock', 'malloc', 'calloc',
}

BANNER = """/* Star Wars: Force Commander - import bridge - AUTO-GENERATED, DO NOT EDIT */
/* Regenerate: py -3 gen_imports.py */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "imports.h"

/* %d imports across %d DLLs. %d purge counts derived, %d hand-written,
 * %d refused (see gen_imports.py -- a refusal aborts, it does not guess). */
"""


def cname(dll, name):
    keep = [c if (c.isalnum() or c == '_') else '_' for c in name]
    s = ''.join(keep)
    return 'imp_%s__%s' % (os.path.splitext(dll)[0].replace('.', '_'), s)


def main():
    info = analyze_pe(EXE)
    iat = build_iat_map(info)
    r = ArgcResolver()
    rows = r.resolve_iat_ex(iat)

    derived = hand = refused = 0
    dlls = sorted({row[1] for row in rows})
    body, table = [], []

    for va, dll, name, real, argc, conv, src in rows:
        fn = cname(dll, real)
        if argc is None:
            argc = HAND_ARGC.get((dll, real))
            if argc is not None:
                conv, src, = 'stdcall', 'hand-written'
                hand += 1
        else:
            derived += 1

        if argc is None:
            # Refuse. Abort naming the symbol; never invent a purge count.
            refused += 1
            body.append(
                '/* %s!%s  %s -- purge count NOT derivable, see gen_imports.py */\n'
                'static void %s(void) { IMPORT_REFUSED("%s!%s", "%s"); }'
                % (dll, real, conv or '?', fn, dll, real, conv or 'unknown'))
        elif real in HOST_SHIM:
            body.append('extern void %s(void);  /* %s!%s - hand-written in shims_impl.c */'
                        % (fn, dll, real))
        elif conv == 'data':
            body.append('/* %s!%s is a DATA import (never called) */\n'
                        'static void %s(void) { IMPORT_REFUSED("%s!%s", "data"); }'
                        % (dll, real, fn, dll, real))
        else:
            ret = 1 if real in RET_ONE else 0
            # cdecl pops nothing; stdcall/thiscall pop argc slots. CDECLRET and
            # STDRET both also drop the dummy return address the ICALL pushed.
            epi = 'CDECLRET()' if conv == 'cdecl' else 'STDRET(%d)' % argc
            body.append('/* %s!%s  (%s, %d slots, %s) */\n'
                        'static void %s(void) { IMPORT_STUB("%s"); RET(%d); %s; }'
                        % (dll, real, conv, argc, src, fn, real, ret, epi))
        table.append('    { 0x%08Xu, %s, "%s!%s" },' % (va, fn, dll, real))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', newline='\n') as f:
        f.write(BANNER % (len(rows), len(dlls), derived, hand, refused))
        f.write('\n'.join(body))
        f.write('\n\nconst import_entry_t g_imports[] = {\n')
        f.write('\n'.join(table))
        f.write('\n};\nconst unsigned g_import_count = %d;\n' % len(table))

    print('%d imports, %d DLLs -> %s' % (len(rows), len(dlls), OUT))
    print('  derived %d   hand-written %d   refused %d' % (derived, hand, refused))
    return refused


if __name__ == '__main__':
    main()

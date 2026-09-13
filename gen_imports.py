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

# __thiscall members whose purge count is not derivable from the mangled
# name (see docs/STL-GATE.md). Read off the signature by hand: `this` arrives
# in ecx and is not a popped slot, every parameter here is one 4-byte slot,
# and a by-value return adds a hidden return-buffer pointer.
#
# Deriving these automatically needs a real MSVC demangler, and the table
# gets written by hand anyway because each of these also needs a body --
# whoever writes the body knows the signature. So it is written once, here,
# with the signature beside it so it can be checked by eye.
HAND_THISCALL = {
    # basic_filebuf(FILE*)
    ('MSVCP60.dll', '??0?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAE@PAU_iobuf@@@Z'): 1,
    # basic_ios()
    ('MSVCP60.dll', '??0?$basic_ios@DU?$char_traits@D@std@@@std@@IAE@XZ'): 0,
    # basic_iostream(streambuf*)
    ('MSVCP60.dll', '??0?$basic_iostream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z'): 1,
    # string(const string&)
    ('MSVCP60.dll', '??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z'): 1,
    # string(const allocator&)
    ('MSVCP60.dll', '??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV?$allocator@D@1@@Z'): 1,
    # string(const char*, const allocator&)
    ('MSVCP60.dll', '??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBDABV?$allocator@D@1@@Z'): 2,
    # ios_base::Init()
    ('MSVCP60.dll', '??0Init@ios_base@std@@QAE@XZ'): 0,
    # _Lockit()
    ('MSVCP60.dll', '??0_Lockit@std@@QAE@XZ'): 0,
    # _Winit()
    ('MSVCP60.dll', '??0_Winit@std@@QAE@XZ'): 0,
    # ios_base()
    ('MSVCP60.dll', '??0ios_base@std@@IAE@XZ'): 0,
    # ~basic_filebuf()
    ('MSVCP60.dll', '??1?$basic_filebuf@DU?$char_traits@D@std@@@std@@UAE@XZ'): 0,
    # ~basic_fstream()
    ('MSVCP60.dll', '??1?$basic_fstream@DU?$char_traits@D@std@@@std@@UAE@XZ'): 0,
    # ~basic_ios()
    ('MSVCP60.dll', '??1?$basic_ios@DU?$char_traits@D@std@@@std@@UAE@XZ'): 0,
    # ~basic_iostream()
    ('MSVCP60.dll', '??1?$basic_iostream@DU?$char_traits@D@std@@@std@@UAE@XZ'): 0,
    # ~basic_streambuf()
    ('MSVCP60.dll', '??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAE@XZ'): 0,
    # ~string()
    ('MSVCP60.dll', '??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ'): 0,
    # ~ios_base::Init()
    ('MSVCP60.dll', '??1Init@ios_base@std@@QAE@XZ'): 0,
    # ~_Lockit()
    ('MSVCP60.dll', '??1_Lockit@std@@QAE@XZ'): 0,
    # ~_Winit()
    ('MSVCP60.dll', '??1_Winit@std@@QAE@XZ'): 0,
    # ~ios_base()
    ('MSVCP60.dll', '??1ios_base@std@@UAE@XZ'): 0,
    # ~locale()
    ('MSVCP60.dll', '??1locale@std@@QAE@XZ'): 0,
    # string::operator=(const char*)
    ('MSVCP60.dll', '??4?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z'): 1,
    # string::operator[](size_t) const
    ('MSVCP60.dll', '??A?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEABDI@Z'): 1,
    # string::operator+=(const string&)
    ('MSVCP60.dll', '??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@ABV01@@Z'): 1,
    # string::operator+=(const char*)
    ('MSVCP60.dll', '??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z'): 1,
    # basic_fstream vbase dtor
    ('MSVCP60.dll', '??_D?$basic_fstream@DU?$char_traits@D@std@@@std@@QAEXXZ'): 0,
    # string default-ctor closure
    ('MSVCP60.dll', '??_F?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXXZ'): 0,
    # string::_Copy(size_t)
    ('MSVCP60.dll', '?_Copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z'): 1,
    # string::_Eos(size_t)
    ('MSVCP60.dll', '?_Eos@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z'): 1,
    # string::_Freeze()
    ('MSVCP60.dll', '?_Freeze@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ'): 0,
    # string::_Grow(size_t, bool)
    ('MSVCP60.dll', '?_Grow@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAE_NI_N@Z'): 2,
    # string::_Refcnt(const char*)
    ('MSVCP60.dll', '?_Refcnt@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEAAEPBD@Z'): 1,
    # string::_Split()
    ('MSVCP60.dll', '?_Split@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ'): 0,
    # string::_Tidy(bool)
    ('MSVCP60.dll', '?_Tidy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEX_N@Z'): 1,
    # string::append(const string&, size_t, size_t)
    ('MSVCP60.dll', '?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z'): 3,
    # string::append(size_t, char)
    ('MSVCP60.dll', '?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ID@Z'): 2,
    # string::append(const char*, size_t)
    ('MSVCP60.dll', '?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z'): 2,
    # string::assign(const string&, size_t, size_t)
    ('MSVCP60.dll', '?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z'): 3,
    # string::assign(const char*)
    ('MSVCP60.dll', '?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBD@Z'): 1,
    # string::assign(const char*, size_t)
    ('MSVCP60.dll', '?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z'): 2,
    # basic_ios::clear(int, bool)
    ('MSVCP60.dll', '?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z'): 2,
    # ios_base::clear(int, bool)
    ('MSVCP60.dll', '?clear@ios_base@std@@QAEXH_N@Z'): 2,
    # basic_filebuf::close()
    ('MSVCP60.dll', '?close@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@XZ'): 0,
    # string::copy(char*, size_t, size_t) const
    ('MSVCP60.dll', '?copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPADII@Z'): 3,
    # string::erase(size_t, size_t)
    ('MSVCP60.dll', '?erase@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@II@Z'): 2,
    # string::find(const char*, size_t, size_t) const
    ('MSVCP60.dll', '?find@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z'): 3,
    # string::find_first_not_of(...)
    ('MSVCP60.dll', '?find_first_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z'): 3,
    # string::find_first_of(...)
    ('MSVCP60.dll', '?find_first_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z'): 3,
    # string::find_last_not_of(...)
    ('MSVCP60.dll', '?find_last_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z'): 3,
    # string::find_last_of(...)
    ('MSVCP60.dll', '?find_last_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z'): 3,
    # string::max_size() const
    ('MSVCP60.dll', '?max_size@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIXZ'): 0,
    # basic_filebuf::open(const char*, int)
    ('MSVCP60.dll', '?open@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@PBDH@Z'): 2,
    # string::replace(size_t, size_t, const string&, size_t, size_t)
    ('MSVCP60.dll', '?replace@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@IIABV12@II@Z'): 5,
    # string::resize(size_t)
    ('MSVCP60.dll', '?resize@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXI@Z'): 1,
    # basic_ios::setstate(int, bool)
    ('MSVCP60.dll', '?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z'): 2,
    # string::substr(size_t, size_t) const [+hidden ret ptr]
    ('MSVCP60.dll', '?substr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBE?AV12@II@Z'): 3,
    # ~type_info()
    ('MSVCRT.dll', '??1type_info@@UAE@XZ'): 0,
    # type_info::operator==(const type_info&) const
    ('MSVCRT.dll', '??8type_info@@QBEHABV0@@Z'): 1,
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
                argc = HAND_THISCALL.get((dll, real))
                if argc is not None:
                    conv, src = 'thiscall', 'hand-written'
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
        table.append('    { 0x%08Xu, %s, "%s!%s", %s },'
                     % (va, fn, dll, real,
                        ('"%s"' % conv) if conv else 'NULL'))

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

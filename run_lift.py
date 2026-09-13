#!/usr/bin/env python3
"""Force Commander lift driver (pipeline Phase 3).

Uses the shared `tools/lift/generate.py` rather than forking it. That file was
unimportable until this project needed it, which is why fury3 carries a 361-line
copy; the fix went upstream instead of becoming a fourth copy.

The one thing this adds is **closure-limited lifting**, and it exists because of
scale. Fury3 lifts 1,945 functions. `Focom.exe` has 34,674, so lifting
everything is millions of lines of C before anything has been shown to work
once. Instead, lift the call-graph closure from the entry point and give every
function outside it a stub that aborts naming its own address:

    [not-lifted] sub_004A1C30 called from sub_004A1B00
      Add 0x004A1C30 to the closure: run_lift.py --roots ...

So a run says exactly what to lift next, and the closure grows by measurement
instead of by lifting 3.9 MB on spec. Widen with --max, or --all when the
bring-up is far enough along to want everything.

    py -3 run_lift.py                      # entry-point closure, 2000 functions
    py -3 run_lift.py --max 6000
    py -3 run_lift.py --roots 0x004A1C30,0x004B0000
    py -3 run_lift.py --all
"""
import argparse
import collections
import json
import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_LIFT = os.path.join(_HERE, '..', 'tools', 'tools', 'lift')
sys.path.insert(0, _LIFT)
sys.path.insert(0, os.path.join(_HERE, '..', 'tools', 'tools', 'pe'))

from capstone import Cs, CS_ARCH_X86, CS_MODE_32          # noqa: E402
from generate import (linear_disassemble_function,         # noqa: E402
                      lift_function_linear, write_chunk)
from lift32 import Lifter                                  # noqa: E402
from pe_analyze import analyze_pe, build_iat_map           # noqa: E402

EXE = os.path.join(_HERE, 'game', 'Focom.exe')
CATALOG = os.path.join(_HERE, 'analysis', 'functions.json')
OUT = os.path.join(_HERE, 'src', 'recomp', 'gen')

# Imports whose body is hand-written in shims_impl.c rather than lifted.
HOST_SHIM = {
    # MSVC __chkstk / _alloca_probe. It walks the frame touching guard pages,
    # then returns by moving the return address to the new stack top and doing
    # `push eax; ret`. Lifting that faithfully means modelling a return through
    # a relocated return address; the semantics are simply "allocate EAX bytes
    # of frame", so the shim in shims_impl.c does exactly that.
    0x0056EB30,
}


def closure(funcs, roots, limit):
    """Breadth-first call-graph closure from `roots`, at most `limit` functions.

    Breadth-first on purpose: the first N functions reached from the entry point
    are the startup path, which is what a first render needs. A depth-first walk
    would spend the budget down one arbitrary branch.
    """
    byaddr = {f['address']: f for f in funcs}
    seen, order, q = set(), [], collections.deque(r for r in roots if r in byaddr)
    seen.update(q)
    while q and len(order) < limit:
        a = q.popleft()
        order.append(a)
        for t in byaddr[a].get('calls_to', ()):
            if t in byaddr and t not in seen:
                seen.add(t)
                q.append(t)
    return order


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=EXE)
    ap.add_argument('--catalog', default=CATALOG)
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--roots', default='',
                    help='comma-separated extra root VAs (hex ok)')
    ap.add_argument('--max', type=int, default=2000,
                    help='closure size cap (default 2000)')
    ap.add_argument('--all', action='store_true', help='lift every function')
    ap.add_argument('--split', type=int, default=400)
    args = ap.parse_args()

    if not os.path.exists(args.catalog):
        sys.exit('no catalog at %s -- run disasm32.py first' % args.catalog)

    info = analyze_pe(args.exe)
    iat = build_iat_map(info)
    cs, ce = info.code_start, info.code_end
    print('[*] base=0x%08X code=0x%08X-0x%08X IAT=%d'
          % (info.image_base, cs, ce, len(iat)))

    cat = json.load(open(args.catalog))
    funcs = [f for f in cat['functions'] if cs <= f['address'] < ce]
    byaddr = {f['address']: f for f in funcs}
    print('[*] catalog: %d functions (%.1f%% byte coverage)'
          % (len(funcs), cat.get('stats', {}).get('coverage_pct', 0)))

    entry = info.image_base + info.entry_point_rva \
        if hasattr(info, 'entry_point_rva') else info.entry_point
    roots = [entry] + [int(x, 0) for x in args.roots.split(',') if x.strip()]
    print('[*] roots: ' + ', '.join('0x%08X' % r for r in roots))

    if args.all:
        chosen = sorted(byaddr)
        print('[*] lifting ALL %d functions' % len(chosen))
    else:
        chosen = closure(funcs, roots, args.max)
        print('[*] closure: %d of %d functions (cap %d)'
              % (len(chosen), len(funcs), args.max))
    chosen_set = set(chosen)

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    pe_data = open(args.exe, 'rb').read()
    text = [s for s in info.sections if s.name == '.text'][0]
    code = pe_data[text.raw_offset:
                   text.raw_offset + min(text.virtual_size, text.raw_size)]

    lifter = Lifter(iat_map=iat)
    os.makedirs(args.out, exist_ok=True)
    entries, chunk, idx, errors = [], [], 0, 0
    t0 = time.time()

    for addr in sorted(chosen_set):
        f = byaddr[addr]
        name = 'sub_%08X' % addr
        end = min(f['end'], ce)
        if end <= addr:
            chunk.append(('void %s(void) { }\n' % name, addr, name))
            entries.append((addr, name))
            continue
        if addr in HOST_SHIM:
            chunk.append(('/* %s: host-shimmed */\nextern void %s(void);\n'
                          % (name, name), addr, name))
            entries.append((addr, name))
            continue
        try:
            insns, leaders = linear_disassemble_function(md, code, cs, addr, end)
            if not insns:
                chunk.append(('void %s(void) { }\n' % name, addr, name))
            else:
                chunk.append((lift_function_linear(lifter, name, insns,
                                                   leaders, addr), addr, name))
            entries.append((addr, name))
        except Exception as e:                              # noqa: BLE001
            chunk.append(('/* ERROR %s: %s */\nvoid %s(void) {}\n'
                          % (name, e, name), addr, name))
            entries.append((addr, name))
            errors += 1
        if len(chunk) >= args.split:
            write_chunk(args.out, idx, chunk)
            idx += 1
            chunk = []
            print('[*]   %d/%d (%d err)' % (len(entries), len(chosen_set), errors),
                  flush=True)

    # Every function the catalog knows but we did not lift becomes a stub that
    # names itself and stops. It is registered in the dispatch table, so an
    # indirect call or tail call into it resolves and reports instead of
    # failing as an unresolved VA with no clue which one to add.
    stubs = [a for a in sorted(byaddr) if a not in chosen_set]
    for a in stubs:
        name = 'sub_%08X' % a
        chunk.append(('void %s(void) { RECOMP_NOT_LIFTED(0x%08Xu); }\n' % (name, a),
                      a, name))
        entries.append((a, name))
        if len(chunk) >= args.split:
            write_chunk(args.out, idx, chunk)
            idx += 1
            chunk = []
    if chunk:
        write_chunk(args.out, idx, chunk)
        idx += 1

    with open(os.path.join(args.out, 'recomp_funcs.h'), 'w', newline='\n') as f:
        f.write('/* Force Commander - AUTO-GENERATED */\n#pragma once\n'
                '#include <stdint.h>\n\n'
                '/* A catalogued function outside the lifted closure. The stub\n'
                ' * names itself and stops, so a run says which VA to add next. */\n'
                'void recomp_not_lifted(uint32_t va);\n'
                '#define RECOMP_NOT_LIFTED(va) recomp_not_lifted(va)\n\n')
        for a, n in entries:
            f.write('void %s(void);  /* 0x%08X */\n' % (n, a))
    with open(os.path.join(args.out, 'recomp_dispatch.c'), 'w', newline='\n') as f:
        f.write('/* Force Commander - AUTO-GENERATED */\n'
                '#include "recomp_types.h"\n#include "recomp_funcs.h"\n\n'
                'const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n')
        for a, n in sorted(entries):
            f.write('    { 0x%08Xu, %s },\n' % (a, n))
        f.write('};\nconst uint32_t recomp_dispatch_count = %d;\n' % len(entries))
        f.write('\n/* PE entry point, for the host to start at. */\n'
                'uint32_t focom_entry_va = 0x%08Xu;\n' % entry)

    lines = bytes_ = 0
    for fn in os.listdir(args.out):
        fp = os.path.join(args.out, fn)
        if os.path.isfile(fp):
            bytes_ += os.path.getsize(fp)
            lines += sum(1 for _ in open(fp, encoding='utf-8', errors='replace'))
    json.dump({'lifted': len(chosen_set), 'stubs': len(stubs),
               'errors': errors, 'files': idx, 'lines': lines, 'bytes': bytes_},
              open(os.path.join(_HERE, 'analysis', 'phase3_codegen.json'), 'w'),
              indent=1)
    print('=' * 60)
    print('  lifted %d   not-lifted stubs %d   errors %d   files %d'
          % (len(chosen_set), len(stubs), errors, idx))
    print('  %s lines of C, %.1f MB, %.1fs'
          % (format(lines, ','), bytes_ / 1048576, time.time() - t0))
    print('=' * 60)


if __name__ == '__main__':
    main()

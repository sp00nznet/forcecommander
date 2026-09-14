"""Turn a --scripttrace log into named script lines.

`--scripttrace` prints one `[step]` per executed script line:

    [step] t1234 block=1A2B3C40 line=6 of 217 fn=005120D0 vt=007C.. ivt=007C.. op=-1

`vt` is the GamePPVisLibrary wrapper, the same for every line and therefore
useless; `ivt` is the vtable of the object that implements this particular
script function, which analysis/rtti.json names.  So this collapses the log
to the sequence a block actually ran, with the class per line:

    py -3 tools/stepdecode.py work/tw.log            # every block, in order
    py -3 tools/stepdecode.py work/tw.log 217        # only 217-line blocks
    py -3 tools/stepdecode.py --selftest

Caveat worth knowing: a CONTROL-FLOW line's `op` is its jump target, not a
variable slot (GamePPGlobalSysWhile::Execute keeps the target at args+8), so
the name printed beside a While, EndIf, Else or Wait is nonsense. The `decl`
index is still right; it is the slot that is being reused for something else.

ponytail: the first run of each (block, line) pair only. A front end re-runs
the same waiting line thousands of times a second and every repeat says the
same thing.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RTTI = os.path.join(HERE, '..', 'analysis', 'rtti.json')
STEP = re.compile(r'\[step\] t(\d+) block=([0-9A-F]+) line=(-?\d+) of (\d+)'
                  r' fn=([0-9A-F]+) vt=([0-9A-F]+) ivt=([0-9A-F]+) op=(-?\d+)'
                  r'(?: node=([0-9A-F]+) decl=(\d+) var=([0-9A-F]+)=([0-9A-F]+))?')


# Declaration index -> name, from the script container's own table. Empty
# unless gtxvars can find the .gtx, because a trace is still readable without
# it -- just less so.
DECLS = {}


def load_decls(gtx=None):
    """Fill DECLS from a .gtx, so `decl=839` reads as "Min CD Number"."""
    import gtxvars
    if gtx is None:
        gtx = os.path.join(HERE, '..', 'work', 'Opening.gtx')
    if not os.path.exists(gtx):
        return 0
    data = open(gtx, 'rb').read()
    seed = gtxvars.find_seed(data, b'Min CD Number' + b'\0')
    if seed is None:
        return 0
    rows = gtxvars.records(data, seed)
    base = rows[0][0]
    for off, _tag, _a, _kind, _scope, _slot, nm in rows:
        DECLS[(off - base) // gtxvars.STRIDE] = nm
    return len(DECLS)


def vtable_names(path=RTTI):
    d = json.load(open(path, encoding='utf-8'))
    return {int(v['vtable'], 16): k for k, v in d['classes'].items()}


def decode(lines, names, want=None):
    """Yield `(thread, block, line, count, class, op)` for each new step."""
    seen = set()
    for text in lines:
        m = STEP.search(text)
        if not m:
            if text.startswith('[click]') or text.startswith('[scripttrace]'):
                yield None, None, None, None, text.rstrip(), None
            continue
        (t, blk, ln, n, _fn, _vt, ivt, op,
         node, decl, var, val) = m.groups()
        if want is not None and int(n) != want:
            continue
        key = (blk, ln)
        if key in seen:
            continue
        seen.add(key)
        cls = names.get(int(ivt, 16), 'ivt=' + ivt)
        if var and int(var, 16):
            who = DECLS.get(int(decl)) if decl else None
            cls += '  %s = 0x%s' % (who or ('var@0x' + var), val)
        elif node:
            cls += '  node kind %d' % ((int(node, 16) & 0xF000) >> 12)
        yield t, blk, int(ln), int(n), cls, int(op)


def main():
    a = sys.argv[1:]
    if a and a[0] == '--selftest':
        return selftest()
    if not a:
        print(__doc__)
        return 2
    want = int(a[1]) if len(a) > 1 else None
    names = vtable_names()
    n = load_decls()
    if n:
        print('%d declarations named' % n)
    with open(a[0], encoding='utf-8', errors='replace') as f:
        for t, blk, ln, n, cls, op in decode(f, names, want):
            if blk is None:
                print('---', cls)
            else:
                print('t%s %s L%-4d/%-4d %-40s op=%d' % (t, blk, ln, n, cls, op))
    return 0


def selftest():
    names = {0x007C5990: 'GamePPVisCodeBlock'}
    log = [
        '[step] t1 block=AAAA line=6 of 217 fn=1 vt=2 ivt=007C5990 op=-1\n',
        '[step] t1 block=AAAA line=6 of 217 fn=1 vt=2 ivt=007C5990 op=-1\n',
        '[step] t1 block=AAAA line=7 of 217 fn=1 vt=2 ivt=00DEAD00 op=3\n',
        '[step] t1 block=BBBB line=1 of 9 fn=1 vt=2 ivt=007C5990 op=0'
        ' node=03471022 decl=839 var=171F9DAC=FFFFFFFF\n',
        '[click] #1\n',
    ]
    out = list(decode(log, names))
    assert len(out) == 4, out                 # one repeat dropped
    assert out[0][4] == 'GamePPVisCodeBlock'
    assert out[1][4] == 'ivt=00DEAD00'        # unknown vtable kept as-is
    assert out[3][4].startswith('[click]')
    # the decl index names the variable when the table has been loaded
    DECLS[839] = 'Min CD Number'
    named = [o for o in decode(log, names) if o[1] == 'BBBB'][0]
    assert 'Min CD Number = 0xFFFFFFFF' in named[4], named[4]
    DECLS.clear()
    only = [o for o in decode(log, names, 217) if o[1]]
    assert [o[2] for o in only] == [6, 7], only
    print('stepdecode.py self-test OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())

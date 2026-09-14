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
                  r'(?: node=([0-9A-F]+) var=([0-9A-F]+)=([0-9A-F]+))?')


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
        t, blk, ln, n, _fn, _vt, ivt, op, node, var, val = m.groups()
        if want is not None and int(n) != want:
            continue
        key = (blk, ln)
        if key in seen:
            continue
        seen.add(key)
        cls = names.get(int(ivt, 16), 'ivt=' + ivt)
        if var and int(var, 16):
            cls += '  var 0x%s = 0x%s' % (var, val)
        elif node:
            cls += '  node type %d' % (int(node, 16) >> 12)
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
        '[step] t1 block=BBBB line=1 of 9 fn=1 vt=2 ivt=007C5990 op=0\n',
        '[click] #1\n',
    ]
    out = list(decode(log, names))
    assert len(out) == 4, out                 # one repeat dropped
    assert out[0][4] == 'GamePPVisCodeBlock'
    assert out[1][4] == 'ivt=00DEAD00'        # unknown vtable kept as-is
    assert out[3][4].startswith('[click]')
    only = [o for o in decode(log, names, 217) if o[1]]
    assert [o[2] for o in only] == [6, 7], only
    print('stepdecode.py self-test OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())

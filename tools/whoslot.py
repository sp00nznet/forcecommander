"""Name a code address by the vtable slot that holds it.

A --scripttrace line whose thunk is sub_005120D0 or sub_005120[6]0 carries a
FUNCTION pointer where the other thunks carry an object, so rtti.json cannot
name it directly.  But every one of those functions is a virtual method, so
the vtable that contains it does name it: vtables.json says which vtable and
slot, rtti.json says which class owns that vtable.

    py -3 tools/whoslot.py 0x00655280 0x006426C0
    py -3 tools/whoslot.py --log work/tw.log        # every vt= in a trace
    py -3 tools/whoslot.py --selftest
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ANALYSIS = os.path.join(HERE, '..', 'analysis')


def load(analysis=ANALYSIS):
    rt = json.load(open(os.path.join(analysis, 'rtti.json'), encoding='utf-8'))
    byvt = {int(v['vtable'], 16): k for k, v in rt['classes'].items()}
    vts = json.load(open(os.path.join(analysis, 'vtables.json'), encoding='utf-8'))
    return byvt, vts


def index(byvt, vts):
    """addr -> list of 'Class+slot' (or '0xVTABLE+slot' for unnamed vtables)."""
    out = {}
    for v in vts:
        va = int(v['address'], 16)
        who = byvt.get(va, v['address'])
        for slot, e in enumerate(v['entries']):
            out.setdefault(int(e, 16), []).append('%s+%d' % (who, slot))
    return out


def name(idx, addr):
    hits = idx.get(addr)
    if not hits:
        return '?'
    # A method inherited into many vtables lists them all; the shortest class
    # name is the one that declared it often enough to be the useful answer.
    return min(hits, key=len) + (' (+%d more)' % (len(hits) - 1) if len(hits) > 1 else '')


def main():
    a = sys.argv[1:]
    if a and a[0] == '--selftest':
        return selftest()
    if not a:
        print(__doc__)
        return 2
    idx = index(*load())
    if a[0] == '--log':
        seen = []
        for line in open(a[1], encoding='utf-8', errors='replace'):
            for m in re.finditer(r'\bvt=([0-9A-F]{8})', line):
                v = int(m.group(1), 16)
                if v not in seen:
                    seen.append(v)
        for v in seen:
            print('0x%08X  %s' % (v, name(idx, v)))
        return 0
    for s in a:
        v = int(s, 16)
        print('0x%08X  %s' % (v, name(idx, v)))
    return 0


def selftest():
    byvt = {0x007C5990: 'GamePPVisCodeBlock'}
    vts = [{'address': '0x007C5990', 'slots': 2,
            'entries': ['0x00401000', '0x00401010']},
           {'address': '0x007C9999', 'slots': 1, 'entries': ['0x00401000']}]
    idx = index(byvt, vts)
    assert name(idx, 0x00401010) == 'GamePPVisCodeBlock+1'
    assert name(idx, 0x00401000).startswith('0x007C9999+0')   # shortest wins
    assert '(+1 more)' in name(idx, 0x00401000)
    assert name(idx, 0x00999999) == '?'
    print('whoslot.py self-test OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())

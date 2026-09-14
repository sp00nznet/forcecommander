"""Name the script variables and threads in a Ronin .gtx.

A `.gtx` in the .rpk is a script container, and near the end of it is a flat
declaration table: fixed 34-byte records of

    [4-byte tag][u16 a][u16 kind][u16 c][u16 slot][22-byte NUL-padded name]

`kind` is a subsystem id -- 34 is `Variable`, 35 `Structure` -- and for those
`slot` is the index the compiled bytecode uses, which is what a `--scripttrace`
line prints. So this turns

    [step] ... op=83 var=171F9DAC=FFFFFFFF

into "Min CD Number is 0xFFFFFFFF", which is the difference between a number
and an answer.

    py -3 tools/gtxvars.py "Trasse - Night/Opening.gtx"        # from the .rpk
    py -3 tools/gtxvars.py work/Opening.gtx --kind 34          # variables only
    py -3 tools/gtxvars.py work/Opening.gtx --slot 83
    py -3 tools/gtxvars.py --selftest

ponytail: the table is found by seeding on one record and walking the stride
both ways, because the file header is not decoded. That finds the run a seed
is in and nothing else; pass --seed to move it if a file has several runs.
"""
import os
import struct
import sys

STRIDE = 34
NAME_OFF = 12
NAME_LEN = STRIDE - NAME_OFF


def _name(rec):
    """The record's name, or None if the field is not a printable string."""
    raw = rec[NAME_OFF:]
    end = raw.find(b'\0')
    s = raw if end < 0 else raw[:end]
    if not s or any(c < 0x20 or c > 0x7E for c in s):
        return None
    if end >= 0 and any(c != 0 for c in raw[end:]):
        return None                      # padding must be zero
    return s.decode('latin1')


def records(data, seed):
    """Every record of the fixed-stride run containing `seed`, in order."""
    start = seed
    while start - STRIDE >= 0 and _name(data[start - STRIDE:start]) is not None:
        start -= STRIDE
    out = []
    p = start
    while p + STRIDE <= len(data):
        rec = data[p:p + STRIDE]
        nm = _name(rec)
        if nm is None:
            break
        a, kind, c, slot = struct.unpack_from('<HHHH', rec, 4)
        out.append((p, rec[:4], a, kind, c, slot, nm))
        p += STRIDE
    return out


def find_seed(data, anchor):
    """Offset of the record whose name field starts at `anchor`."""
    i = data.find(anchor)
    if i < 0:
        return None
    return i - NAME_OFF


def main():
    a = sys.argv[1:]
    if a and a[0] == '--selftest':
        return selftest()
    if not a:
        print(__doc__)
        return 2
    path = a[0]
    if not os.path.exists(path):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import rpk
        here = os.path.dirname(os.path.abspath(__file__))
        pak = rpk.Pak(os.path.join(here, '..', 'game', 'Resource',
                                   'forcecommand', 'forcecommand.rpk'))
        m = pak.find(path)
        if not m:
            print('not in the .rpk:', path)
            return 1
        data = pak.read(m)
    else:
        data = open(path, 'rb').read()

    seed = None
    for anchor in (b'Min CD Number\0', b'For CD\0'):
        seed = find_seed(data, anchor)
        if seed is not None:
            break
    for i, opt in enumerate(a):
        if opt == '--seed':
            seed = int(a[i + 1], 0)
    if seed is None:
        print('no seed record found; pass --seed OFFSET')
        return 1

    want_kind = want_slot = None
    for i, opt in enumerate(a):
        if opt == '--kind':
            want_kind = int(a[i + 1], 0)
        if opt == '--slot':
            want_slot = int(a[i + 1], 0)

    rows = records(data, seed)
    print('%d records from %06X' % (len(rows), rows[0][0] if rows else 0))
    for off, tag, x, kind, c, slot, nm in rows:
        if want_kind is not None and kind != want_kind:
            continue
        if want_slot is not None and slot != want_slot:
            continue
        print('%06X %s a=%04X kind=%-5d c=%04X slot=%-5d %s'
              % (off, tag.hex(), x, kind, c, slot, nm))
    return 0


def selftest():
    def rec(tag, a, kind, c, slot, name):
        body = tag + struct.pack('<HHHH', a, kind, c, slot)
        return body + name.encode('latin1').ljust(NAME_LEN, b'\0')

    data = (b'\xAA' * 7
            + rec(b'\x08\x04\x20\x00', 0xFFFF, 34, 0x0208, 83, 'Min CD Number')
            + rec(b'\x08\x04\x20\x00', 0xFFFF, 34, 0x020D, 236, 'CD Number')
            + rec(b'\x08\x04\x20\x00', 0x0345, 59, 0x003B, 0x0351, 'For CD')
            + b'\xFF' * 40)
    seed = find_seed(data, b'Min CD Number\0')
    assert seed == 7, seed
    rows = records(data, seed)
    assert len(rows) == 3, rows
    assert [r[6] for r in rows] == ['Min CD Number', 'CD Number', 'For CD']
    assert rows[0][3] == 34 and rows[0][5] == 83
    # a name field with a NUL followed by non-zero padding is not a record
    bad = rec(b'\x08\x04\x20\x00', 0, 34, 0, 1, 'x')[:NAME_OFF + 2] + b'y' * 20
    assert len(bad) == STRIDE, len(bad)
    assert _name(bad) is None
    # ...but a name that exactly fills the field, with no NUL at all, is fine
    full = rec(b'\x08\x04\x20\x00', 0, 34, 0, 1, 'x' * NAME_LEN)
    assert _name(full) == 'x' * NAME_LEN
    print('gtxvars.py self-test OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())

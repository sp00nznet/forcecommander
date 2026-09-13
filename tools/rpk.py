"""Ronin PakFile (.rpk) reader -- Force Commander's entire asset archive.

Layout, all little-endian:

  +0x00  "Ronin PakFile\r\n\0"
  +0x10  u32 dir_offset
  +0x14  member data, back to back, in directory order

  at dir_offset:
    18 bytes 0xFF, 6 bytes 0x00
    u32 name_count
    name_count x (u32 id, u32 len, char[len])     -- the string pool
    16 bytes
    u16 year, u8 month, u8 day, u32 time, u32 dir_count, ... -- a HEADER record,
      shaped like an entry but its first u32 is the number of directories
    entry records until the year field stops being a plausible year:
      u16 year, u8 month, u8 day, u32 time
      u32 offset          (files) / u32 unused (dirs)
      u32 size            -- 0 discriminates a DIRECTORY, and that is the
                             only way to tell a 28-byte record from a 32-byte one
      u32 x2, u32 x3      (x3 present on files only)
      u32 name_id, u32 parent_id

Directories carry one u32 fewer, so the walk is size-driven and cannot be
seeked into blindly -- read the whole record list once.

ponytail: timestamps are skipped, nothing reads them. Parse them if a build
tool ever needs to compare member ages.
"""
import os
import struct
import sys

MAGIC = b'Ronin PakFile\r\n\x00'
DATA_START = 0x14


class Member(object):
    __slots__ = ('name', 'parent', 'offset', 'size')

    def __init__(self, name, parent, offset, size):
        self.name = name
        self.parent = parent
        self.offset = offset
        self.size = size

    @property
    def path(self):
        return self.parent + '/' + self.name if self.parent else self.name

    def __repr__(self):
        return '<%s %d bytes @%d>' % (self.path, self.size, self.offset)


class Pak(object):
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        size = os.path.getsize(path)
        if self.f.read(16) != MAGIC:
            raise ValueError('%s: not a Ronin PakFile' % path)
        dir_off = struct.unpack('<I', self.f.read(4))[0]
        if not DATA_START < dir_off < size:
            raise ValueError('%s: directory offset %d out of range' % (path, dir_off))
        self.f.seek(dir_off)
        blob = self.f.read(size - dir_off)

        count = struct.unpack_from('<I', blob, 0x18)[0]
        names = {}
        o = 0x1C
        for _ in range(count):
            sid, ln = struct.unpack_from('<II', blob, o)
            o += 8
            names[sid] = blob[o:o + ln].decode('latin1')
            o += ln
        self.names = names

        # Parents resolve by name_id, so walk twice: collect, then join.
        raw = []
        p = o + 0x10
        self.dir_count = struct.unpack_from('<I', blob, p + 8)[0]
        p += 32                                 # the header record
        while p + 28 <= len(blob):
            if not 1990 <= struct.unpack_from('<H', blob, p)[0] <= 2010:
                break
            offset, msize = struct.unpack_from('<II', blob, p + 8)
            n = 28 if msize == 0 else 32
            name_id, parent_id = struct.unpack_from('<II', blob, p + n - 8)
            raw.append((name_id, parent_id, offset, msize))
            p += n
        self.dir_end = dir_off + p

        # ponytail: parent_id is a name_id, and directory names repeat (1510
        # distinct names over 2025 directories), so a member's path is its
        # immediate container only, not a full root-to-leaf chain. Chase the
        # real parentage if two same-named directories ever have to be told
        # apart -- extraction does not need it.
        ndirs = sum(1 for r in raw if r[3] == 0)
        assert ndirs == self.dir_count, (ndirs, self.dir_count)
        # The archive repeats its magic after the last record, so the final
        # entry's name/parent fields read as trailer bytes. Everything before
        # the name pool -- offset and size -- is still good, so keep the member
        # and give it a synthetic name.
        self.members = []
        for i, (name_id, parent_id, offset, msize) in enumerate(raw):
            if msize == 0 or offset + msize > size:
                continue
            self.members.append(Member(names.get(name_id, 'unnamed%04d' % i),
                                       names.get(parent_id, ''),
                                       offset, msize))

    def read(self, m):
        self.f.seek(m.offset)
        data = self.f.read(m.size)
        if len(data) != m.size:
            raise IOError('%s: short read' % m.path)
        return data

    def find(self, path):
        want = path.lower()
        for m in self.members:
            if m.path.lower() == want or m.name.lower() == want:
                return m
        return None


# ---------------------------------------------------------------- .pro files
#
# 2,039 members are named *.pro and they come in two shapes.
#
# A property list, CRLF-separated, is a run of records:
#
#     <name> <type> <count>
#     <count lines of values>
#
# `type` is 0 or 1 (1 appears on `flags` and friends -- a bitfield rather than a
# value list). Values are whitespace-separated numbers, or free text.
#
#     id 0 1
#     218 2 13
#     inheritID 0 1
#     2 21 1
#
# The other 21 are flat NAME LISTS, one entry per line and no header at all.
# `dataset.pro/info.pro` is the big one: 44,589 bytes at offset 20, the first
# member of the whole archive, and it begins `a3de001.wav`. That is what
# RECON.md saw when it reported "the first member is a3de001.wav" -- it was
# reading this list's contents, not a member of that name. The two readings
# agree.
#
# ponytail: values stay strings. Nothing here needs them typed, and the game's
# own types are per-key rather than per-record.


def parse_pro(data):
    """`(records, None)` for a property list, `(None, names)` for a name list.

    records is a list of `(name, type, [value lines])` in file order; keys
    repeat, so a dict would lose some.
    """
    lines = data.decode('latin1').splitlines()
    records = []
    i = 0
    while i < len(lines):
        head = lines[i].strip()
        if not head:
            i += 1
            continue
        parts = head.rsplit(' ', 2)
        if len(parts) != 3 or not parts[1].isdigit() or not parts[2].isdigit():
            return None, [l for l in lines if l.strip()]
        n = int(parts[2])
        records.append((parts[0], int(parts[1]), lines[i + 1:i + 1 + n]))
        i += 1 + n
    return records, None


def _safe(s):
    return ''.join(c if c.isalnum() or c in ' ._-' else '_' for c in s).strip() or '_'


def extract(pak, dest, filt=None):
    n = 0
    for m in pak.members:
        if filt and filt.lower() not in m.path.lower():
            continue
        d = os.path.join(dest, _safe(m.parent))
        if not os.path.isdir(d):
            os.makedirs(d)
        with open(os.path.join(d, _safe(m.name)), 'wb') as out:
            out.write(pak.read(m))
        n += 1
    return n


def selftest(path):
    """Members must tile the data region back to back with no gap or overlap."""
    pak = Pak(path)
    assert pak.members, 'no members'
    order = sorted(pak.members, key=lambda m: m.offset)
    assert order[0].offset == DATA_START, order[0].offset
    for a, b in zip(order, order[1:]):
        assert a.offset + a.size == b.offset, (a, b)
    last = order[-1]
    assert last.offset + last.size <= pak.dir_end, (last, pak.dir_end)
    assert pak.find('info.pro') is not None

    # Both .pro shapes parse, and every member of one or the other.
    props = names = 0
    for m in pak.members:
        if not m.name.lower().endswith('.pro'):
            continue
        recs, nl = parse_pro(pak.read(m))
        assert (recs is None) != (nl is None), m.path
        props += recs is not None
        names += nl is not None
    assert props and names, (props, names)
    print('selftest ok: %d .pro property lists, %d name lists' % (props, names))
    print('selftest ok: %d members in %d directories, tiling %d..%d contiguously'
          % (len(pak.members), pak.dir_count, DATA_START, last.offset + last.size))


if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--selftest':
        selftest(a[1])
    elif len(a) >= 2 and a[1] == '--extract':
        pak = Pak(a[0])
        print('extracted %d' % extract(pak, a[2], a[3] if len(a) > 3 else None))
    elif a:
        pak = Pak(a[0])
        for m in pak.members:
            print('%10d %9d  %s' % (m.offset, m.size, m.path))
    else:
        print(__doc__)

"""Copy files out of the Force Commander CD image, with their real names.

The installer copies `Resource\\` off the disc into the install directory, and
an install assembled by hand is easy to leave incomplete: `Resource\\Music` and
`Resource\\Movies` are ~260 MB and the game says nothing when they are empty.
It just never advances its music state, and a front-end transition that waits
on one waits forever.

The names matter, which is why this reads the JOLIET tree (the supplementary
volume descriptor at sector 17, escape `%/E`) rather than the ISO-9660 one.
Primary-tree names are 8.3 and truncated -- `1201-O~1.IMU` -- while the game
asks for `1201 - OpeningScreen.IMU`, one of the 63 state names in its own
state table.

    py -3 tools/iso_extract.py original/fc1.iso --list
    py -3 tools/iso_extract.py original/fc1.iso RESOURCE/MUSIC game/Resource/Music
    py -3 tools/iso_extract.py --selftest

Retail content: this writes into the install directory, which is gitignored.
Nothing it produces may be committed or redistributed.
"""
import os
import struct
import sys

SECTOR = 2048


def _descriptor(f, joliet=True):
    """Sector of the root directory extent, and its size."""
    for s in range(16, 32):
        f.seek(s * SECTOR)
        d = f.read(SECTOR)
        if d[1:6] != b'CD001':
            break
        want = 2 if joliet else 1
        if d[0] != want:
            continue
        if want == 2 and d[88:91] not in (b'%/@', b'%/C', b'%/E'):
            continue
        root = d[156:190]
        return struct.unpack('<I', root[2:6])[0], struct.unpack('<I', root[10:14])[0]
    raise SystemExit('no %s volume descriptor' % ('Joliet' if joliet else 'ISO'))


def _entries(data, joliet):
    """Yield (name, extent, size, is_dir) from one directory extent."""
    i = 0
    while i < len(data):
        length = data[i]
        if length == 0:                      # pad to the end of the sector
            i = (i // SECTOR + 1) * SECTOR
            continue
        rec = data[i:i + length]
        i += length
        nl = rec[32]
        raw = rec[33:33 + nl]
        if raw in (b'\x00', b'\x01'):        # . and ..
            continue
        name = raw.decode('utf-16-be' if joliet else 'latin1', 'replace')
        name = name.split(';')[0]
        yield (name, struct.unpack('<I', rec[2:6])[0],
               struct.unpack('<I', rec[10:14])[0], bool(rec[25] & 2))


def walk(f, joliet=True):
    """Yield (path, extent, size) for every file in the image."""
    ext, size = _descriptor(f, joliet)
    stack = [(ext, size, '')]
    while stack:
        ext, size, prefix = stack.pop()
        f.seek(ext * SECTOR)
        for name, e, sz, is_dir in _entries(f.read(size), joliet):
            if is_dir:
                stack.append((e, sz, prefix + name + '/'))
            else:
                yield prefix + name, e, sz


def extract(iso, subdir, dest):
    """Copy every file under `subdir` (case-insensitive) into `dest`."""
    want = subdir.replace('\\', '/').strip('/').lower() + '/'
    n = total = 0
    with open(iso, 'rb') as f:
        for path, ext, size in sorted(walk(f)):
            if not path.lower().startswith(want):
                continue
            out = os.path.join(dest, path[len(want):].replace('/', os.sep))
            os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
            if os.path.exists(out) and os.path.getsize(out) == size:
                continue
            f.seek(ext * SECTOR)
            left = size
            with open(out, 'wb') as o:
                while left > 0:
                    chunk = f.read(min(1 << 20, left))
                    if not chunk:
                        raise SystemExit('short read on ' + path)
                    o.write(chunk)
                    left -= len(chunk)
            n += 1
            total += size
            print('  %-44s %10d' % (os.path.basename(out), size))
    print('%d files, %d bytes -> %s' % (n, total, dest))
    return n


def main():
    a = sys.argv[1:]
    if a and a[0] == '--selftest':
        return selftest()
    if len(a) == 2 and a[1] == '--list':
        with open(a[0], 'rb') as f:
            for path, _e, size in sorted(walk(f)):
                print('%-52s %10d' % (path, size))
        return 0
    if len(a) != 3:
        print(__doc__)
        return 2
    extract(a[0], a[1], a[2])
    return 0


def selftest():
    """Build a two-sector directory extent and read it back."""
    def rec(name, extent, size, is_dir):
        """One directory record, by its real offsets: LBA at 2, size at 10,
        flags at 25, name length at 32."""
        # . and .. carry a single byte, not a UCS-2 pair, even in Joliet.
        raw = name.encode('latin1') if name in ('\x00', '\x01') \
            else name.encode('utf-16-be')
        body = (b'\x00'                            # 1  ext attr length
                + struct.pack('<I', extent)        # 2  LBA, little
                + struct.pack('>I', extent)        # 6  LBA, big
                + struct.pack('<I', size)          # 10 size, little
                + struct.pack('>I', size)          # 14 size, big
                + b'\x00' * 7                      # 18 recording date
                + bytes([2 if is_dir else 0])      # 25 flags
                + b'\x00' * 2                      # 26 unit size, gap
                + b'\x00' * 4                      # 28 volume sequence
                + bytes([len(raw)]) + raw)         # 32 name
        length = len(body) + 1
        if length % 2:
            length += 1
            body += b'\x00'
        return bytes([length]) + body

    data = rec('\x00', 0, 0, True) + rec('\x01', 0, 0, True)
    data += rec('A - Long Name.IMU;1', 40, 1234, False)
    data += rec('SUB', 50, 2048, True)
    data = data.ljust(SECTOR, b'\x00')
    got = list(_entries(data, True))
    assert [g[0] for g in got] == ['A - Long Name.IMU', 'SUB'], got
    assert got[0][1:] == (40, 1234, False), got[0]
    assert got[1][3] is True
    # a zero length byte must skip to the next sector rather than loop
    assert list(_entries(data + b'\x00' * 8, True)) == got
    print('iso_extract.py self-test OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())

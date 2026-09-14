#!/usr/bin/env python3
"""Write the Focom.ini the game needs, for a given install directory.

Focom.ini is NOT a settings file. It is a **startup script**: one directive per
line, which the game reads with `sscanf(line, "%4095s %n")` and dispatches on
the keyword. Its own keyword table lives at 0x0083E5E4..0x0083E690 in Focom.exe:

    Wait  Run  Options  Players  GameFiles  Music  Movies  Workspace
    RPKDir  RPKFile  InitBase  SetRegKey  HideLoadingPanel  ShowLoadingPanel
    LoadAppFileName  CheckCD  CheckAppMutex

The file ships on the disc as a **zero-byte placeholder** (Install\\Focom.ini),
because the installer writes it at install time with the paths filled in. So a
copy taken from the disc is empty, the game parses nothing, and it dies with a
virtual call through a member that InitBase would have set.

The template below is the installer's, read out of the compiled InstallShield
script `Install\\setup.ins` (search it for "CheckAppMutex" -- the literals are
plain text in the bytecode). `<T>` there is the install directory.

The installer emits `RPKDir` TWICE, and the two are not the same line: the
bytecode holds different path variables (`b 1c` and `b 1f`), one for the
install directory and one for the CD, so a partial install can leave data on
the disc. With everything installed locally they resolve to the same directory
and the game says so itself, through its own message box:

    Error in workspace: Duplicated object template found:

so only one is emitted here.

`Run` takes an object-template id, and which one decides what the game does.
`2 1 6` is what the installer writes: "Trasse - Day", whose own code.bin is 44
bytes and whose inheritID is `2 1 12` = "Endor - Dawn", the front end. The
campaign maps are the other type-2 subtype-1 templates, and each one's
`info.pro` in the .rpk carries its id and boot thread:

    Tatooine - Day     2 1 7    boot 3     196,662 bytes of code
    Endor - Dusk       2 1 13   boot 59     66,614
    Coruscant - Dusk   2 1 25   boot 0      66,616
    Abridon - Day      2 1 5    boot 0      48,384
    Trasse - Night     2 1 21   boot 43     22,041

--run skips the front end and boots one of them directly, which is the only
way into a map while the menu cannot be clicked.

    py -3 tools/make_focom_ini.py G:\\path\\to\\install > Focom.ini
    py -3 tools/make_focom_ini.py --run "2 1 7" G:\\path\\to\\install
    py -3 tools/make_focom_ini.py --selftest
"""
import os
import sys

TEMPLATE = [
    ('CheckAppMutex', 'FORCE'),
    ('CheckCD', '{T}'),
    ('LoadAppFileName', r'{T}\Resource\appname.ini'),
    ('ShowLoadingPanel', None),
    ('InitBase', None),
    ('RPKDir', r'{T}\Resource\forcecommand'),
    ('Workspace', r'{T}\Resource\forcecommand\forcecommand.gpl'),
    ('Movies', r'{T}\Resource\Movies'),
    ('Music', r'{T}\Resource\Music'),
    ('GameFiles', r'{T}\Resource\GameFiles'),
    ('Players', r'{T}\Resource\Players'),
    # `Options` is in the game's own keyword table and the install has an
    # Options\FoCom.opt, but the template reconstructed from setup.ins did not
    # emit the directive, so the game ran with no settings path at all.
    #
    # The argument is the FILE, not the directory. Given the directory the
    # handler faults in sub_006847E0 constructing a std::string from a null --
    # it is looking for a name it did not get. Given the .opt it runs clean.
    #
    # ponytail: this has not been shown to be what the installer wrote, only
    # that the game accepts it and asks for it. It does not unlock the front
    # end's Single Player item either, so it is a correctness fix and not the
    # one that matters.
    ('Options', r'{T}\Options\FoCom.opt'),
    ('Run', '2 1 6'),
    ('HideLoadingPanel', None),
]

# The install layout the script's paths imply. Resource/* on the disc is copied
# into <T>/Resource/, and Focom.ini itself sits next to it in <T>.
NEEDED_DIRS = ['Resource', 'Resource/forcecommand', 'Resource/Movies',
               'Resource/Music', 'Resource/GameFiles', 'Resource/Players']
NEEDED_FILES = ['Resource/appname.ini',
                'Resource/forcecommand/forcecommand.rpk',
                'Resource/forcecommand/forcecommand.gpl']


def render(target, run=None):
    """The script text for an install rooted at `target`."""
    target = target.rstrip('\\/')
    out = []
    for key, arg in TEMPLATE:
        if key == 'Run' and run:
            arg = run
        if arg is None:
            out.append(key)
        else:
            out.append('%-17s %s' % (key, arg.format(T=target)))
    return '\n'.join(out) + '\n'


def check(target):
    """Report what the script refers to but the install does not have."""
    missing = []
    for d in NEEDED_DIRS:
        if not os.path.isdir(os.path.join(target, d)):
            missing.append(d + '/')
    for f in NEEDED_FILES:
        if not os.path.isfile(os.path.join(target, f)):
            missing.append(f)
    return missing


def selftest():
    t = r'C:\Games\Focom'
    s = render(t)
    lines = s.strip().split('\n')
    assert len(lines) == len(TEMPLATE), len(lines)
    # Every directive is one of the game's own keywords.
    keywords = {k for k, _ in TEMPLATE}
    for l in lines:
        assert l.split()[0] in keywords, l
    # InitBase is the directive that builds GamePPProdBase; without it the
    # loading loop dereferences a null manager.
    assert 'InitBase' in lines
    # Paths must be absolute: the game feeds them back through _fullpath.
    for l in lines:
        p = l.split(None, 1)
        if len(p) == 2 and '\\' in p[1]:
            assert p[1].startswith(t), l
    assert render('C:\\Games\\Focom\\') == s, 'trailing separator not trimmed'
    # --run replaces the template id and nothing else.
    r = render(t, '2 1 7')
    assert 'Run               2 1 7' in r, r
    assert '2 1 6' not in r
    assert len(r.strip().split('\n')) == len(lines)
    print('selftest ok: %d directives' % len(lines))


if __name__ == '__main__':
    a = sys.argv[1:]
    run = None
    if len(a) >= 2 and a[0] == '--run':
        run = a[1]
        a = a[2:]
    if a and a[0] == '--selftest':
        selftest()
    elif a:
        target = os.path.abspath(a[0])
        miss = check(target)
        if miss:
            sys.stderr.write('warning: %s is missing:\n  %s\n'
                             % (target, '\n  '.join(miss)))
        sys.stdout.write(render(target, run))
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)

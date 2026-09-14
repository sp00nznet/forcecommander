# Star Wars: Force Commander — Static Recompilation

Static recompilation of **Star Wars: Force Commander** (LucasArts, 2000) from
its shipping Win32 binary to native C.

Built on the [pcrecomp](https://github.com/sp00nznet/pcrecomp) toolchain, and
next to [xwa](https://github.com/sp00nznet/xwa) — same publisher, same year,
same studio's tooling.

## Project Status: **It renders.** The splash screen, drawn by lifted code.

![The splash screen, drawn by recompiled Force Commander code](docs/img/splash.png)

*640×480, pixel-exact. Every pixel is produced by `sub_00401770` — Force
Commander's own splash dialog procedure, lifted to C — running on a host that
owns nothing but the window. The title text is the game's too: `CreateFontA`
sized from the window rect, then `TextOutA` twice, black then yellow one pixel
up and left, for the drop shadow.*

```
$ build/focom game/Focom.exe --splash
  import bridges:               307
  IAT slots self-patched:       307
  mapped game/Focom.exe: 0x00400000 + 5632000 bytes
  WM_INITDIALOG -> lifted 0x00401770
  [shim] LoadBitmapA(102): 640x480 8bpp, 256 colors, 308264 bytes
  [shim] StretchBlt dst=0,0 640x480 src=0,0 640x480 rop=00CC0020 -> 1
  real shims installed:         71
```

Two functions lifted to get here (the dialog proc and `__chkstk`), 71 of 307
imports with real bodies. **Getting in-game is a different order of work** — see
[The road to in-game](#the-road-to-in-game).

| | |
|---|---:|
| recovered function starts | **34,674** |
| byte coverage of `.text` | **98.8%** |
| classes recovered from RTTI | **567** |
| vtables / virtual methods | 1,121 / 5,436 |
| methods attributed to one class | 4,832 |
| functions attributed to a source file | 738 |
| game-specific classes | **6** |

**Read [`docs/RECON.md`](docs/RECON.md) first.** It is the primary P1 document:
the `RE3D`/`Ronin`/`GEAPI`/`DX7` namespace map, the renderer's interface seam,
and the decoded `.rpk`, `.znm`/SMUSH and `.M3D` formats. This README covers P0,
P2 and what was added afterwards; it does not repeat RECON.md.

---

## P0: what the binaries are

| Binary | Size | `.text` | Built | Role |
|--------|-----:|--------:|-------|------|
| `Focom.exe` | 4,943,872 | **3,936,882** | 2000-03-03 | the game |
| `Force.exe` | 106,496 | — | 2000-02-29 | launcher |
| `Smush.dll` | 147,456 | — | 1999-02-27 | LucasArts SMUSH video (4 exports) |
| `mss32.dll` | 328,704 | — | 1999-01-04 | Miles Sound System |
| `FocomSetup.dll` | 843,776 | — | 2000-02-29 | installer |

MSVC 6.0, `MSVCP60.dll` with 82 imports — STL-heavy C++, and both the CRT and
the standard library are *dynamic*, which is a smaller classification problem
than a statically linked CRT (cf. Monster Truck Madness).

**No DRM**, no `.reloc`, fixed base `0x00400000`. 3.94 MB of `.text` makes this
the second-largest target in the collection; only Rise of Legends (13.25 MB) is
bigger, and that one is explicitly a stress test rather than a project. This is
roughly 1.5× X-Wing Alliance and 4× Crimson Skies.

### The import table understates the runtime surface

307 imports across 11 DLLs, and **not one of them is DirectX**:

```
KERNEL32 63   MSVCP60 82   MSVCRT 67   USER32 44   mss32 22
WINMM 8   GDI32 11   ADVAPI32 5   ole32 3   SHELL32 1   DINPUT 1
```

`DDRAW.DLL`, `SMUSH.DLL`, `FEELIT.DLL` and `DINPUT.DLL` are `LoadLibrary`'d;
DirectPlay, DirectMusic and the video path arrive through `CoCreateInstance`.
RECON.md makes this point from the GUIDs in `.rdata`; the import table confirms
it from the other side.

The consequence for bring-up: `runtime/compat/win32_compat.h` sorts 275
**imported** Win32 APIs into keep/shim/SDL2/stub, and that machinery does not
reach an interface pointer called through a vtable. Budget for interface shims.
`runtime/hybrid/` — built for Encarta 97's MFC boundary — is the nearest
existing machinery. RECON.md's point that `CDD7MemRenderer` gives a software
path means the first shim can be "hand it a lockable surface", not "implement
Direct3D 7".

---

## P2: the classification is free, because RTTI was left on

567 classes out of one `tools/cpp/rtti.py` run. For scale: **Black & White has
569 types and they were recovered by hand** — that project is what all of
`tools/cpp/` was built for. Same class count here, with names, vtables and
inheritance chains, in seconds, because LucasArts shipped a release build with
RTTI enabled.

RECON.md maps the four engine namespaces. What it left as "plus `Subsystem`,
`GamePPMultiplayer`, `GamePPVisBase` and friends" is 237 of the 567 classes, and
they sort cleanly by name prefix:

| Family | Classes | vtable slots | What it is |
|---|---:|---:|---|
| `GamePPGlobal*` | 145 | 4,274 | Platform/game layer — resources, events, managers, **and the script AST** |
| `GamePPSys*` | 67 | 1,650 | Framework core: Object, Process, Event, Message, Resource, Library, Variable, State, Undo |
| `GamePPVis*` | 25 | 1,053 | **A visual editor.** Shipped in the retail binary. |
| `GamePPMultiplayer*` | 20 | 655 | DirectPlay |
| `Focom*` | **6** | 161 | **The entire game-specific surface** |
| `GamePPProd*` / `App` / `Log` | 3 | 247 | Startup and logging |

82% of all 567 classes have a recovered base class. (Namespace counts here are
by outer scope after stripping template arguments; they run a little higher than
RECON.md's for `RE3D` and `Ronin`, which is a counting-method difference rather
than a disagreement — RECON.md's table is the one to quote.)

### Six classes are game-specific. Six, out of 567.

```
FocomStartup                       FocomEmitterObjectCallback
FocomPassCallback                  GamePPFocomSystemManager
GamePPFocomLandscapeResourceData   GamePPFocomLandscapeResourceType
```

Plus a handful of global-scope render-pass callbacks — `SkyObjectCallback`,
`ShadowObjectCallback`, `RadarObjectCallback`, `CFOWOverlayCallback`
(fog of war), `GlobalWeatherObjectCallback`, `CSmushMemRenderCallback`.

This is the Gunman Chronicles shape — *78% of the binary is the SDK, and only
499 of 3,990 functions need real work* — except Gunman needed a four-pass
classifier to **prove** which functions were SDK, and that classifier is all of
`tools/classify/`. Here the answer is in the type names, and `classify/` does
not need to run at all.

So this is a thin game on a large unreleased in-house engine, and the
recompilation is mostly an **engine** recompilation. The payoff is bigger than
one title: anything else built on Ronin/GamePP gets cheap afterwards.

### Mission logic is a visual script, not native code

38 `GamePPGlobal*` classes are AST nodes with their own vtables:

```
If  Else  ElseIf  For  EndFor  While  EndWhile  Switch  Case  CaseOr  CaseRange
CaseOrRange  DefaultCase  Do  Loop  LoopForever  LoopVar  EndLoop  EndIf  And
Or  Assert  Comment  NOOP  Stop  CallParent  EnumDef  ArrayTypeDef  ConstTypeDef
MessageTypeDef  ResourceTypeDef  EventFunctionTypeDef  Argument{Expr,VarDec,…}
```

With `GamePPSysCodeBlock`, `GamePPSysCodeContainer`, `GamePPSysLibraryProc`,
`GamePPVisCodeBlock`, `GamePPVisCodeClipboard`, the `info.pro` / `code.bin`
filenames in `.rdata`, and `Subsystems` as the *first* asset category in the
`.rpk` string table, that is a complete in-house visual scripting language
**and its editor**, compiled into the shipped game.

Force Commander therefore lands on the same shelf as Encarta 97, the Magic
School Bus and Prodigy: **the interpreter is the code and the behaviour is
data.** Recompiling the binary gets the VM; `code.bin` and the `Subsystems`
members of the `.rpk` are the other half of the project.

Audio has two managers, `GamePPGlobalSysMilesSoundManager` and
`GamePPGlobalSysiMuseSoundManager` — **iMuse**, LucasArts' interactive music
system, alongside Miles. And `GamePPGlobalSysFEELitMouseManager` goes with the
`FEELIT.DLL` string: Immersion force-feedback. Genuinely obscure.

---

## Correction: RTTI is a symbol source, not a recovery pass

This was asserted the wrong way round here and is worth recording properly.

`tools/cpp/rtti.py`'s docstring says *the method addresses are proof of function
entry points, which is why this is worth running before disassembly rather than
after*, and the obvious inference is that seeding a 3.94 MB C++ binary with
1,121 vtables should find functions a branch scan cannot. **Measured, it finds
essentially none:**

| | |
|---|---:|
| RTTI virtual methods | 5,436 |
| already in the disassembler's catalog | **5,427** |
| not found by `disasm32` | 9 |
| …of those, new functions rather than alternate entry points | **0** |

All nine land inside a body the sweep had already decoded, against 34,674
recovered starts at 98.8% byte coverage. `vtable_scan.py` measured against RTTI
as truth found 91.6% of its methods and proposed 688 more, of which **109** were
absent from the catalog — so the more generous of the two passes contributes at
most 109 addresses out of 34,674.

That is now measured on two binaries that differ in nearly everything that
should matter (Trespasser: 7.8 MB, has a linker map; this: 3.9 MB, none) with
the same answer both times. **E9 seeding plus the fixpoint is what finds the
functions.** See
[pcrecomp docs/CONSOLIDATION.md](https://github.com/sp00nznet/pcrecomp/blob/main/docs/CONSOLIDATION.md)
items 5 and 6 for the full write-up.

What RTTI is worth here is **names**, and on a stripped retail binary with no
symbols of any kind that is the whole point: 4,832 methods attributed to one of
567 classes plus 738 functions attributed to a source file means **5,570 of
34,674 functions (16%) stop being `sub_004A1C30`**. That is what makes lifted
code readable and a crash stack worth reading.

`analysis/rtti_seeds.json`, `analysis/vtable_seeds.json` and their union in
`analysis/seeds_union.json` (6,124 addresses) are kept as a names/methods index,
not as a seeding input.

---

## The road to in-game

The splash screen was reachable because it is a closed loop: one dialog
procedure, two GDI calls, a resource already inside the exe. Nothing else in
this binary is like that. The inventory below was written when only that loop
worked; the status column is what happened since.

| # | Work | Status |
|---|---|---|
| 1 | **The function catalog.** Needed before any closure can be computed. | **done** — 38,908 entries |
| 2 | **Lift the startup closure** and walk the failures. | **done, and then some** — the whole binary lifts, 10.7M lines of C, 0 errors |
| 3 | **58 `__thiscall` MSVCP60 shims** — "the wall". | **done** — real `basic_string` with the MSVC 6 COW layout. What is left of MSVCP60 is data symbols (vtables, `npos`, `_Nullstr`), not code |
| 4 | **DirectDraw 7 over the DIB.** | **done** — COM object model in the target address space, 640×480 16bpp primary surface, device enumeration, `GetCaps` |
| 5 | **The `.rpk` reader.** 274 MB, and nothing loads without it. | **done** — `tools/rpk.py`; 9,554 members tile the archive with no gap and no overlap |
| 6 | **Stubs that must not lie:** DirectInput, Miles (22), SMUSH, DirectPlay. | **partly** — Miles and WINMM are still stubs; nothing has needed them yet |
| 7 | **Whatever the lifter gets wrong across 34,674 functions.** | **two found, two fixed** — see below |

Item 3 was called the real gate and item 7 the real risk. That was the wrong way
round. The STL came out in a day; the lifter bugs were the expensive part, and
both of them truncated function bodies *silently*, so the generated C compiled,
ran, and quietly did less than the original:

- a fixed 512-byte window in the recursive descent, which cut every body with a
  longer straight run — **WinMain lost everything past 0x004012AE** and fell off
  its own end into the CRT, which then stored the result through a clobbered
  `ebp`;
- the tail-call guard being given the catalog's 4,234 `alias` entries, so a
  `jmp` to a second entry point of the *same* function ended the descent, and
  the arm reachable only through it became an unresolved `ITAIL` at runtime.

A third was chased and turned out not to exist. 4,025 bodies appear to end on a
`lea`, which cannot end a function, and that looked like the catalog's `end`
being too small — but an indirect tail call (`jmp [eax+0x18]`, which is every
vtable thunk in this binary) emits as an `ITAIL` dispatch block, so the last
instruction *comment* in the body is the one before the jump. Measured properly,
**14 of 38,908** bodies never reach a terminator, all one-block fragments at
false starts, and raising the bound for them changes the generated C by nothing.
The catalog's `end` is trustworthy. `run_lift.py` now reports the number and
leaves it alone.

The other finding is that not one of the sixteen blockers between the splash
screen and here was a defect in the lifted code. Every one was an infidelity in
a hand-written Windows or CRT reimplementation. `docs/STL-GATE.md` argues from
that to a 32-bit host, and the argument has only got stronger.

### Where it actually stops

The game runs **its own startup**, driven by its own script -- see
[`docs/STARTUP.md`](docs/STARTUP.md), which is the other thing that had to be
recovered: `Focom.ini` is not a settings file but a directive list, and the disc
ships it as zero bytes because the installer writes it.

```
CheckAppMutex FORCE       -> CreateMutexA
CheckCD <installdir>      -> GetVolumeInformationA, label FOCOM_1
LoadAppFileName ...       -> reads Resource/appname.ini
ShowLoadingPanel          -> CreateDialogParamA(101), dialog procedure
                             0x00401770 -- lifted -- and it paints
InitBase                  -> builds GamePPProdBase, creates its registry key
RPKDir / Workspace        -> opens forcecommand.rpk, 288,585 reads,
                             compiles 1,713 object templates
Movies / Music / GameFiles / Players
Run 2 1 6                 -> starts the "Trasse - Day" section as a process
                             on a Ronin worker thread
HideLoadingPanel
```

CreateThread is honoured -- one thread runs lifted code at a time, with the
switch points at the blocking shims -- and the section's script really runs.

Then **RE3D comes up**: a 640x480 16bpp mode, a flipping primary with a back
buffer, an attached Z buffer, a Direct3D 7 device, texture formats enumerated,
textures uploaded, render state and materials set. All 49 `IDirect3DDevice7`
methods are implemented, with the purge count of each taken from the headers.
Nothing is rasterised yet -- `Clear` really clears the render target and the
`DrawPrimitive` family accepts and counts its vertices.

Getting that far took one discovery and four corrections, all of the same
shape, and `docs/STARTUP.md` has them: the game's screen gate reads
`dwDeviceRenderBitDepth` at a hardcoded `device + 0x454`, which is the **HAL**
slot of the four device descriptions it keeps inline, so offering only the RGB
software device made it skip the entire screen and renderer setup and run its
front end for 61 frames with nothing to draw on.

And then **it draws**. Every reading of the stall pointed at the boot
script -- one context on `Wait Forever`, one on a `Wait If` that never cleared
-- and the script was fine. Its thread was being shot, by one shim:

`u32_MsgWaitForMultipleObjects` passed `nCount = 0` with no handle array, and
read its timeout from `ARG(2)`, which is `bWaitAll`. The return value of that
function is a *position in the handle array*, so with `nCount = 0` "a message
arrived" comes back as `WAIT_OBJECT_0 + 0` -- byte-for-byte what "handle 0
signalled" looks like. Handle 0, for every Ronin thread, is its stop event, and
a boot process lives exactly as long as its boot thread. The first stray mouse
message ended the game's first section; the frame count that varied run to run
(61, 68, 164) was just how long it took one message to arrive.

With the handles passed through, `BeginScene`, `SetTransform`, `SetViewport`,
`Clear`, `EndScene` and `Flip` all run, `CDD7FSScreen::Present` is reached, and
the window shows frames:

```
[present] #1 surface 0x10D62090 640x480: 307200 of 307200 pixels non-black
[present] #3 surface 0x10D62090 640x480: 0 of 307200 pixels non-black
[present] #4 surface 0x10D62090 640x480: 307200 of 307200 pixels non-black
```

And then it renders. `src/runtime/raster.c` is a software rasteriser for the
DrawPrimitive family -- half-space edge functions, gouraud diffuse, one
modulated texture stage, alpha blending, the alpha test, a 16-bit depth test --
and with it, plus two things that had to be right first, the game **boots into
its front end and draws its title screen**, presenting around 3,000 frames a
run.

The two: 130 vtable slots were never lifted, because MSVC's
virtual-inheritance adjustor thunks are eight bytes that nothing calls and
nothing falls into, so `RECOMP_ICALL` answered the slot with `eax = 0` and a
renderer took that null for a render stage. And
`IDirect3DDevice7::Load` was a no-op, which is how every texture stayed black:
the game locks a system-memory surface, writes the image, and calls `Load` to
move it into the texture it draws with.

Two more were found by looking at the frames rather than the code. A bit depth
is not a pixel format -- the font atlas is ARGB1555 and decoding it as 565
painted a solid red panel over the game's own title -- and `Clear` ignored
`D3DCLEAR_ZBUFFER`.

And it is a menu. One value explains why it was not: the front end draws its
menu with a vertex diffuse of 0x00FC0000 -- a real red, and an alpha of zero --
against `ALPHAOP = MODULATE, ALPHAARG1 = TEXTURE, ALPHAARG2 = CURRENT`.
Reading that literally multiplies every glyph by zero, and with the alpha test
the game also sets, 6 of 500 UI draws painted anything. Taking alpha from the
texture alone puts **Single Player / Multiplayer / View Installation / Show
Credits** on the screen.

**And it responds to a click.** That took finding out that there were two
windows: the host's, which has the pixels, and the game's, which has the
procedure Windows delivers input to. `--click X Y` posts a move, a press and a
release to the game's own window -- not the host's, whose queue nobody pumps,
because the main thread is inside lifted code for the whole run -- and
`--uimap` prints the hot rectangles so the coordinates are read off the draws
instead of guessed:

```
140,130  175x 30  centre 227,145    Single Player
538,388   78x 78  centre 577,427    forward
```

Selecting Single Player changes every menu colour and both button glyphs, and
confirming it navigates to the next page. Deeper pages stop drawing their
content, which is the next thing.

Three other things had to be right for that, and `docs/STARTUP.md` has them:
`IDirect3DVertexBuffer7` (the game locks one on its first rendered frame),
`host_present` blitting straight to the window rather than through
`UpdateWindow` (which blocks on another thread's message pump, and hung the
process on the first `Flip` it ever issued), and a fourth lifter bug -- a body
can continue past an int3 when something jumps over it, and four dropped
leaders came out as tail transfers to VAs nobody lifted.

`SMUSH.DLL` is shimmed too, so the intro movie is skipped rather than silently
disabling the module: the exe asks `GetProcAddress` for four exports and the
loader answers one NULL by `FreeLibrary`-ing the whole thing.

## Where it goes next

In order, and the first two are the ones that matter:

1. **Deeper front-end pages stop drawing their content.** The page after
   Single Player maps four text rectangles and shows none of them, while its
   buttons still draw. Same class as the red panel and the invisible menu: a
   pixel state honoured differently from the hardware. `--uimap`, `--dumpframe`
   and the per-draw state line find these.
2. **Then the mission.** Booting a map template directly does not work and the
   reason is structural -- the engine and renderer init live in the front end's
   script, so a map that inherits nothing never runs them. The route in is
   through the menu.
3. **Miles (21 entries) and WINMM (7).** Still stubs. Nothing has needed them,
   and returning "no device" cleanly should be enough for a first frame.
4. **Stub `GamePPVis*` entirely.** 25 classes and 1,053 vtable slots of *editor*
   that a first playable does not need. Free.
5. **Plan interface shims, not import shims** — and start at `CDD7MemRenderer`.
   `docs/STL-GATE.md` makes the case for going further and loading the real
   32-bit DLLs instead.
6. **DirectPlay is a dead service.** Whatever replaces it is a design decision,
   not a recompilation one.
7. Disc 2 has not been unpacked.

`Smush.dll` needs no reverse engineering — RECON.md has the container decoded
and ScummVM has implemented SMUSH for twenty years.

---

## Layout

```
forcecommander/
  original/   both discs (.bin/.cue, and fc1.iso converted from disc 1)
  game/       extracted binaries
  analysis/   catalog, sections, imports, rtti.json, vtables.json, seeds
  docs/
    RECON.md  the primary P1 document -- namespaces, renderer seam, formats
```

## Credits

Star Wars: Force Commander © 2000 LucasArts Entertainment Company. This project
neither contains nor distributes any part of it.

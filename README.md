# Star Wars: Force Commander — Static Recompilation

Static recompilation of **Star Wars: Force Commander** (LucasArts, 2000) from
its shipping Win32 binary to native C.

Built on the [pcrecomp](https://github.com/sp00nznet/pcrecomp) toolchain, and
next to [xwa](https://github.com/sp00nznet/xwa) — same publisher, same year,
same studio's tooling.

## Project Status: **P0, P1 and P2 complete. Nothing lifted yet.**

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

## Where it goes next

1. **Regenerate the catalog and merge the names.** `disasm32.py` over 3.94 MB is
   a long run (>20 min) and its output is not committed; `pe/debug_symbols.py`
   and `pe/merge_names.py` both need it:
   ```sh
   python ../tools/tools/disasm/disasm32.py game/Focom.exe -o analysis/functions.json
   python ../tools/tools/pe/debug_symbols.py game/Focom.exe \
          --functions analysis/functions.json -o analysis/src_files.json
   python ../tools/tools/pe/merge_names.py --source rtti=analysis/rtti.json \
          --files analysis/src_files.json -o analysis/names.json
   ```
   Do **not** pass `--seed-functions`; the section above is why.
2. **Split the work by library, not by address.** `Ronin::` first — 75 classes
   of filesystem, threading, bitmap loading and math with no game logic in them,
   and `CRPKPackFile` is needed before any asset loads. Then `GamePPSys*` (67),
   which is the bottom of the framework dependency graph.
3. **Stub `GamePPVis*` entirely.** 25 classes and 1,053 vtable slots of *editor*
   that a first playable does not need. Free.
4. **Finish the `.rpk` directory layout.** RECON.md maps the header, the name
   list and the string table; the fixed-size records past the strings are not
   done. Nothing loads until they are.
5. **Plan interface shims, not import shims** — and start at `CDD7MemRenderer`.
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

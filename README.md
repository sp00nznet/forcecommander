# Star Wars: Force Commander — Static Recompilation

Static recompilation of **Star Wars: Force Commander** (LucasArts, 2000) from
its shipping Win32 binary to native C.

Built on the [pcrecomp](https://github.com/sp00nznet/pcrecomp) toolchain, and
next to [xwa](https://github.com/sp00nznet/xwa) — same publisher, same year,
same studio's tooling.

## Project Status: **P0 and P2 complete. P1 in progress.**

The phases went out of order on purpose, and that turned out to be the right
call — see [P1 came second](#p1-came-second-and-should-have).

---

## P0: what the binaries are

| Binary | Size | `.text` | Built | Role |
|--------|-----:|--------:|-------|------|
| `Focom.exe` | 4,943,872 | **3,936,882** | 2000-03-03 | the game |
| `Force.exe` | 106,496 | — | 2000-02-29 | launcher |
| `Smush.dll` | 147,456 | — | 1999-02-27 | LucasArts SMUSH video (4 exports) |
| `mss32.dll` | 328,704 | — | 1999-01-04 | Miles Sound System |
| `FocomSetup.dll` | 843,776 | — | 2000-02-29 | installer |

MSVC 6.0, and it imports `MSVCP60.dll` — so the C++ standard library and the
CRT are both *dynamic*, which is a smaller classification problem than a
statically linked CRT (cf. Monster Truck Madness).

**No DRM.** Entropy 5.31–6.45 across every section, no wrapper sections, no
packer. Nothing to dump, nothing to unwrap. There is no `.reloc` section —
fixed image base, which is what `runtime/recomp32/image_loader.c` already
assumes.

3.94 MB of `.text` makes this the second-largest target in the collection.
Only Rise of Legends (13.25 MB) is bigger, and Rise of Legends is explicitly
the stress test rather than a project. This is roughly 1.5× X-Wing Alliance and
4× Crimson Skies.

### The import table understates the problem

307 imports across 11 DLLs — and **no DirectDraw, no Direct3D, and exactly one
DirectInput function**:

```
KERNEL32 63   MSVCP60 82   MSVCRT 67   USER32 44   mss32 22
WINMM 8   GDI32 11   ADVAPI32 5   ole32 3   SHELL32 1   DINPUT 1
```

The graphics stack is not imported. It is loaded at runtime — `DDRAW.DLL`,
`SMUSH.DLL`, `FEELIT.DLL` and `DINPUT.DLL` all appear as name strings for
`LoadLibrary`, with `DirectDrawCreate` / `DirectDrawCreateEx` /
`DirectDrawEnumerateA` / `DirectDrawEnumerateExA` resolved by
`GetProcAddress` — and DirectPlay, DirectMusic and the video pipeline come in
through **COM**, via the three `ole32` entry points:

```
CoCreateInstance() construction of IDirectPlay4A interface
CoCreateInstance() construction of IDirectPlayLobby3A interface
Couldn't create CLSID_DirectMusic
```

Three renderer paths are named in the strings: `Direct3D hardware renderer`,
`Direct3D RGB renderer`, `DirectDraw, memory-lock only renderer`.

This matters for the runtime plan. `runtime/compat/win32_compat.h` sorts 275
**imported** Win32 APIs into keep/shim/SDL2/stub. That machinery does not reach
an interface pointer obtained from `CoCreateInstance` and called through a
vtable — which is the same class of problem the Encarta 97 work built
`runtime/hybrid/` for. Budget for interface shims, not just import shims.

`FEELIT.DLL` is Immersion's force-feedback driver, and there is a
`GamePPGlobalSysFEELitMouseManager` class to go with it. Genuinely obscure.

---

## P2: the classification is free, because RTTI was left on

**567 classes, 1,121 vtables, 5,436 virtual methods, recovered in seconds.**

```
python ../tools/tools/cpp/rtti.py game/Focom.exe -o analysis/rtti.json \
                                                 --seeds analysis/rtti_seeds.json
  type descriptors          606
  complete object locators  1,121
  vtables                   1,121
  classes                   567
  virtual methods           5,436  (4,832 attributable to one class)
```

For scale: **Black & White has 569 types, and they were recovered by hand.**
That project is what the whole of `tools/cpp/` was built for. This is the same
number of classes, with their names, their vtables and their inheritance
chains, out of one command — because LucasArts shipped a release build with
RTTI enabled.

The contrast with Rise of Legends is sharper still. Its standing problem is
*25,513 functions reachable only through vtables, and no RTTI to lean on*. Here
the vtables describe themselves.

### The engine has a name

Six source paths survived in assert strings, and they name the whole thing:

```
c:\ronin\gamepp\GamePPMultiplayer/GamePPSubsystemIdioms.h
C:\ronin\gamepp\gameppmultiplayer\GamePPMultiplayerConnectionDirectPlayHelpers.h
```

The engine is **GamePP**; the studio tree is **`ronin`**. The RTTI names sort
into eight libraries with no guessing required:

| Library | Classes | vtable slots | What it is |
|---|---:|---:|---|
| `GamePPGlobal*` | 145 | 4,274 | Platform/game layer — resources, events, managers, **and the script AST** |
| `RE3D::` | 107 | — | The render engine. `CD3D7GeometryRenderer`, `CD3D7MaterialManager` — a **D3D7** backend |
| `Ronin::` | 77 | 632 | Studio foundation: a virtual filesystem, threads, bitmap loaders, math |
| `GamePPSys*` | 67 | 1,650 | Engine core: Object, Process, Event, Message, Resource, Library, Variable, State, Undo |
| `GEAPI::` | 53 | — | Geometry/animation API — object, material and animation factories over `TQuat`/`TVector`/`TMatrix`/`TTransf` |
| `DX7::` | 26 | — | DirectX 7 wrapper |
| `GamePPVis*` | 25 | 1,053 | **A visual editor.** Shipped in the retail binary. |
| `GamePPMultiplayer::` | 20 | 655 | DirectPlay |
| `Focom*` | **6** | 161 | **The entire game-specific surface** |

82% of classes have a recovered base class.

### Two findings that change the plan

**1. Six classes are game-specific. Six, out of 567.**

```
FocomStartup                       FocomEmitterObjectCallback
FocomPassCallback                  GamePPFocomSystemManager
GamePPFocomLandscapeResourceData   GamePPFocomLandscapeResourceType
```

This is the Gunman Chronicles shape — *78% of the binary is the SDK, and only
499 of 3,990 functions need real work* — except Gunman needed a four-pass
classifier to **prove** which functions were SDK, and that classifier is what
all of `tools/classify/` is. Here the answer is in the type names. Force
Commander is a thin game on a large unreleased in-house engine, and the
recompilation is mostly an **engine** recompilation.

That also means the payoff is bigger than one title. GamePP was Ronin's engine;
anything else built on it becomes cheap once this exists.

**2. Mission logic is a visual script, not native code.**

38 of the `GamePPGlobal*` classes are AST nodes with vtables:

```
If  Else  ElseIf  For  EndFor  While  EndWhile  Switch  Case  CaseOr  CaseRange
CaseOrRange  DefaultCase  Do  Loop  LoopForever  LoopVar  EndLoop  EndIf  And
Or  Assert  Comment  NOOP  Stop  CallParent  EnumDef  ArrayTypeDef  ConstTypeDef
MessageTypeDef  ResourceTypeDef  EventFunctionTypeDef  Argument{Expr,VarDec,...}
```

Together with `GamePPSysCodeBlock`, `GamePPSysCodeContainer`,
`GamePPSysLibraryProc`, `GamePPVisCodeBlock`, `GamePPVisCodeClipboard` and the
`info.pro` / `code.bin` filenames in the data section, that is a complete
in-house visual scripting language *plus its editor*, compiled into the shipped
game.

So Force Commander joins Encarta 97, the Magic School Bus and Prodigy on the
same shelf: **the interpreter is the code, and the behaviour is data.**
Recompiling the binary gets the VM; `code.bin` is the other half of the project.

`Ronin::CRPKPackFile` / `CRPKFile` / `CRPKFolder` behind
`Ronin::IFileSystem_class` says the assets live in an **RPK virtual
filesystem** — that is the asset-extraction target, and it is not a format
`tools/assets/` currently knows.

---

## P1 came second, and should have

`tools/cpp/rtti.py` says so in its own docstring:

> **The method addresses are proof of function entry points**, which is why
> this is worth running *before* disassembly rather than after.

The first recursive-descent run here was launched unseeded and then thrown
away, because RTTI had already produced 5,436 addresses that are *known* to be
function starts. On a 3.94 MB C++ binary with 1,121 vtables that is not a
marginal improvement — a method reached only through a vtable slot is called by
no `CALL` anywhere in the image, so a branch scan never names it.

### Two independent recoveries, cross-checked

| | methods |
|---|---:|
| `rtti.py` — exact, from CompleteObjectLocators | 5,436 |
| `vtable_scan.py` — structural, runs of code pointers in `.rdata` | 5,670 |
| **agree** | **4,982** (91% of RTTI) |
| RTTI only | 454 |
| structural only | 688 |
| **union → `analysis/seeds_union.json`** | **6,124** |

Worth keeping both. The 688 the structural scan finds alone are most likely the
`DX7::`/`RE3D::` COM-style interfaces, whose vtables are real but carry no
CompleteObjectLocator; the 454 RTTI finds alone are vtables the structural
heuristics rejected. `vtable_scan.py` exists for binaries built with RTTI off,
and this target shows it is still worth running when RTTI is on.

```
vtable candidates 847 (.rdata 843, .data 4)
slots per vtable  min 3, median 8, max 358
largest named     GamePPProdBase 223, GamePPVisBase 222,
                  GamePPGlobalDataRE3DObject2D 162, GamePPVisObjectTemplate 155
```

### Still running

`disasm32.py` with `--seed-functions analysis/seeds_union.json` over 3.94 MB has
not finished yet, so there is no recursive-descent catalog and **no Phase 1b
score**. When it lands, the scoring here will be unusually strong: the 5,436
RTTI method addresses are real ground truth, not a second opinion, so
`score_recovery.py` will give a precision/recall figure that actually means
something — unlike a reference reconstructed from a partial disassembly.

That comparison is worth doing carefully. It is the first chance in the
collection to score 32-bit recovery against symbols on a large C++ binary, and
whatever it says about `disasm32.py`'s vtable blindness is a toolkit finding,
not a project one.

---

## Where it goes next

1. **Finish the seeded disassembly, then score it** against `analysis/rtti_seeds.json`.
2. **Split the work by library, not by address.** `GamePPSys*` (67 classes) is
   the bottom of the dependency graph and the place to start; `GamePPVis*` (25
   classes, 1,053 vtable slots) is the *editor* and can almost certainly be
   stubbed out entirely for a first playable, which is a free 1,000 slots.
3. **`Ronin::` first of all, actually.** 77 classes of filesystem, threading and
   bitmap loading with no game logic in them — the easiest possible warm-up on
   this binary, and `CRPKPackFile` is needed before any asset loads.
4. **Plan interface shims, not import shims.** DDRAW/D3D7 via `LoadLibrary` +
   `GetProcAddress`, DirectPlay and DirectMusic via COM. `runtime/hybrid/` is
   the nearest existing machinery.
5. **DirectPlay is a dead service.** Whatever replaces it is a design decision.
6. Disc 2 has not been unpacked.

`Smush.dll` is the one piece that needs no reverse engineering — SMUSH is
documented and ScummVM has implemented it for two decades.

---

## Layout

```
forcecommander/
  original/   both discs (.bin/.cue, and fc1.iso converted from disc 1)
  game/       extracted binaries
  analysis/   catalog, sections, imports, rtti.json, vtables.json, seeds
  docs/
```

## Credits

Star Wars: Force Commander © 2000 LucasArts Entertainment Company. This project
neither contains nor distributes any part of it.

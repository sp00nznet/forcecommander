# What Force Commander does before it draws anything

Recovered by running it. Every step here was a blocker, in this order.

## Focom.ini is a script, not a settings file

The game reads `<installdir>\Focom.ini` and will not start without it. It is not
an INI -- there are no `GetPrivateProfile*` imports at all. It is a list of
directives, one per line, read with `sscanf(line, "%4095s %n")` and dispatched
on the keyword against the game's own table at 0x0083E5E4:

    Wait  Run  Options  Players  GameFiles  Music  Movies  Workspace
    RPKDir  RPKFile  InitBase  SetRegKey  HideLoadingPanel  ShowLoadingPanel
    LoadAppFileName  CheckCD  CheckAppMutex

The disc ships `Install\Focom.ini` as **zero bytes** -- the installer writes the
real one at install time with paths substituted. So a copy lifted off the disc
is empty, nothing parses, and the game dies dereferencing a member that
`InitBase` would have filled.

The template is the installer's own, read out of the compiled InstallShield
script `Install\setup.ins` (its string literals are plain text in the bytecode;
search for `CheckAppMutex`). `tools/make_focom_ini.py` writes it for a given
install directory. `RPKDir` really is emitted twice.

## The install layout is not the disc layout

`Resource\*` on the disc is copied into `<installdir>\Resource\`, and every path
in the script is absolute and goes through `Resource\`:

    <installdir>\Focom.ini
    <installdir>\Focom.exe
    <installdir>\Resource\appname.ini
    <installdir>\Resource\forcecommand\forcecommand.{rpk,gpl}
    <installdir>\Resource\forcecommand\project.gam
    <installdir>\Resource\{Movies,Music,GameFiles,Players}\

`GameFiles` and `Players` are not on the disc; the installer creates them empty.

## Settings live in the registry, not on disk

Under `LucasArts Entertainment Company LLC\Force Commander\v1.0`, with
`Settings\Screen` (`UseLowQualityTextures`) and `Settings\Game`
(`UseMipmapping`, `LODLevel`, `FogLevel`, `Brightness`). The game creates the
key itself and writes its own defaults, so the only thing a host has to do is
let `Reg*` through to a real registry -- there is nothing to invent. The error
string on the failure path is its own: "Failed to create file system registry
object!".

## Paths must come back absolute

The game calls `GetCurrentDirectoryA` / `GetModuleFileNameA` and feeds the
result straight into `_fullpath`. A shim answering with a relative `"game"` gets
it resolved a second time, and every path the game derives is then one directory
too deep -- it looked for `game\game\Focom.ini`, did not find it, and exited 0
without a word.

## The sequence, as it runs

    CheckAppMutex FORCE        CreateMutexA; a NULL handle reads as
                               "another instance is already running"
    CheckCD <installdir>       GetVolumeInformationA; disc 1 is labelled FOCOM_1
    LoadAppFileName ...        reads Resource\appname.ini
    ShowLoadingPanel           CreateDialogParamA(101), whose dialog procedure
                               is the game's own lifted code at 0x00401770 --
                               the same one that draws the splash. It paints.
    InitBase                   builds GamePPProdBase, creates the registry key,
                               and spawns a service thread
    RPKDir / Workspace / ...   mount points for the .rpk and the .gpl path map
    Run 2 1 6
    HideLoadingPanel

## Where it stops today

`Run 2 1 6` is `GamePPProdStartup` slot 19, forwarding to `GamePPProdBase`
slot 61 (`sub_0051CE70`), which calls slot 14 on the **GamePPVisProcessManager**
at `GamePPProdBase + 0x94` (vtable 0x007C58B4). The three numbers are an
object-template id: `Trasse - Day/info.pro`, whose `inheritID` is `2 1 12` =
`Endor - Dawn`, the template every mission inherits its code from.

The process is started with a **boot thread**, and a boot process lives exactly
as long as its boot thread: `sub_005263D0` removes it the moment
`[thread+0xC]`, the OS handle, goes to zero, and a Ronin thread body runs
exactly once. The boot thread's body is `CProcessThread::Tick`
(`sub_00524A80`) -- `GetTickCount()`, a delta, and `process->Update(dt)`. So
the game's whole first section is that loop, and its length is the answer to
"how far does this get".

### The HAL device was not optional

For a long time the section ran **61 frames with no renderer at all** and then
finished, cleanly, with no error reported anywhere. The reason was one number.

`sub_007321C0` is the gate that decides whether a `CDD7Device` can have a
screen. It reads `dwDeviceRenderBitDepth` at a **hardcoded** offset,
`device + 0x454`. CDD7Device keeps all four enumerated `D3DDEVICEDESC7`s
inline, filed by GUID by the callback at `sub_0076ACC0`:

| slot | GUID at | description at | `dwDeviceRenderBitDepth` |
|---|---|---|---|
| TnLHal | 0x007CE6B0 | device+0x2D4 | device+0x348 |
| **HAL** | 0x007CE670 | device+0x3E0 | **device+0x454** |
| RGB | 0x007CE660 | device+0x4EC | device+0x560 |
| Ref | 0x007CE690 | device+0x5F8 | device+0x66C |

The shim offered only the RGB software device, so the HAL slot stayed zeroed,
`test ah,4` for `DDBD_16` failed, and everything after it in the constructor
was skipped -- the `CDD7WinScreen`, the screen registration, the renderer
selection. The RGB description was sitting correctly in slot 3 at
`device+0x560` (0x700 = DDBD_16|24|32) the whole time, which is how the
offsets were confirmed rather than guessed.

The comment in `ddraw_shims.c` had predicted this: *"offer the HAL device too
if the game turns out to insist on one."* It insists.

### Four more infidelities behind it

Each was found by the next fault, and each is the same kind of thing --
a hand-written Windows reimplementation that was not quite the real one.

- **`SetDisplayMode` takes five arguments on IDirectDraw7**, not the three
  IDirectDraw v1 took, and the shim purged for three. Eight bytes of refresh
  rate and flags stayed on the target stack; the caller popped its saved
  registers off the wrong slots and `CUtilityDevice::SelectRenderer` came back
  from creating the screen with `this == 0`. An audit of all 30 IDirectDraw7
  slots against the header signatures found `StartModeTest` and `EvaluateMode`
  wrong the same way.
- **`GetSurfaceDesc` and `Lock` filled in a v1 `DDSURFACEDESC`** -- `dwSize`
  108 where the DX7 interfaces want 124, and the `DDSCAPS2` tail left holding
  whatever the caller had there.
- **`GetDDInterface` was a stub** that returned DD_OK and left its out pointer
  alone. RE3D calls straight through the result.
- **`GetAttachedSurface` returned the surface ITSELF** when nothing was
  attached, on the theory that non-NULL was the safer answer. It is the
  opposite: the DX7 texture manager walks a mipmap chain until
  GetAttachedSurface *refuses*, so that made the walk run forever -- 280,000
  iterations, each appending to two vectors, until the whole gigabyte of target
  heap was gone and the failure surfaced as a null-pointer `memset` inside
  msvcrt.dll with nothing to connect it to the cause.

### A real allocator, and a lifter bug

The target heap was a bump allocator that never reused anything, with a note
saying to replace it when a run actually ran out. A run ran out: 1,069,123,488
bytes in 641,500 allocations, because the game frees constantly (operator
delete at 0x0056EAD0 forwards to MSVCRT `free`) and nothing came back. It is
now size-binned free lists -- exact-size reuse up to 4 KB, first fit above.

And the third real lifter bug this target has found:
`linear_disassemble_function` stopped at the first `int3`, which is right for
inter-function padding and wrong for the `int3` MSVC emits *inside* a function
as the unreachable fallthrough of `__assume(0)`:

```
0077C86F  jbe  0x77c872
0077C871  int3
0077C872  <the rest of the function>
```

Everything the `jcc` jumped over was dropped -- silently, because the extent
walk had the right answer and 0x0077C872 was already a known leader, so the
body kept a label it could not place, `generate.py` turned that into a tail
transfer, and `RECOMP_ITAIL` cannot resolve a VA that was never lifted. The
transfer did nothing and the function fell through to its caller. One line of
evidence: `ITAIL: unresolved VA 0x0077C872 from 0x0077C6E0`. Fixed upstream in
pcrecomp; 5 bodies in this binary change and 431 instructions come back.

### What the run does now

Sixty-nine distinct COM methods, in this order:

```
SetDisplayMode 640x480 16bpp
CreateSurface  640x480 16bpp PRIMARY|FLIP|COMPLEX|3DDEVICE -> primary + back buffer
EnumZBufferFormats -> 3 formats
CreateSurface  640x480 16bpp ZBUFFER              -> attached to the render target
IDirect3D7::CreateDevice                          -> device
GetCaps / EnumTextureFormats / GetRenderTarget / GetDDInterface
Surface::Lock / Unlock, IDirect3DDevice7::Load    -> 32x32 and 128x128 textures
SetRenderState / SetTexture / SetTextureStageState
BeginStateBlock / EndStateBlock / SetMaterial
GetDirect3D / CreateVertexBuffer
GetDeviceIdentifier / TestCooperativeLevel / Surface::IsLost
DirectInputCreateA -> CreateDevice / SetDataFormat / SetCooperativeLevel
                      GetProperty / Acquire / GetDeviceState
CoCreateInstance(CLSID_DirectSound) -> CreateSoundBuffer / SetFormat / SetVolume
                                       Lock / Unlock / Play / Stop / Release
CoCreateInstance(DirectPlay, DirectPlayLobby) -> EnumConnections,
                      RegisterApplication, GetConnectionSettings
```

`IDirect3DDevice7` is implemented: all 49 methods, with the purge count of each
taken from the headers rather than guessed, state setters that remember and
getters that hand back, a real `Clear` that clears the render target, and the
`DrawPrimitive` family accepting and counting its vertices. `IDirectSound` and
`IDirectSoundBuffer` are implemented the way a working sound card with no
output device answers -- buffers that can be locked, written and reported
stopped, and nothing played.

Two more things had to be wired that only a fault revealed:

- **`DINPUT.dll!DirectInputCreateA` is a static import.** Everything else in
  DirectX arrives through LoadLibrary or CoCreateInstance, so the shim was
  reachable only through the GetProcAddress hook; the version probe asks
  GetProcAddress for it, but `GamePPGlobalSysMouseManager` calls the import.
  That went to the generated stub, which returns without filling the out
  pointer.
- **`dinput_init()` had no caller at all.** The DirectInput vtables were built
  by a function nobody ran, so the object came back with a vptr of 0. Nothing
  had exercised the path, because the probe only ever asked for the address.

### Nothing is drawn, and that is not a presentation problem

`BeginScene`, `Clear`, `DrawPrimitive` and `Flip` are never called, and
`CDD7FSScreen`'s present (`sub_007363B0`, slot 10 of vtable 0x007D87A0) is
never reached. For a while the obvious reading was that the frame is composed
and simply never shown.

It is not. `--dumpframe PATH` writes the Direct3D render target to a BMP from
whichever lifted thread is running -- no window, no message pump, no second
thread -- and prints how much of it is not black:

```
[dump] work/frame.0.bmp: surface 0x1000AB60 640x480 16bpp, 0 of 307200 non-black
[dump] work/frame.1.bmp: surface 0x10D62090 640x480 16bpp, 0 of 307200 non-black
```

Both the primary and the render target are **entirely black**. Nothing has been
drawn into them, so the missing `Flip` is a consequence, not the cause. That
retires the whole "find the present" line of enquiry.

### The boot script, as it actually runs

`--scripttrace` now prints, for every line the interpreter steps, the block, the
line number, the line's thunk and the class that implements it. The class is
the useful part and it takes one indirection more than it looks: `[line+4]` is
always the `GamePPVisLibrary` *wrapper*, whose vtable is the same for every
line; the object that implements the particular script function is at
`wrapper+0x20` (`sub_00517FA0` is nothing but a forward to it) and its vtable is
one class per script function, which `analysis/rtti.json` names.

Interleaved with `--argtrace 0x0052C300` (`GamePPVisLibraryManager::GetLibrary`,
whose one argument is a subsystem id that `tools/scriptmap.py` names) that makes
the boot script readable. It does this, in order:

1. a block of 32 lines initialises the RE3D **stages** -- `RE3DStage` appears on
   nearly every line, and three sub-blocks of 7, 6 and 16 lines are each run
   several times, once per stage. The names in `Endor - Dawn/code.bin` match:
   `Init Stage 2D Virtual`, `Init Stage 3D`, `Init Post Engines`, `Init EXE`,
   "Initialize the 3D rendering stages", "Initialize the old engines".
2. a block of 19 lines starts script threads (`GamePPGlobalThread`) and runs
   one of their bodies (25 lines) inline.
3. a block of 6 lines ends on **`GamePPGlobalSysWaitForever`**.
4. a block of 25 lines starts one more thread -- whose block compiles
   `Loop Forever`, `Smush`, `If`, `Wait`, `EndLoop`, i.e. the **intro movie
   player** -- and then stops on **`GamePPGlobalSysWaitIf`** at line 6 of 25.

Both remaining contexts are waiting, the movie thread's body never steps, and
the section ends.

### SMUSH.DLL was never resolved

`Resource/Smush.dll` ships with the game and the exe loads it by name:

```
[dll] LoadLibraryA("SMUSH.DLL") -> 0x7F050000 (stub module)
[dll] GetProcAddress("SmushStartup")   -> NULL (not implemented)
[dll] GetProcAddress("SmushPlay")      -> NULL (not implemented)
[dll] GetProcAddress("SmushShutdown")  -> NULL (not implemented)
[dll] GetProcAddress("SmushSetVolume") -> NULL (not implemented)
```

The loader at 0x0068BE32 handles that gracefully -- one NULL proc and it
`FreeLibrary`s the module and zeroes the handle -- which is why it never showed
up as a fault. The four are shimmed now; the signatures come from their only
caller, `sub_0068D190`, which is the movie player itself:

```
SmushSetVolume(vol)                                    0x0068D283
SmushStartup(surface)                                  0x0068D296
SmushPlay(name, 0xF, 0,0,0, 640, 480, -1, 0,           0x0068D2E8, 13 args
          callback, 1, 1000000, 1000000)
SmushShutdown()                                        0x0068D30D
```

`SmushPlay` is blocking -- the caller sets its state to 4 and shuts down on the
next line -- so returning at once is exactly "the movie finished", which is what
pressing a key during the intro does on real hardware. `Resource/Movies` is
empty in this install anyway, so there is nothing to decode even if decoding
were wanted.

That is a real infidelity fixed, and it is not enough: the movie thread's block
still never steps.

### Why the section retires

`CProcessThread::Tick` (`sub_00524A80`) is one frame: `GetTickCount`, a delta,
and `process->Update(dt)` through slot +0x118. The thread ends when `Update`
returns false, and a boot process lives exactly as long as its boot thread.

`Update` is `sub_005261C0`, and its return is one line:

```
return sub_00526A20(...) != 0  &&  this->quit == 0;     // quit at +0x258
```

The quit byte is never written: its only two writers, `sub_00524920` (a
one-instruction setter) and `sub_00524F50`, have a call count of zero in a full
`--calltrace`. So `sub_00526A20` returned 0, and `sub_00526A20` answers "did any
context advance this frame" -- it starts by testing `[this+0x234]`, which
`Update` zeroes immediately before the call.

So the engine retires a process the moment no context can advance. With one
context on `Wait Forever` and one on a `Wait If` that never clears, that is
every frame. 61 frames of it in one run, 164 in another, all of them empty: the
whole script runs in one frame near the end and then everything stops.

### The Wait If is load-bearing

`--nowait` replaces `GamePPGlobalSysWaitIf::Execute` (`sub_005D4350`, vtable
0x007D1408 slot 20) with a "condition false" stub through
`recomp_lookup_manual`, which `RECOMP_ICALL` consults before the dispatch table.
It is a probe, not a fix, and what it proves is that the wait is not spurious:
the script runs on and faults in `sub_006BF7B0` at

```
esi = [ebp+0xC] + i;  eax = [esi+0x28]      /* esi = 0 */
```

-- a container whose count at `[ebp+0x30]` is greater than zero while its array
pointer at `[ebp+0xC]` is null. Something the script waits for is meant to fill
that array, and running past the wait reaches the consumer before the producer.

So the next thread to pull is the wait itself: `WaitIf` evaluates its condition,
then calls `sub_00506EC0`, and on a true answer records the resume line with
`sub_00507030` and sets `ctx->flags |= 4`. `sub_00506EC0` walks an event queue
(`sub_00506FA0`, then `[queue vtable + 4]` and `[event vtable + 0x58]`). What
posts to that queue, and which post never happens, is the question.

### What is not the cause

Ruled out by measurement, so as not to be re-guessed:

- **Not the present.** The surface is black. See above.
- **Not a missing script subsystem.** A probe on `GetLibrary` that reads the
  1024-entry table directly and logs every NULL shows only ids 934 and 935
  failing, twice each, in an optional-feature probe well before the script runs,
  plus a full 0..1023 enumeration sweep. Every id the boot script names
  resolves.
- **Not a null process manager.** `GamePPProdBase + 0x94` holds a live
  GamePPVisProcessManager. An earlier note here said otherwise; that came from
  a `--poison` address captured in a *different run*, and allocation order
  varies once the game's own threads are up.
- **Not thread starvation.** Coarse preemption of the machine lock was
  implemented and tried at every 500, 5,000 and 50,000 function entries, twice,
  once at two threads and once at four. No change: of the four Ronin workers two
  carry the load and the other two run 13 and 8 lifted calls and then park in
  their own queue-consumer wait, which is correct behaviour for an idle worker.
- **Not a wall-clock timeout.** `--waitscale 100` changes nothing.
- **Not an error the game noticed.** The game ships with its assert and log
  machinery LIVE -- about 2,500 sites test a byte at 0x00833878 and report
  through a `std::fstream`. Startup clears it from a setting named
  `permitSyncChecking` that has to read `"on"`, so the reports were being
  skipped. With `--poke 0x00833878 1 0x0052C300` the log opens and flushes and
  the report formatter is never entered once.
- **Not the device identifier**, though that was wrong too.
  `IDirectDraw7::GetDeviceIdentifier` was a "return DD_OK and write nothing"
  stub, and the game read the uninitialised 1068-byte buffer straight back and
  logged it (`Driver: ''`, `VendorId: 00000030`, `WHQLLevel: 117abb00`). A 1999
  title reads that to decide which card-specific workarounds to switch on, so
  stack garbage there is a coin flip on every one of them. It now reports an
  unknown vendor with a WHQL-signed driver -- the combination that matches no
  blacklist. Fixing it changed nothing visible, which is the point of writing it
  down.

## Running it

From the **project root**, not from `game/`:

```
$ build/focom.exe game/Focom.exe --run --watchdog 180
```

Launched from inside `game/` the game returns from `WinMain` immediately after
its 358 static constructors, before a single `Focom.ini` directive runs. The
path resolution in the ini is absolute, so this is not about finding files; it
has not been chased further because the root invocation is the one that works.

Useful flags: `--scripttrace` (per-line class names, plus the `[nolib]` probe),
`--argtrace 0x0052C300` (subsystem ids per line), `--dumpframe PATH` (the render
target as a BMP), `--calltrace FILE`, `--nowait` (the Wait If probe).

### A build that fails without saying anything

If `cmake --build build` reports `FAILED` with no compiler diagnostics at all,
`C:\msys64\mingw64\bin` is missing from `PATH`. `gcc.exe` is found by absolute
path and starts, but `cc1.exe` lives under `lib/gcc/...` and loads
`libmpfr-6.dll` from `mingw64/bin` at run time; without it the loader kills cc1
before it can print, and gcc exits 1 with an empty stderr. Running `cc1.exe`
directly is what says so:

```
cc1.exe: error while loading shared libraries: libmpfr-6.dll: cannot open ...
```

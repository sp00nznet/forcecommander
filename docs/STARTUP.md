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

## How it got here, in order

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

### Nothing was drawn, and it was not the presentation

`Flip` was never called, which for a long time read as "the frame is composed
and never shown". It was not. `--dumpframe` writes the frames the game presents
to a BMP and counts their non-black pixels, and before the fix below both the
primary and the render target were **entirely black**. Nothing had been drawn
into them.

### The boot script, as it actually runs

`--scripttrace` prints, for every line the interpreter steps, the block, the
line number, the line's thunk and the class that implements it. The class takes
one indirection more than it looks: `[line+4]` is always the `GamePPVisLibrary`
*wrapper*, whose vtable is the same for every line; the object that implements
the particular script function is at `wrapper+0x20` (`sub_00517FA0` is nothing
but a forward to it) and its vtable is one class per script function, which
`analysis/rtti.json` names. `[line+8]` is the argument block and `[args+8]` the
opcode a subsystem switches on -- `GamePPGlobalThread`'s Execute is a 44-way
jump table on exactly that value.

Interleaved with `--argtrace 0x0052C300` (`GamePPVisLibraryManager::GetLibrary`,
whose one argument is a subsystem id that `tools/scriptmap.py` names) the boot
script becomes readable. It does this, in order:

1. a block of 32 lines initialises the RE3D **stages** -- `RE3DStage` appears on
   nearly every line, and three sub-blocks of 7, 6 and 16 lines are each run
   several times, once per stage. The names in `Endor - Dawn/code.bin` match:
   `Init Stage 2D Virtual`, `Init Stage 3D`, `Init Post Engines`, `Init EXE`,
   "Initialize the 3D rendering stages", "Initialize the old engines".
2. a block of 19 lines starts script threads and runs one of their bodies
   (25 lines) inline.
3. a block of 6 lines ends on `GamePPGlobalSysWaitForever`.
4. a block of 25 lines starts one more thread and waits on a
   `GamePPGlobalSysWaitIf` at line 6.

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
empty in this install anyway.

### What was actually killing it: MsgWaitForMultipleObjects

Every reading above pointed at the script. The script was fine. Its thread was
being shot.

`sub_00550F60` is one step of a Ronin thread:

```
r = MsgWaitForMultipleObjects(1, &thread->stopEvent, FALSE, 25, QS_ALLINPUT)
if (r == WAIT_TIMEOUT)  return thread->Tick()      /* slot 3 -- one frame */
if (r == 0)             return false               /* stop */
else                    pump messages; return true
```

and `false` is answered by the caller clearing `[thread+0xC]`, the OS handle,
which `sub_005263D0` watches: a boot process lives exactly as long as its boot
thread.

The shim passed **nCount = 0** with no handle array -- written when the target
had no working threads, on the reasoning that there was nothing else to wait on
-- and read its timeout from `ARG(2)`, which is `bWaitAll`. The return value of
this function is a *position in the handle array*, so with nCount = 0 "a
message arrived" comes back as `WAIT_OBJECT_0 + 0`, which is byte-for-byte what
"handle 0 signalled" looks like. Handle 0 is the stop event.

So the first stray mouse message ended the game's first section, and the frame
count that varied run to run -- 61, 68, 164 -- was just how long it took one
message to arrive. Two things follow that are worth keeping:

- **The Wait If was never the problem.** `--nowait` (which replaces
  `GamePPGlobalSysWaitIf::Execute` with a "condition false" stub through
  `recomp_lookup_manual`) made the script run on and fault in `sub_006BF7B0` on
  a container whose count was positive and whose array was null. That reads as
  "the wait is load-bearing", and it is, but it was load-bearing on a producer
  that had been killed, not on one that never started.
- **An analysis of the process manager that was wrong.** `Update`
  (`sub_005261C0`) returns `sub_00526A20(...) && !this->quit`, and both were
  being read as the cause. `--watch 0x00526A20 --watchspan 0x220 0x260` settled
  it in one line: `[this+0x234]` is 6 and `[this+0x258]` is 0 on every frame
  including the last, so `Update` returned true and the manager was never the
  one giving up.

### It draws

With the handles passed through:

```
[present] #1 surface 0x10D62090 640x480: 307200 of 307200 pixels non-black
[present] #3 surface 0x10D62090 640x480: 0 of 307200 pixels non-black
[present] #4 surface 0x10D62090 640x480: 307200 of 307200 pixels non-black
```

`BeginScene`, `SetTransform`, `SetViewport`, `Clear`, `EndScene` and `Flip` all
run, `CDD7FSScreen::Present` (`sub_007363B0`) is reached, the frames alternate
solid colours as the renderer clears them, and the script gets 284 lines deep
instead of 156 -- past the Wait If, into four blocks it had never reached, and
on to loading 256x256 32bpp textures.

Two more things had to be right for that:

**IDirect3DVertexBuffer7**, nine methods. The game locks a vertex buffer on its
first rendered frame and used to get a generic object whose slots abort. The
FVF stride is deliberately not computed -- 64 bytes a vertex is larger than any
FVF DX7 can express, and only the game writes the buffer.

**`host_present` blits straight to the window.** It used to `InvalidateRect` and
`UpdateWindow`, and `UpdateWindow` sends `WM_PAINT` -- which, for a window owned
by another thread, blocks until that thread pumps. The renderer runs on a Ronin
worker and the window belongs to the main thread, so the first `Flip` the game
ever issued hung the process: the watchdog reported no lifted call for 90 s with
`Present` at the top of the entry trace.

### The fourth lifter bug: a body can continue past a gap

The next fault came with one line of warning:

```
ITAIL: unresolved VA 0x0066E27D from 0x0066E150
```

`sub_0066E150` ends a block with `jmp 0x66e27d`, and what MSVC put in between is
not padding:

```
0066E270  cc                    int3
0066E271  b8 77 e2 66 00        mov eax, 0x0066E277     ; the EH state thunk
0066E276  c3                    ret
0066E277  8b 75 e4              mov esi, [ebp-0x1c]
...
0066E27D  <the jump target>
```

The sweep stopped at the int3 -- the rule upstreamed last time was "an int3
ends the body when the instruction after it is not a leader", and the
instruction after it is that thunk, not a leader. Four leaders past the gap
were dropped, each came out as a `RECOMP_ITAIL` to a VA nobody lifted, the
transfer silently did nothing, and the function fell through to its caller.
Several calls later a script function handed a null `this` to `sub_006369D0`,
the DX7 pixel-pipe manager's RE3D lookup.

The fix is in `tools/lift/generate.py`: an int3 whose successor is not a leader
ends the sweep, and the sweep then resumes at **the target of the unconditional
jump it last took**, if that target has not been decoded. "Something stepped
over this gap, and here is where it went" needs no judgement about the
function's extent and cannot invent a destination.

The bound was the whole difficulty, and three looser rules were measured and
thrown away first:

- continuing the linear sweep past the int3 decoded data as instructions for
  the whole extent, and one 400-function chunk came out at **104 MB**;
- resuming at the next undecoded leader looked tight, since leaders come from
  decoded branches -- but in the region of overlapping entries around
  0x005A5840 the leaders are themselves derived from garbage, and a chunk
  passed **60 MB** and was still growing;
- resuming only across a run of 0xCC/0x90 does not fix the case it was written
  for, because the gap is a code thunk.

`lift32.py` also emits `return;` for an int3 now, so the unreachable
fallthrough cannot run the next block. In isolation `sub_0066E150` goes from
four unresolvable tail transfers to 338 instructions, 42 leaders, all placed,
and no `RECOMP_ITAIL` at all.

### What is not the cause

Ruled out by measurement, so as not to be re-guessed:

- **Not the present.** The surface was black, and now it is not.
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
`--argtrace 0x0052C300` (subsystem ids per line), `--dumpframe PATH` (the frames
the game presents, as BMPs), `--calltrace FILE`, `--waittrace` (what a Wait If
decided), `--nowait` (stub its condition to false), and from the toolkit tracer
`--argobj VA N` (the vtable and first 0x40 bytes of argument N).

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

And the reverse: `run_lift.py` needs the *Windows* Python, the one with
capstone installed. With `msys64/mingw64/bin` first on `PATH` its own `python`
wins and the lift dies on `ModuleNotFoundError: No module named 'capstone'`.
Prepend the mingw directory for `cmake --build` and for nothing else.

## Where it is now

The section renders and presents. The frontier is `sub_00755280`, slot 7 of a
secondary base of `CD3D7Renderer@RE3D` and `CD3D7GeometryRenderer@RE3D`, which
faults on a null first argument: `sub_0071E850` reads
`[[renderer+0xC]+0x98]`, writes it to its out parameter and returns whether it
is non-zero, and it is zero. That pointer is the renderer's geometry buffer, so
the next question is which RE3D setup step was supposed to allocate it and what
this shim layer told it instead.


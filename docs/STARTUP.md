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

## It renders

The section boots into the game's **front end** and renders it. A run presents
around 3,000 frames at 640x480, the title art is on screen, and the frame
changes frame to frame:

```
[present] #2100 surface 0x11564280 640x480: 172877 of 307200 pixels non-black,
          1348332100 rasterised in 96451 prims
```

Four things closed the gap from "presents a solid colour" to that.

### 130 vtable slots were never lifted

MSVC's virtual-inheritance adjustor thunks are eight bytes:

```
00762820  sub ecx, dword ptr [ecx - 4]     ; apply the vtordisp
00762823  jmp 0x761880                     ; the real method
```

Nothing calls them and nothing falls through into them, so the catalog never
names one and `--all` never lifted one. `RECOMP_ICALL` answered the slot with
`unresolved VA` and set `eax = 0`, and that zero was an ordinary null return
value: `sub_00755280` took it for the render stage it had just asked for and
faulted on `[ebp+0x8C]` several hundred instructions later, with the one-line
ICALL warning long since scrolled off.

`run_lift.py` now unions every vtable slot address into the function list.
The bound has to be tight -- handing them the end of `.text` the way a
`--seeds` entry gets it gives the extent walk the whole 3.9 MB section to
descend through and hangs the lift on its first chunk. Script depth went from
299 lines to 485, and the run stopped faulting.

Finding it took two small additions to the toolkit tracer, both upstreamed:
`--chase VA OFFSETS` (follow a pointer chain from `ecx`, printing the vtable at
each hop) and a caller recorded beside every indirect-call target, with the
ring widened from 32 to 128 because a method that makes calls of its own
scrolls its own entry out.

### A rasteriser

`src/runtime/raster.c`. Half-space edge functions, barycentric interpolation,
gouraud diffuse, one modulated texture stage, SRCALPHA/INVSRCALPHA blending,
the alpha test and a 16-bit depth test; triangle lists, strips, fans and point
lists, indexed or not, from a raw pointer or from our own vertex buffer. It
takes host pointers and plain integers and has no dependency on the recomp
runtime, so it builds and checks itself:

```
$ gcc -DRASTER_MAIN -o raster_test src/runtime/raster.c -lm && ./raster_test
raster.c self-test OK
```

The ceilings it ships with are listed at the top of the file. The one that will
matter next is perspective-correct texture coordinates.

### IDirect3DDevice7::Load was a no-op

Which is how every texture in the game stayed black. The upload path is a
**pair** of surfaces, created back to back:

```
[dd] CreateSurface 256x256 32bpp caps=0x4401008   texture, video memory
[dd] CreateSurface 256x256 32bpp caps=0x401808    texture, system memory
```

The game locks the system-memory one, writes the image into it, and calls
`Load` to move it to the one it draws with. With `Load` doing nothing, 3.4
billion rasterised pixels came out invisible because every one was modulated
by a surface full of zeros.

### A bit depth is not a pixel format

Two fidelity bugs that only a screenshot could have found.

The font atlas is created as **ARGB1555** -- `R=0x00007C00, G=0x000003E0,
B=0x0000001F, A=0x00008000` -- and decoding it as 565 shifts every channel,
which is what painted a solid red panel over the game's own title. And a
32-bit surface comes in two flavours: an alpha mask of 0 means XRGB and its
top byte is whatever the game left there, not alpha. `O_PF` now records the
format from the `DDPIXELFORMAT` the game passed to `CreateSurface`, and
`raster.c` decodes 565, 1555, 4444, XRGB8888 and ARGB8888.

`Clear` also ignored `D3DCLEAR_ZBUFFER`, so the depth buffer held the previous
frame's depths.

## The menu, and input

### Alpha comes from the texture, not the vertex diffuse

One value explains the whole front end. It draws its menu with a vertex
diffuse of **0x00FC0000** -- a real red, and an alpha of **zero** -- against
`ALPHAOP = MODULATE, ALPHAARG1 = TEXTURE, ALPHAARG2 = CURRENT`. Reading that
literally, alpha = texture x diffuse, multiplies every glyph by zero, and with
the alpha test the game also sets ("greater than 0") the whole menu is
discarded: **6 of 500 UI draws painted anything**, and the panel was an empty
black rectangle with a border.

Treating stage 0's CURRENT alpha as opaque puts the menu on the screen:

```
Single Player
Multiplayer
View Installation
Show Credits
```

The ceiling is recorded in `raster.c`: the diffuse alpha is now ignored
everywhere, and the right shape is a real texture-stage evaluator.

### There were two windows

Clicking that menu did nothing for a long time, and the reason was not the
click.

The host makes a window at startup, because something has to exist before the
game does anything. The game then makes its own through `CreateWindowExA`,
which `shims_impl.c` binds to its real window procedure through
`win_trampoline`. Windows delivers `WM_MOUSEMOVE` and `WM_LBUTTONDOWN` to
whichever window the pointer is over -- the host's, whose procedure did not
know the game existed. **Pixels went to one window and input to the other.**

Three things were tried, and the order is the useful part:

- **DirectInput.** `DIMOUSESTATE` deltas, homed into a corner with a large
  negative delta and walked out to a known position, then `rgbButtons[0]`. It
  fired and the front end did not move a pixel: the game does not take its
  cursor from that device.
- **Presenting into the game's window** instead of the host's. It works and it
  is much worse -- **39 presents a run against 3,000** -- because cross-thread
  GDI to another thread's window is that slow, and the `ShowWindow` needed to
  make it visible SENDS a message and blocks on that thread's pump.
- **Posting window messages**, which works. The destination is the point:
  posting to the *host* window put them in the main thread's queue, and the
  main thread is inside lifted code from the entry point until the game exits,
  so nobody ever pumped them. The game's window belongs to the Ronin worker
  that created it, and that worker's step function (`sub_00550F60`) pumps every
  frame.

So `host_input()` forwards mouse and key messages from the host window to the
game's procedure -- correct, and documented as not firing yet, because nothing
pumps that queue -- and `--click X Y` (repeatable, with `--clickat` and
`--clickgap`) posts a move, a press and a release to the game's own window.

### --uimap, so clicking is not guesswork

A 2D batch's transformed bounding box is its hot rectangle, and the draws know
where the buttons are even when the text is not legible:

```
140,130  175x 30  centre 227,145    Single Player
140,174  116x 30  centre 198,189    Multiplayer
140,218  138x 30  centre 209,233    View Installation
140,262  107x 30  centre 194,277    Show Credits
 22,388   78x 78  centre  61,427    back
538,388   78x 78  centre 577,427    forward
```

`--click 227 145 --click 577 427` selects Single Player and confirms it, and
the front end navigates: every menu colour and both button glyphs change on the
first click, and the second brings up a page whose four items sit at the same
four rows with different widths, loading 102 new 64x64 textures on the way.

## The menu works. Two of its four items do.

With the mouse calibrated, `--uimap` reading the hot rectangles off the draws
and `--scripttrace` saying what each click ran, the front end is navigable and
its own dispatcher is readable.

### What the items are

`Trasse - Night/missionSelector.gtx` in the .rpk is the front end's string
table, and its keys name the whole thing: `single`, `multiplayer`, `intro`,
`credits`, `exit`, `help`, `play`, `SINGLE`, `SELECT`, `campaign`, `death`,
`scenario`, `load`, `cancel`, `namealready`, `blankname`, `MaxNames`,
`NoName`. So the four rows are **Single Player, Multiplayer, View
Introduction, Show Credits**, and the back arrow is `exit`.

### What works

```
140,130  175x30  centre 227,145    Single Player
140,174  116x30  centre 198,189    Multiplayer
140,218  138x30  centre 209,233    View Introduction
140,262  107x30  centre 194,277    Show Credits
 22,388   78x78  centre  61,427    exit
538,388   78x78  centre 577,427    forward (always disabled here)
```

- A click **selects**: the selection bar appears across the row
  (`133,131 346x31`) and the item's description at `140,315`.
- **Show Credits activates.** Sixteen script blocks that had never run start,
  with freshly allocated addresses, and the page fills with hundreds of
  15-pixel text rows scrolling between y=350 and y=440. That is the credits
  roll, and it is proof that activation works.
- **View Introduction activates** -- three new blocks, which is the intro movie
  being asked for and skipped.
- The **exit** arrow activates: four new blocks and a confirmation page, with
  three lines of prompt text in a right-hand column and a button each side
  (`165,294` dismisses it and returns to the menu; `434,294` quits, and
  quitting still hits the old teardown fault in `sub_0074E020`).

### What does not: Single Player and Multiplayer

The menu is one 217-line script block, a Switch on the row index with a Case
per item. Single Player is the Case at line 6, and `--scripttrace` gives the
path it takes exactly:

```
L2 Switch  L3 Case  L4 arg  L5 Switch  L6 Case   <- item 0
L7 Comment  L8 If  L9..L19 (eleven calls: Variable, Bool, Set, Message, Int,
                            Enum)
L20 <statement>  L21 Else  L23 EndIf             <- the branch ends normally
```

It **runs to completion** -- L21 is the Else line skipping to the EndIf, which
is how a taken branch finishes, not a bail-out. And the front end's page
controller, a 309-line block, reacts: lines 212, 213, 217, 219, 221-226 run
for the first time. So the click is seen, the handler acts, the controller
responds, and the page does not change.

The page identity is not in doubt, because the exit arrow settles it: click
Single Player, then exit, and the quit confirmation comes up (its three prompt
lines at x=311..315 and its two buttons at y=294) instead of a return to the
menu. We are still on the main menu.

An earlier reading of this said the controller's Switch "takes its
DefaultCase, so the requested page is not one it knows". That was wrong, and
`--switchtrace` is what corrected it: the 309-line controller switches on the
**current** page id, which is 7, at lines 63, 89 and 212, and its Cases
compare against 5, 11, 19 and 20. Page 7 having no special case is not a
failure.

So the handler sets state (`Set` a `Bool` `Variable`, an `Int`, an `Enum`) and
posts a `Message`, the controller runs its response, and nothing opens. What
consumes that state is the next thing to find.

What it is not:

- **Not the CD check.** `GetVolumeInformationA` reports `FOCOM_1`, which is
  disc 1's real label from the ISO primary volume descriptor.
- **Not a missing settings path**, though that was a real bug. The game's
  keyword table has `Options` and the install has an `Options\FoCom.opt`, and
  the template reconstructed from `setup.ins` never emitted the directive --
  so the game ran with no settings path at all. `make_focom_ini.py` emits it
  now, and the argument is the FILE: given the directory the handler faults in
  `sub_006847E0` constructing a `std::string` from a null pointer, and given
  the `.opt` it runs clean. It changes nothing here.
- **Not the window messages.** The game's own procedure
  (`sub_00665D10` -> `sub_00665500`) dispatches messages 7..0x100, 0x101 and
  0x102..0x112 and sends everything else to `DefWindowProc`, so WM_MOUSEMOVE
  (0x200) and WM_LBUTTONDOWN (0x201) reach it and are thrown away. The mouse
  is DirectInput only. Keys DO reach it, and Enter and Space change nothing.
- **Not the click shape.** One tap, two taps, 60 ms and 2 s holds, a second
  click on the same row, and the forward arrow after a selection: all only
  select.
- **Not the game's own diagnostics.** With `--poke 0x00833878 1` the assert
  and log machinery is live and reports nothing.

`Resource/Players` and `Resource/GameFiles` are empty, and the disc does not
carry them -- they are created at runtime -- so a missing player profile
remains the best guess. `missionSelector.gtx`'s `NoName`, `blankname`,
`namealready` and `MaxNames` say the front end has a name-entry flow, and both
gated items are the two that would need a name.

## Where it is now

Not in a mission. The front end renders, takes input, and activates two of its
four items; the two that lead to a game stop on one script condition, which is
named above to the line.

Booting a map directly is not a way round it:
`tools/make_focom_ini.py --run "2 1 7"` points `Run` at `Tatooine - Day`, the
game gets 625,000 allocations of template compilation deep and then exits
cleanly, because that template's `inheritID` is `0 0 0` and the engine and
renderer init live in the front end's own script.

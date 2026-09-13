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

### And still nothing is drawn

`BeginScene`, `Clear`, `DrawPrimitive` and `Flip` are never called. The section
now runs **68 frames** instead of 61, does all of the above, and still ends.

The line-level trace (`--scripttrace`, which prints the block, the line number
and the class of each line's library object) puts the stop precisely: a block
of **25 lines stops after line 6**. It does not run out of lines; it stops. Line
4 of that block is a `GamePPGlobalThread` operation and line 6 a function
defined in the workspace rather than a built-in. That is the next thread to
pull: what line 6 calls, and why the block never reaches line 7.

Also outstanding: the run now faults during *shutdown*, in `sub_0074E020`
reading through a low pointer with `free` as the last import. That is a
teardown-order problem, not a blocker, and it only appears because the game now
has a renderer to tear down.

### What is not the cause

Ruled out by measurement earlier, so as not to be re-guessed:

- **Not a null process manager.** `GamePPProdBase + 0x94` holds a live
  GamePPVisProcessManager. An earlier note here said otherwise; that came from
  a `--poison` address captured in a *different run*, and allocation order
  varies once the game's own threads are up.
- **Not thread starvation.** Coarse preemption of the machine lock was
  implemented and tried at every 500, 5,000 and 50,000 function entries. No
  change: the main thread is not waiting, it has 7.8 million calls of its own
  to make compiling 1,713 object templates out of 9,554 archive members.
- **Not a wall-clock timeout.** `--waitscale 100` changes nothing.
- **Not an error the game noticed.** The game ships with its assert and log
  machinery LIVE -- about 2,500 sites test a byte at 0x00833878 and report
  through a `std::fstream`. Startup clears it from a setting named
  `permitSyncChecking` that has to read `"on"`, so the reports were being
  skipped. With `--poke 0x00833878 1 0x0052C300` the log opens and flushes and
  the report formatter is never entered once.

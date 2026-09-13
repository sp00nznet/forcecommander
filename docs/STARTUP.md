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

The game boots, loads, starts its first section, runs that section for 61
frames, and then exits cleanly. Nothing is drawn, and nothing goes wrong: no
assertion fires and no error is reported anywhere in the run. The section
finishes.

### The chain, measured

`Run 2 1 6` is `GamePPProdStartup` slot 19, forwarding to `GamePPProdBase`
slot 61 (`sub_0051CE70`), which calls slot 14 on the object at
`GamePPProdBase + 0x94` -- the **GamePPVisProcessManager** (vtable
0x007C58B4). Slot 14 is `sub_0052B410`, "start a process".

The three numbers are an object-template id: `.pro` files key `id` and
`inheritID` as triples, and `2 1 6` is `Trasse - Day/info.pro`, whose
`bootThread` is 102 and whose `inheritID` is `2 1 12` = `Endor - Dawn` -- the
template every mission inherits from, and the one that actually carries the
code. So the last live line of Focom.ini means "boot the Trasse section", and
it works: the lookup finds the template, allocates, and registers a process in
slot 0 of the manager's 16-slot array.

The process is started with a **boot thread** (`sub_00523980` stores that
choice at `[proc+0x1B4]` and the Ronin `CThread` at `[proc+0x1B0]`), and a
Ronin thread is one-shot: `sub_005510F0` calls the body once, stores the
result, signals the completion event, and sets `[thr+0xC] = 0`. The process's
own per-frame check, `sub_005263D0`, is then:

```
if (![proc+0x1B4])  return proc->Update(...)        /* not a boot process */
if (![proc+0x1B0] || ![[proc+0x1B0]+0xC])           /* boot thread finished */
     manager->RemoveProcess(proc)
return 1
```

So **the process lives exactly as long as its boot thread.** That is the
design, not a bug.

The boot thread's body is `CProcessThread::Tick` (`sub_00524A80`), which is
`GetTickCount()`, a delta, and `process->Update(template, dt)` -- one frame.
It ran **61 times** and then stopped, because `sub_005261C0` returned 0:

```
alive = sub_00526A20(threads...) != 0  &&  [proc+0x258] == 0
```

`sub_00526A20` walks the section's script threads; when none of them has
anything left to run, it returns 0. The section's script had finished.

### The script trace

`tools/scriptmap.py` recovers the script-function registry from the lifted
code -- every subsystem is registered with an id, a name and a help string, so
156 is `If`, 384 is `String`, 821 is `Smush`, 900 is `Screen`, 936 is
`Protocol@GamePPMultiplayer`, 940 is `RE3D`. `GamePPVisLibraryManager::
GetLibrary` is `sub_0052C300`, a one-argument lookup into a 1024-entry table,
so

```
focom.exe game/Focom.exe --run --argtrace 0x0052C300
```

prints the subsystem each script line calls. Two threads run script: 3,440
dispatches on the boot thread and 1,734 on a second. The boot thread's script
uses `Message` (314), `Text Bounds` (281), `Mouse` (185), `String` (172),
`RE3D` (169), `Protocol` (120), `RE3DStage` (115), `Smush` (12), `Wait If`
(12), `Wait Forever` (6), `Wait` (3), `Stop` (3) -- a front end, driving a 2D
interface over 61 frames.

`Wait Forever` works: `GamePPGlobalSysWaitForever::Execute` (`sub_005D48F0`)
sets bit 2 of the context's flag word at `+0x30`, and the interpreter's
line-runner `sub_00512170` reads it back and returns 0 for "do not advance".
The idiom it uses is `and al,0x14 / neg al / sbb eax,eax / inc eax`, which
needs NEG's carry -- the lifter emits it (that fix predates this work), and
the lifted C is correct. A parked thread yields to the next script thread,
which is why lines keep running after it.

The script's last dispatch is subsystem **936, `Protocol@GamePPMultiplayer`**,
and the last shim call in the whole run is `IDirectPlayLobby::
GetConnectionSettings`, which answers `DPERR_NOTLOBBIED` -- the correct answer
for a launch that did not come from a lobby. Then the boot thread's body
returns, the manager reaps the process, and WinMain returns.

### What is not the cause

Ruled out by measurement, so as not to be re-guessed:

- **Not a null process manager.** `GamePPProdBase + 0x94` holds a live
  GamePPVisProcessManager. An earlier note here said otherwise; that came from
  a `--poison` address captured in a different run, and allocation order
  varies once the game's own threads are up.
- **Not thread starvation.** Coarse preemption of the machine lock (hand it
  over every N function entries) was implemented and tried at N = 500, 5,000
  and 50,000. No change: the main thread is not waiting, it has 7.8 million
  calls of its own to make compiling 1,713 object templates out of 9,554
  archive members while the worker runs the script.
- **Not a wall-clock timeout.** `--waitscale 100` changes nothing.
- **Not the frame tick.** `sub_0051D480`, the base's slot 62, is willing to
  keep ticking; the loop ends because the live-process count goes to zero.
- **Not an error the game noticed.** The game ships with its assert and log
  machinery LIVE: ~2,500 sites test a byte at 0x00833878 and report through a
  `std::fstream`. Startup clears that byte (it is set from a setting named
  `permitSyncChecking`, which has to read `"on"`), so the reports were being
  skipped; with the byte poked back on

  ```
  focom.exe game/Focom.exe --run --poke 0x00833878 1 0x0052C300
  ```

  the log file is opened and flushed three times and **nothing is ever
  queued** -- `sub_00403900`, the report formatter, is entered zero times.

### The open question

No Direct3D device is ever created. RE3D gets as far as enumerating drivers and
probing devices -- `CDD7Driver::CreateDevice` builds a 2,568-byte `CDD7Device`,
enumerates 8 display modes, registers three renderers, and the whole thing is
torn down again, which is what a probe pass looks like -- and then the screen
code is never entered at all. The `Screen` subsystem's script API is `Init`,
`Done`, `FullScreen(Width, Height, Bits Per Pixel)` and `Windowed(Width,
Height)`; `CEngine@RE3D` slots 6, 8 and 10 are never called, and
`CDD7WinScreen` / `CDD7FSScreen` never run.

So the front end runs its 61 frames with no renderer and finishes. The next
step is to find which `Screen` call the script makes and what it gets back:
the function index is in the line record, not in the `GetLibrary` argument, so
reading it needs one more hop than `--argtrace` currently gives.

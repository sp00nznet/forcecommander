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

The startup script now runs to its last line and the process exits cleanly,
having executed **8,944,137 lifted calls across 3,866 distinct functions** and
opened its own content: `project.gam`, the `Focom Translation` text files, and
**`forcecommand.rpk` with 288,585 reads** -- the 274 MB archive, in exactly the
directory format `tools/rpk.py` decodes.

It renders nothing, and the reason is specific. `Run 2 1 6` is
`GamePPProdStartup` vtable slot 19 (`sub_005026D0`), which does only this:

    ecx = [this + 0x14]          ; the GamePPProdBase
    if (!ecx) return
    call [ecx_vtbl + 0xF4](2, 1, 6)   ; GamePPProdBase slot 61, sub_0051CE70

and `sub_0051CE70` ends:

    ecx = [this + 0x94]
    if (!ecx) goto epilogue      ; <-- taken
    ...
    call [ecx_vtbl + 0x38](this, &state)   ; the main loop

`GamePPProdBase + 0x94` is the **GamePPVisObjectTemplateManager**, and it is
null by the time Run asks for it. The sequence, from a `--poison` on that slot
against the call trace:

| trace index | what |
|---|---|
| 20,097 | `sub_0052B910` (GamePPVisObjectTemplateManager) creates it; `+0x94 = 0x1014E140` |
| 115,999 | `GamePPProdBase` slot 186 (`sub_0051DE00`) releases it and nulls `+0x94` |
| 4,692,711 | `Run` finds it null and returns |
| 8,635,133 | `sub_00519C50`, the real teardown, runs at the end |

The nulling at 115,999 is not a bug: slot 186 is called deliberately from
`GamePPProdStartup` slot 12 (`sub_00501DB0`, at 0x00501ED9) after two virtual
checks pass, as a reset before loading a program -- the code that follows it
goes on to `sub_00520370` and `sub_0053DCB0`. So the question is not why it is
cleared but **what should re-create it on the load-program path, and why that
does not happen.** That is the next thread, and it is game logic rather than
shim fidelity.

Two things are known not to be the cause: `<Bad Template>` (0x0083EEA8, in
`sub_0051D530`) never executes, and the game never calls `GetMessage`,
`PeekMessage` or `DispatchMessage` at all -- so it is not losing a message
loop, it never starts one, because Run returns before reaching it.


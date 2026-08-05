# Palworld Integrated Storage — Native Linux Dedicated-Server Port

<!-- linux-dedicated-port-overview:start -->

> [!IMPORTANT]
> This fork targets the native Linux Palworld dedicated server through
> NullPrism RE-UE4SS. It does not support Palworld clients, single-player
> sessions, listen servers, or the original Windows DLL loading path.

## What this project is trying to do

The original Integrated Storage mod was built as a Windows C++ DLL and
combines client-facing behaviour with server-authoritative storage logic.

This fork is separating and porting the server-side functionality so it
can run natively inside a Linux Palworld dedicated server.

The long-term goal is to provide dedicated-server Integrated Storage
behaviour that can:

- Discover every base camp belonging to a guild.
- Discover the storage modules and eligible chests associated with those
  camps.
- Build a server-authoritative view of the guild's available storage.
- Keep that view reconciled as camps, chests and storage objects change.
- Allow supported storage operations to work across the guild's bases.
- Run without requiring a Windows PalServer installation or Wine.

The port is being developed in deliberately isolated stages. Discovery
and validation come first; registration, routing and inventory mutation
remain disabled until the underlying reflected data has been proven safe.

## This is a source port, not a DLL converter

The Windows `.dll` is not being wrapped, translated byte-for-byte, or
renamed for Linux.

Instead, the original MIT-licensed C++ source is being used as the
behavioural reference while the usable server-side portions are rebuilt
for Linux.

That process involves:

1. Identifying which original features are server-authoritative.
2. Excluding client HUD, crafting UI, client RPC and Windows injection
   behaviour.
3. Replacing Windows-only APIs, detours and binary scanning with
   NullPrism-compatible reflected Unreal access.
4. Compiling the Linux implementation as a native x86-64 ELF shared
   object.
5. Packaging the resulting file as `dlls/main.so` for NullPrism.
6. Testing it against an isolated clone of a populated dedicated-server
   world before production use.

The output is therefore a new native Linux build of the mod logic, not a
converted copy of the Windows DLL.

## How NullPrism is used

[NullPrism RE-UE4SS-Linux](https://github.com/NullPrism/RE-UE4SS-Linux)
provides the connection between the native Linux mod and the Unreal
Engine runtime inside PalServer.

The current attachment path is:

1. PalServer starts through the NullPrism Linux launcher.
2. `libUE4SS.so` is preloaded only into the PalServer process.
3. NullPrism initialises inside the dedicated server.
4. NullPrism discovers the enabled native C++ mod package.
5. It loads `ModIntegratedStorageCpp/dlls/main.so`.
6. It calls the exported `start_mod` lifecycle function.
7. The returned `CppUserModBase` implementation receives NullPrism
   lifecycle callbacks.
8. The mod uses NullPrism's reflected Unreal APIs to inspect live
   PalServer objects and properties.

The relevant server layout is:

- `Pal/Binaries/Linux/libUE4SS.so`
- `Pal/Binaries/Linux/run_ue4ss.sh`
- `Pal/Binaries/Linux/Mods/ModIntegratedStorageCpp/enabled.txt`
- `Pal/Binaries/Linux/Mods/ModIntegratedStorageCpp/dlls/main.so`

The native mod exports:

- `start_mod`
- `uninstall_mod`

Its current lifecycle path includes:

- `on_program_start`
- `on_cpp_mods_loaded`
- `on_unreal_init`
- `on_update`

No second UE4SS installation is used, and `LD_PRELOAD` is not configured
globally.

## How the current code works

The Linux implementation currently uses reflected Unreal access to:

- Locate a valid world context.
- Verify that the process is a dedicated server.
- Enumerate loaded `PalBaseCampModel` objects.
- Read each camp's `GroupIdBelongTo` guild identifier.
- Read and validate each camp's `ModuleArray`.
- Resolve the reflected `PalBaseCampModuleItemStorage` class.
- Match storage modules through reflected class-pointer ancestry.
- Group discovered camps and storage modules by their 16-byte guild ID.
- Enumerate loaded `PalMapObjectItemChestModel` objects.
- Schedule chest ownership queries through a native EngineTick callback
  so reflected `ProcessEvent` calls execute on the Unreal game thread.
- Resolve each chest's owning camp through
  `GetBaseCampModelBelongTo`.
- Associate each resolved chest with its camp and existing guild ID.
- Emit read-only camp, storage and chest-association diagnostics.

Because NullPrism can release its original native-mod loader handle, the
mod intentionally retains one additional `dlopen` reference for the
lifetime of PalServer. This keeps callback code mapped until process
exit, including while NullPrism invalidates and later collects a
registered callback.

The current implementation does not:

- Register storage modules or buildable chests.
- Add chests to another camp or guild.
- Redirect inventory requests.
- Provide shared inventory routing.
- Alter crafting behaviour.
- Move, consume or transfer items.
- Mutate any storage container.
- Install client hooks or UI changes.

## Linux ABI considerations

NullPrism and a separately compiled native mod can own different C++
runtime and allocator boundaries.

Two populated-world crashes were found during Stage 4a:

- A temporary class-name string crossed the shared-library boundary and
  was destroyed through the wrong runtime.
- A local `std::vector` passed to `FindAllOf` was populated by
  `libUE4SS.so` and later destroyed by `main.so`.

The accepted implementation avoids those paths by:

- Comparing reflected `UClass` and `UStruct` pointers instead of
  converting `FName` values into C++ strings.
- Retaining one process-lifetime `FindAllOf` output vector and reusing
  its capacity instead of destroying cross-boundary storage.
- Treating guild IDs as opaque 16-byte values rather than making
  platform-specific wide-string assumptions.

## Development status

### Stage 4a — accepted

Read-only populated-world discovery has been validated with:

- 20 base camps.
- 8 guilds.
- 20 base-camp storage modules.
- 25 repeated populated discovery cycles.
- A 180-second post-pass stability window.
- No Unreal or NullPrism crash records.
- An unchanged production server.

### Stage 4b — next

Stage 4b will add read-only discovery of buildable storage chests and
safe association of those chests with their camp and guild.

Stage 4b will not yet enable:

- Cross-camp registration.
- Shared inventory routing.
- Item transfer or consumption.
- Container mutation.

## Platform scope

| Environment | Status |
| --- | --- |
| Native Linux dedicated server | Target platform |
| NullPrism RE-UE4SS Linux | Required runtime bridge |
| Windows dedicated server | Use the upstream Windows project |
| Linux client | Not supported |
| Windows client | Not supported by this fork |
| Listen server | Not supported |
| Single-player | Not supported |
| Wine-based Windows server | Not a target |

## Upstream and licence

The original Integrated Storage project was created by Sarfflow and is
licensed under the MIT License.

This repository preserves that attribution while developing the native
Linux dedicated-server port.

Detailed Stage 4a architecture and validation notes are available in
`docs/LINUX_STAGE_4A.md`. Stage 4b.2 extends that read-only foundation
with validated game-thread chest-to-camp and chest-to-guild association.

<!-- linux-dedicated-port-overview:end -->

<!-- linux-nullprism-port-notice -->
> [!IMPORTANT]
> This fork is focused on adding native Linux dedicated-server
> support through NullPrism's RE-UE4SS-Linux.
>
> The original Integrated Storage project is maintained by
> [Sarfflow](https://github.com/Sarfflow/palworld-integrated-storage).
> Windows clients continue to use the upstream Windows client build.


**Author: Sarfflow** · Palworld 1.0 · a [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) C++ mod

Build and craft anywhere in your guild using materials stored in **any** of your guild's
base camps — not just the camp you're standing in. Vanilla Palworld only lets a camp use its
own local storage; this mod pools every same-guild camp's storage so the whole guild's
materials are available at every camp, including brand-new empty ones.

- Build & craft menus show your real pooled guild totals for each material.
- Recipes your guild genuinely lacks stay greyed/red, just like vanilla.
- The server stays authoritative — it consumes the real materials from wherever they are.
- Native `ItemStackInfo` is **never** mutated, so native Quick Stack and the Item Retrieval
  Device keep working normally.

Works in single-player (host), on a listen/host session, and on a dedicated server with remote
clients. A single DLL ships to every end and role-gates itself at runtime.

> This repository is the **source**. Most players just install the prebuilt Windows binary from
> wherever they got the mod. The source is here for people who want to read it, build it
> themselves, or **port it to another platform** (see [Porting](#porting-to-other-platforms-eg-macos-arm64)).

---

## Install (Windows, prebuilt)

The mod is a standard UE4SS C++ mod. Layout (see [`dist/`](dist/)):

```
<UE4SS Mods dir>/
└── ModIntegratedStorageCpp/
    ├── enabled.txt          (empty file — presence enables the mod)
    ├── config.txt           (optional; see below)
    └── dlls/
        └── main.dll         (the compiled mod — you build this, see below)
```

Install the **same** mod folder on both the server and every client. On a dedicated server the
mod does the authoritative cross-registration and consumption; on a remote client it handles the
display and the client-side craft/build gate.

### config.txt

All keys are optional; each falls back to a built-in default if absent. See
[`dist/ModIntegratedStorageCpp/config.txt`](dist/ModIntegratedStorageCpp/config.txt).

| key | default | meaning |
|---|---|---|
| `verbose` | `true` | detailed `[ISGATE]` diagnostics in `UE4SS.log`. Set `false` once things work. |
| `reconcile_interval_ms` | `8000` | (server/host) how often to re-scan guild chests and re-apply the merge. Min 500. |
| `isi_refresh_ms` | `1500` | reserved / kept for config compatibility. |

---

## How it works

Everything is located by **unique AOB signature at load**, so it survives address-shifting game
updates. Three roles, one binary:

- **Server (authority)** — every `reconcile_interval_ms` a *discovery reconcile* enumerates every
  guild chest from the map-object manager and every base camp (including empty ones), then
  cross-registers each guild chest's container into every same-guild camp's storage module. That
  lets the native build/craft flow **consume** cross-camp. On a host/SP authority the native
  material collector then reads the merged containers, so the display is correct for free.

- **Remote client** — can't see far-camp containers, so it **displays** the guild total by minting
  local item slots and appending them into a spare inventory container ("cont5") **only for the
  duration of the native material scan** (three AOB-located detours). The per-item pool comes over
  a custom transport channel (below), never over `ItemStackInfo`.

- **Transport channel** — demand-driven and event-driven: the client tracks its current camp via
  the `OnEnterBaseCamp`/`OnExitBaseCamp` hooks (no polling), fires a light trigger from a top-level
  tick, the server resolves that client's camp, reads ground-truth container contents for
  (guild − own), and replies over an engine RPC (`Debug_CheatCommand_ToServer` /
  `Debug_ReceiveCheatCommand_ToClient` carrying a small `key:count,…` string). No `FindAllOf` on
  any per-frame path.

The heavy lifting is a single file: [`src/dllmain.cpp`](src/dllmain.cpp). It is thoroughly
commented — start at the top-of-file architecture block and the `on_update` / `install` methods.

---

## Building (Windows)

This mod builds inside a UE4SS custom-mods tree; it is **not** standalone.

**Dependencies (all provided by the UE4SS source tree):**
- The UE4SS C++ mod SDK (`Mod/CppUserModBase.hpp`, `Unreal/*`, `DynamicOutput/*`).
- **PolyHook2** (`polyhook2/Detour/x64Detour.hpp`) — used for the trampoline detours. UE4SS already
  vendors it; the mod links it via `add_packages("polyhook_2")`.

**Steps:**
1. Clone/build [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) and set up its xmake build.
2. Drop this mod into the tree as a C++ mod target, e.g. copy `src/` to
   `cppmods/ModIntegratedStorageCpp/` (the target name in [`src/xmake.lua`](src/xmake.lua) is
   `ModIntegratedStorageCpp`) and register it the way RE-UE4SS registers its own cppmods.
3. `xmake build ModIntegratedStorageCpp`.
4. Copy the resulting DLL to `.../Mods/ModIntegratedStorageCpp/dlls/main.dll`.

The build target is Win64 / MSVC.

---

## Porting to other platforms (e.g. macOS arm64)

The macOS UE4SS port loads C++ mods from `ModName/dlls/main.dylib` and exports the same API
(`CppUserModBase`, `start_mod`, …), so the mod *structure* carries over. The retarget is not a
pure recompile, though — here's an honest map of what ports cleanly vs. what needs real work,
roughly in increasing difficulty:

1. **Pure UE4SS reflection (ports cleanly):** role detection (`IsServer`/`IsDedicatedServer`),
   the enter/exit-camp hooks, the transport request/reply RPCs, and the server-side discovery
   reconcile + container cross-registration. These go through the UE4SS reflection API and use no
   platform-specific tricks. **This is the whole server half and the transport, i.e. most of the
   value.**

2. **Compiler/OS shims (mechanical):**
   - `<Windows.h>` + `GetModuleFileNameW`/`MAX_PATH` (used only to find `config.txt` next to the
     DLL) → locate the module path via `dladdr`, or drop config parsing and hardcode defaults.
   - `GetTickCount64()` → `std::chrono::steady_clock`.
   - SEH (`__try/__except`, `__try/__finally`) guards the container reads and *guarantees* the
     borrowed container is restored even if the native scan faults. clang has no SEH — use an RAII
     scope-guard (restore in a destructor) for the normal + C++-exception paths; the hardware-fault
     case is a defensive extra you can live without on a healthy client.

3. **String width (subtle — read this):** the code reads UE strings/`FName`/`FGuid` bytes through
   `wchar_t*`. On Windows `wchar_t` is 16-bit, which matches UE's UTF-16 `TCHAR`. On macOS/Linux
   **`wchar_t` is 32-bit** and will mis-read every string. Use UE's `TCHAR` / `char16_t` / the
   `RC::CharType` typedef (and `FString`/`FName` APIs) instead of raw `wchar_t` for those reads.

4. **The display-injection half (needs re-RE):** the three material-scan detours and the struct
   offsets (`OFF_*`) were reverse-engineered from the **Windows x86-64** Palworld build. The AOB
   byte signatures are x86-64 opcodes and will **not** match an arm64 binary, and struct offsets
   may differ. If your macOS client is an arm64-native game process, this half has to be
   re-located on that binary. Note the client-side *gate* (whether the craft is even allowed) is
   tied to this display path — so without it, a remote client can request but the local menu may
   still refuse. The server-authoritative consume itself does not depend on it.

If you get the reflection + transport half compiling and want help with the RE half, open an issue
with your `UE4SS.log` (client and server) and what you're seeing.

---

## License

[MIT](LICENSE). Do what you like; attribution appreciated.

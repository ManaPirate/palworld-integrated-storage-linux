# Integrated Storage for Palworld — Linux / NullPrism Port

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

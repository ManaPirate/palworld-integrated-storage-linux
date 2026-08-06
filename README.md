# Palworld Integrated Storage — Native Linux Dedicated-Server Port

This repository is a native Linux dedicated-server port of
[Sarfflow's Integrated Storage](https://github.com/Sarfflow/palworld-integrated-storage).

Its purpose is to provide server-authoritative shared storage across
multiple camps owned by the same guild, without relying on the original
Windows-only client hooks, executable detours, or UI modifications.

## Target environment

- Palworld dedicated server running natively on Linux
- x86-64
- [NullPrism RE-UE4SS-Linux](https://github.com/NullPrism/RE-UE4SS-Linux)
- Native C++ user mod loaded as `main.so`

## Technical approach

The port uses Unreal reflection and NullPrism's native callback APIs to:

- Discover camps, guilds, camp storage modules, and chest models
- Resolve chest ownership on the Unreal game thread
- Build same-guild, foreign-camp registration relationships
- Validate reflected function parameters before invocation
- Perform server-authoritative registration through Palworld's own
  storage-module functions
- Keep worker-thread observation separate from game-thread Unreal calls

The implementation deliberately avoids Windows API dependencies,
PolyHook, executable AOB detours, client inventory hooks, HUD changes,
and the original combined client/server RPC transport.

## Scope

The Linux port targets dedicated servers only.

Listen servers, single-player sessions, client UI integration, and native
Windows builds are outside the current scope.

## Engineering record

The detailed chronological runsheet, accepted builds, runtime evidence,
known failures, safety boundaries, and next implementation stages are
maintained in [`docs/linux-port-status.md`](docs/linux-port-status.md).

## Attribution

Original project by Sarfflow.

Native Linux dedicated-server port by ManaPirate.

The upstream MIT licence and attribution are preserved.

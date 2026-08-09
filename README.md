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

Linux port engineering documentation is split into three purpose-specific records:

- [`docs/linux-port-status.md`](docs/linux-port-status.md) — current accepted checkpoint, safety boundaries, unresolved work, and immediate next stage.
- [`docs/linux-port-history.md`](docs/linux-port-history.md) — detailed chronological record of stages, experiments, failures, accepted results, and recovery actions.
- [`docs/linux-port-evidence-index.md`](docs/linux-port-evidence-index.md) — compact index of major stage classifications, commits, artifact identities, and evidence archives.

## Attribution

Original Integrated Storage project by [Sarfflow](https://github.com/Sarfflow/palworld-integrated-storage).

This port relies on [NullPrism's RE-UE4SS-Linux](https://github.com/NullPrism/RE-UE4SS-Linux) for the native Linux RE-UE4SS runtime used to load and run the mod on the dedicated server.

Native Linux dedicated-server port by ManaPirate.

The upstream MIT licence and attribution are preserved.

# Palworld Integrated Storage: Native Linux Dedicated-Server Port

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

## Installing

See [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) for install steps, how it
behaves for players, and known quirks. Short version: this is a
**server-only** release. Install `main.so` on the dedicated server and
nothing on the client. Don't install any client-side Integrated Storage
mod, it's unnecessary and the unpatched Steam Workshop/NexusMods build can
still crash clients on a large guild pool.

## Reporting a problem

The mod writes a self-contained diagnostic snapshot to
`diagnostic_report.txt` right next to `main.so` (i.e.
`Mods/ModIntegratedStorageCpp/diagnostic_report.txt`), refreshed
automatically every ~2 minutes for as long as the server runs — no
setup or opt-in needed. It covers role/EngineTick health, a live
guild/camp/storage/chest summary, and an egg-incubation section. If
you hit a problem, **please attach this whole file** to a
[GitHub issue](../../issues) or your bug report — it saves a lot of
back-and-forth compared to hunting through `UE4SS.log`.

## Engineering record

Linux port engineering documentation lives in two files:

- [`docs/linux-port-status.md`](docs/linux-port-status.md): current accepted checkpoint, safety boundaries, unresolved work, and a chronological stage-by-stage log.
- [`docs/RELEASE_TEST_PLAN.md`](docs/RELEASE_TEST_PLAN.md): the full release validation checklist and results.

## Attribution

Original Integrated Storage project by [Sarfflow](https://github.com/Sarfflow/palworld-integrated-storage).

This port relies on [NullPrism's RE-UE4SS-Linux](https://github.com/NullPrism/RE-UE4SS-Linux) for the native Linux RE-UE4SS runtime used to load and run the mod on the dedicated server.

Native Linux dedicated-server port by ManaPirate.

The upstream MIT licence and attribution are preserved.

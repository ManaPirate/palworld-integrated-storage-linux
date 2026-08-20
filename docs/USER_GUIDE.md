# Integrated Storage (Linux Dedicated Server): User Guide

This is the install and use-case guide for the native Linux dedicated-server
port of [Sarfflow's Integrated Storage](https://github.com/Sarfflow/palworld-integrated-storage).
It gives every guild member shared, server-authoritative access to
materials sitting in chests at *any* of their guild's base camps, not just
the one they're standing in.

## Read this first: do not install a client-side mod

This release is **server-only**. Install `main.so` on the server and
nothing else. Do **not** install any client-side "Integrated Storage"
mod, including the existing Steam Workshop / NexusMods build.

Two reasons for that:

1. **It's unnecessary.** The server-side mod (`main.so`) makes each
   foreign, same-guild chest a native part of every camp's local storage,
   at the engine level. A completely vanilla client, no mods at all, sees
   correct combined material totals in the build/craft menu and can
   consume from them, because as far as the game's own code is concerned,
   those chests really are local. There's nothing for a client mod to
   add.
2. **The unpatched client mod can still crash you.** The existing
   Steam Workshop / NexusMods build has a known heap-corruption crash
   (`FMallocBinned2 Attempt to realloc an unrecognized block`) that can
   trigger during normal play with a large guild pool. This server release
   doesn't need that mod at all, so the safest move is not to run it.

If you already have it installed, remove it before connecting.

## Requirements

- A Palworld dedicated server running natively on Linux (x86-64)
- [NullPrism RE-UE4SS-Linux](https://github.com/NullPrism/RE-UE4SS-Linux)
  already installed and working on that server
- The `ModIntegratedStorageCpp.zip` drop-in package from this repository's
  release

## Installing

1. **Stop the server.** Copying mod files into a live mount while the
   server process still has the old ones mapped can produce a spurious
   crash on the next start, so always stop first, copy, then start back
   up.
2. Extract `ModIntegratedStorageCpp.zip` directly into your server's
   `Mods` folder:
   ```
   <server root>/Pal/Binaries/Linux/Mods/
   ```
   This drops in the whole `ModIntegratedStorageCpp/` folder already laid
   out correctly: `dlls/main.so`, the `enabled.txt` marker RE-UE4SS-Linux
   needs to load it, and a `BUILD-PROVENANCE.txt` recording exactly which
   commit/build this is. Nothing to assemble by hand.
3. **Start the server.**
4. Confirm a clean startup by tailing the server's logs and checking for
   these lines, all from `[ModIntegratedStorageCpp]`:
   ```
   MODULE_PIN result=PASS
   ENGINE_TICK registered=1
   TRANSPORT_HOOK registered=1
   ```
   followed by, repeating roughly every 8 seconds as the server
   continuously scans camps and guilds:
   ```
   CHEST_ASSOC RESULT=PASS
   FULL_PLAN_REGISTER SUMMARY ... blocked=0 exceptions=0
   ```
   If you see `blocked` or `exceptions` above 0, or any
   `FMallocBinned2` / `LowLevelFatalError` / `Signal 11` line, something's
   wrong. Stop and check the log lines around it before continuing.

That's the entire install. There's nothing to configure, and nothing for
players to install or opt into.

## How it works for players

Nothing changes about how you play. Build and craft as normal:

- Open the build/craft menu at any of your guild's camps. The material
  checklist already reflects everything your guild owns across **every**
  camp, not just the one you're standing in.
- Place the building. Materials are drawn from wherever they actually
  sit, automatically, with no extra step, even if that's a chest at a
  completely different base.
- Existing systems (Quick Stack, the native Guild Chest view, the Item
  Retrieval Device) all continue to work exactly as they do without this
  mod.

Guild-to-guild isolation is strict: a different guild's materials are
never visible or usable at your camps, no matter how physically close
their base is to yours.

## Known behaviour (not bugs)

Two real, confirmed quirks worth knowing about up front so you don't
mistake them for something broken:

- **Rejoin-while-inside-camp refresh.** If you reconnect to the server
  while you're already standing inside a base camp's boundary, the build
  menu can briefly show stale (incomplete) guild materials, even though
  nothing is actually wrong. Re-opening the build menu does **not** fix
  it. Walking out of the camp and back in does. This is native client
  behaviour, storage recognition only re-evaluates on the camp entry/exit
  trigger, and it's not something this mod causes or can silently work
  around.
- **Rare stuck-camp state.** On one occasion, a camp briefly lost
  apparent access to remote guild materials after a successful cross-camp
  build. Leaving and returning to *that same* camp didn't fix it, but
  visiting a *different* camp and then coming back did. This hasn't been
  reproduced since despite deliberate attempts, and server-side logs
  stayed completely healthy throughout the one time it happened, so it
  looks like a rare native client-side hiccup rather than a server bug.
  If it ever happens to you, fast-travel (or otherwise visit) a different
  base and return to the affected one.

## Uninstalling

Stop the server, delete the `Mods/ModIntegratedStorageCpp` folder, and
start the server back up. No other cleanup is needed. The mod makes no
persistent changes to save data; every foreign-chest registration it
performs is re-derived fresh on every discovery pass while it's running.

## Troubleshooting

- **Where are the logs?** Everything this mod logs is prefixed
  `[ModIntegratedStorageCpp]` in the server's normal console/log output
  (`docker logs <container>` if running under Docker).
- **A guild's materials aren't showing up at a brand-new camp.** Give it
  one discovery pass (~8 seconds) after the camp is fully placed. The
  server picks up new camps automatically, no restart needed.
- **Something looks wrong that isn't covered above.** Check for
  `blocked=`, `exceptions=`, or `unassociated=` counts above `0` in the
  `FULL_PLAN_REGISTER SUMMARY` / `CHEST_ASSOC` log lines. A healthy
  server always shows `0` for all of these outside a single-pass
  transient blip right when a chest is placed or removed, which
  self-heals on the very next pass.
- **Running Palworld v1.0.3 and the server won't start, or crashes within
  a few seconds of startup?** This is a known NullPrism-side issue on
  v1.0.3 (recompiled `UGameEngine::Tick`), not specific to this mod. Try
  setting `EngineTickResolveMethod = VTable` under `[Hooks]` in
  `UE4SS-settings.ini`. This isn't guaranteed to fix it for every server;
  see `docs/linux-port-status.md` §9 for what's actually confirmed.
- **Running Palworld v1.0.3, logs look clean (`FULL_PLAN_REGISTER
  SUMMARY` completing with `blocked=0 exceptions=0`), but guild-wide
  materials never show up at non-main camps?** As of v1.0.1, controlled
  testing has confirmed the core mechanism (registration, replication,
  and the build UI) works correctly end-to-end, including on a client
  that stays continuously connected rather than just walking between
  camps. If you're still seeing this, it's likely specific to your
  server's scale or setup rather than a universal v1.0.3 problem —
  please attach your `diagnostic_report.txt` (see below) to a
  [GitHub issue](https://github.com/ManaPirate/palworld-integrated-storage-linux/issues)
  so it can actually be narrowed down. See `docs/linux-port-status.md`
  §9 for the full investigation history.

## Reporting a problem

The mod writes a self-contained diagnostic snapshot to
`diagnostic_report.txt`, right next to `main.so`
(`Mods/ModIntegratedStorageCpp/diagnostic_report.txt`), refreshed
automatically every ~2 minutes for as long as the server runs — no
setup needed. It covers role/`EngineTick` health, a live
guild/camp/storage/chest summary, and an egg-incubation section. If
you hit any problem, **please attach this whole file** to a
[GitHub issue](https://github.com/ManaPirate/palworld-integrated-storage-linux/issues)
— it says a lot more than a description alone, and saves a lot of
back-and-forth.

## Attribution

Original Integrated Storage project by
[Sarfflow](https://github.com/Sarfflow/palworld-integrated-storage). This
port relies on [NullPrism's RE-UE4SS-Linux](https://github.com/NullPrism/RE-UE4SS-Linux)
for the native Linux RE-UE4SS runtime used to load and run the mod.
Native Linux dedicated-server port by ManaPirate.

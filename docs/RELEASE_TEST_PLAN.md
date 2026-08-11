# Release Test Plan

Working checklist for validating the release before merging to `main` and
cutting it. Run this on the isolated test server (`Palworld-NullPrism-Test`),
never production.

**Release scope decision (2026-08-11):** confirmed the patched client DLL
is redundant for the core feature — cross-camp display *and* consumption
both work correctly with zero client-side mod, since Stage 4F's
`OnAvailableConcreteModel_ServerInternal` registration makes the foreign
chest genuinely, natively part of the local camp's storage server-side.
The release is **server-only**: ship `main.so` + an install/use-case guide,
and explicitly recommend against installing any client-side Integrated
Storage mod (including the existing unpatched Steam Workshop version,
which can still hit the Stage 4E.4 crash even though it's now
unnecessary). Sections below marked "client mod" are kept for completeness
and for the separate, still-worthwhile upstream contribution back to
Sarfflow's original project — they are **not** release-blocking for this
project's own release.

Check items off as they pass. If something fails, stop and record the exact
log lines / repro steps before continuing — don't paper over a failure by
retrying until it passes.

## 0. Build provenance

- [x] `main.so` built from `linux/nullprism-dedicated-server` HEAD
      (`fca0e72`), staged SHA256 recorded.
- [x] `main.dll` built from the `ManaPirate/RE-UE4SS` fork — confirmed
      byte-identical (`sha256sum` match) to this repo's `src/dllmain.cpp`
      before building. Kept for the upstream Sarfflow contribution, not
      for this project's own release artifact.

## 1. Server-only health checks (no client needed)

Start the test server with the new `main.so` and watch startup logs alone,
before any client connects.

- [ ] `MODULE_PIN result=PASS`
- [ ] `ENGINE_TICK registered=1`
- [ ] `TRANSPORT_HOOK registered=1`
- [ ] `CHEST_ASSOC RESULT=PASS` repeating every discovery pass (~8s), stable
      `objects=`/`valid=`/`associated=` counts (no `unassociated`,
      `missing_function`, `invalid_camp`, etc.)
- [ ] `FULL_PLAN_REGISTER SUMMARY ... blocked=0 exceptions=0` repeating every
      discovery pass, `attempted == completed == planned`
- [ ] No `FMallocBinned2` / `LowLevelFatalError` / `Signal 11` in the first
      10 minutes of idle uptime

## 2. Vanilla (unmodified) client compatibility AND capability

Connect with a Steam Workshop / NexusMods client, or no client mod at all.
This section covers two distinct claims — don't conflate them:

**2a. Non-disruption** (does the mod ever get in a vanilla client's way):

- [ ] Client connects and plays normally — join, move, build something
      mundane, disconnect
- [ ] `TRANSPORT_REQUEST` count stays at 0 for the whole session (the hook
      is inert without the exact `ISREQ|` sentinel)
- [ ] Zero crashes, zero behavior change versus a completely unmodded server

**2b. Cross-camp building actually works with zero client mod** (Stage 4F is
purely server-authoritative — `OnAvailableConcreteModel_ServerInternal`
registers the foreign chest as a genuinely native container of the local
camp's storage module, so there is nothing for a client to opt into):

- [x] **Confirmed 2026-08-11**: with the client mod fully disabled, placed a
      building at a camp using materials held only at a different
      ("offsite") same-guild camp. Placement succeeded — this is the core
      feature working with a completely vanilla client.
- [x] **Confirmed 2026-08-11**: with the client mod disabled, the build
      menu's material checklist *also* showed the correct combined total
      up front — no false "Insufficient materials," no need to override by
      attempting anyway. Both display and consumption are fully native,
      fully server-authoritative. The client mod's `injectMinted` display
      feature is redundant for this case.
- [ ] Repeat-test under more conditions before treating this as fully
      general: different recipes (more than 4 materials worth spread
      across camps, edge-case item types), larger guild sizes, a camp with
      partial local + partial foreign stock (not just fully-foreign), and
      at least one more player/session to rule out a one-off.
- [x] **Release-shape decision needed**: this changes what "release" even
      means — see conversation with the user, decision pending on whether
      to keep shipping the patched client DLL at all, and if so, what its
      remaining value proposition is (if any).

## 3. Patched client + display (Stage 4E transport) — NOT release-blocking

**Not required for this project's release** (§0 scope decision) — the
release recommends against installing any client mod at all. Kept here
only for the separate upstream contribution back to Sarfflow's original
project, where this client-side fix still matters (that project's users
run it in listen-server/host setups, not this Linux dedicated-server
port). Skip this section entirely for release sign-off; revisit only if
pursuing the upstream PR.

Connect with the freshly-built patched client (Stage 4E.4 fix).

- [ ] `[ISGATE] === IntegratedStorage 3.2 loaded ===` in the client's
      `UE4SS.log`
- [ ] Enter a base camp → `[ISGATE] CH enter-camp -> flagged` →
      `[ISGATE] CH request sent` on the client, `TRANSPORT_REQUEST
      RESULT=SENT items=N len=N` on the server, no crash
- [ ] Open the build/craft menu → guild-wide material totals from foreign
      same-guild camps appear in the checklist
- [ ] **Crash-repro stress test** (the exact Stage 4E.4 scenario): enter a
      camp with a large guild pool (200+ items across camps), open the build
      menu, close it, reopen it — repeat rapidly 7-10 times in quick
      succession. Watch for `FMallocBinned2 Attempt to realloc an
      unrecognized block` / `LowLevelFatalError`. There should be **none** —
      this is the specific crash the Stage 4E.4 fix targets, so this is the
      test that actually validates it, not just "did it build."
- [ ] Disconnect and reconnect once mid-session — pool display still works
      afterward, no crash
- [ ] Leave a camp (`OnExitBaseCamp`) and re-enter — pool refreshes
      correctly, no stale data

## 4. Cross-camp consumption (Stage 4F)

This is the actual reported bug and its fix — test it precisely, not just
"did a build succeed somewhere."

- [ ] **Primary repro**: at a non-main camp whose own real storage lacks a
      recipe's materials (verify this first — check the camp's own chests
      directly), attempt a build that the guild pool can cover. It should
      succeed.
- [ ] **Consumption location check** (important — this is different from
      the reverted Stage 4F.1 design): the executor *registers* a foreign
      chest into the local camp's storage module, it does not move items.
      After a successful cross-camp build, confirm the consumed materials
      decreased at the **foreign source camp's own chest** — not that they
      appeared and then vanished from the local camp's chest, and not that
      they were duplicated (guild total after the build should be exactly
      `total_before - recipe_cost`, check by tallying both camps' real
      chests before and after).
- [ ] **New camp mid-session**: with the server already running, found (or
      have another player found) a brand-new same-guild base camp. Confirm
      it shows up as `+1` in the next `FULL_PLAN_REGISTER SUMMARY
      planned=N` (should increase without a server restart) within one
      discovery pass (~8s), then confirm building at that new camp using
      guild materials works.
- [ ] **Empty-camp target**: a camp with zero of its own chests placed yet
      should still be a valid cross-registration target — confirm a build
      there using only guild-pooled materials succeeds.
- [ ] **Guild isolation**: with two separate guilds each holding camps on
      the server, confirm guild A's materials are never visible or usable
      at guild B's camps. Check `CHEST_GUILD id=... chests=N` lines don't
      cross guild keys, and attempt (and expect to fail, correctly) a build
      at guild B using a material only guild A's guild possesses.
- [ ] Sustained `FULL_PLAN_REGISTER SUMMARY` sanity across the whole
      session: `blocked` and `exceptions` stay at 0 throughout, not just at
      the first pass.

## 5. Stability / soak

- [ ] Extended session — 2+ continuous hours with the mod active and at
      least one player connected on and off throughout
- [ ] Multiple concurrent players from different guilds active
      simultaneously
- [ ] Full server restart mid-test (stop/start the container) — clean
      re-registration from scratch afterward (`FULL_PLAN_REGISTER
      SUMMARY planned=N` reappears with the same or higher `N`), no crash
      on restart
- [ ] Memory (`VmRSS`) growth over the soak window stays bounded/roughly
      linear, consistent with the already-accepted Stage 4D.9f leak-and-
      cache growth curve — not a new unbounded leak introduced by Stage 4F

## 6. Non-interference / regression

Confirm the mod doesn't break existing native systems it sits next to.

- [ ] Quick Stack (native chest-to-inventory quick-stack) still works
      normally on guild chests
- [ ] Item Retrieval Device (if applicable to your game version) still
      works normally
- [ ] The native Guild Chest UI (non-mod, built-in guild storage view)
      still displays and behaves correctly
- [ ] Normal solo (non-cross-camp) building and crafting is completely
      unaffected — a camp with its own sufficient local materials builds
      exactly as it would with no mod installed

## 7. Edge cases

- [ ] Solo player, no guild — mod should be a no-op for them, no crashes,
      no phantom pool data
- [ ] A camp is dismantled entirely mid-session — subsequent discovery
      passes handle its removal cleanly (no dangling pointer crashes, no
      stale `CHEST_GUILD` entries)
- [ ] A player leaves their guild mid-session — no crash, camp
      re-associates correctly on the next pass
- [ ] Two different guilds with camps in close physical proximity — confirm
      no spatial/proximity-based leakage (cross-registration here is
      guild-key-based, not distance-based, so this should be a clean pass,
      but worth confirming explicitly since it's the kind of assumption
      that's easy to get subtly wrong)

## 8. Sign-off

- [ ] Sections 0-2 and 4-7 (the release-blocking ones) checked off with no
      unresolved failures — Section 3 is explicitly out of scope per §0
- [ ] Any deviations/failures found during testing are either fixed and
      re-verified, or explicitly documented as a known limitation in
      `docs/linux-port-status.md` before release
- [ ] `docs/linux-port-status.md` §9 stage log updated with the final
      soak-test evidence (mirroring how every other accepted stage in this
      project is recorded)
- [ ] Release notes explicitly state: no client-side mod needed or
      recommended; if you already have the Steam Workshop/NexusMods
      Integrated Storage client mod installed, remove it (it's not just
      unnecessary now, the unpatched version can still crash you)

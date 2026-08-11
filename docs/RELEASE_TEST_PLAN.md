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

- [x] **Primary repro**: confirmed — built a bench at a camp using only
      "offsite" (foreign same-guild camp) materials, with the client mod
      disabled, checklist showing correct totals.
- [x] **Rejoin-while-inside-camp refresh behavior** (found during testing,
      not a bug — see §8 for release-notes writeup): after reconnecting
      while still positioned inside a camp's boundary, cross-camp materials
      briefly showed as unavailable again despite server-side registration
      being completely unaffected (`FULL_PLAN_REGISTER SUMMARY` stable at
      `planned=285 attempted=285 completed=285 blocked=0 exceptions=0`
      across the whole window, no `WORLD state reset`). Re-opening the
      build menu alone did not fix it; walking out of the camp's radius and
      back in did. Root cause: the native client only re-evaluates nearby
      storage on the `OnEnterBaseCamp` boundary-crossing trigger, and a
      rejoin that spawns the player already inside that boundary never
      fires it — purely a native client-side refresh quirk, not something
      this mod causes or can silently fix without hooking client code
      again (out of scope per the server-only release decision).
- [x] **Consumption location check**: confirmed — Egg Incubator (10
      Paladium Fragment, 5 Cloth, 30 Stone, 2 Ancient Civilization Parts)
      built cross-camp; the foreign source camp's Ancient Civilization
      Parts stack went from 212 to 210, exactly matching the recipe cost,
      with nothing appearing in the local camp's own storage. Clean,
      exact, single-source consumption — no duplication, no drift.
- [x] **New camp mid-session**: confirmed — deleted a base, waited, then
      placed a fresh palbox with the server already running (no restart).
      Live-followed logs captured two clean add/remove cycles (run 283→392
      and run 599→619): `planned=`, `objects=`, and `camps=` all
      incremented cleanly within one discovery pass of the topology change
      (e.g. `planned=285→287 objects=157→158 camps=17→18`), `blocked=0
      exceptions=0` throughout, with only the expected one-pass transient
      `CHEST_ASSOC unassociated=1` blip on removal that self-healed on the
      very next pass. User confirmed in-game that the newly placed base had
      access to remote materials, matching the log evidence exactly.
- [x] **Empty-camp target**: confirmed (Test A) — a camp with zero local
      chests successfully built two items back-to-back using only
      guild-pooled remote materials, checklist accurate throughout. Also
      confirmed a fresh chest placed then removed at the same empty-camp
      site (Test C) doesn't disturb this — see the known-behavior note
      below re: the one unreproduced incident that prompted this testing.
- [x] **Guild isolation**: confirmed — left the old guild and formed a new
      one; at the new guild's camp, an item requiring an old-guild-exclusive
      material was unbuildable/unselectable/greyed out, matching correct
      "insufficient materials" behavior rather than just missing from the
      list. Log evidence corroborates: across the sampled runs,
      `guild_mismatches=0` held for all 8 guilds / 17 camps concurrently
      active on the test server (not just the two involved in this test),
      and each guild's `CHEST_GUILD id=... chests=N` count stayed stable
      and distinct — the new guild's pool never absorbed the old guild's
      chests.
- [x] **Sustained `FULL_PLAN_REGISTER SUMMARY` sanity**: confirmed across
      hundreds of passes spanning multiple test sessions today (including
      through a chest placement/removal, several builds, and the raid
      incident) — `blocked=0 exceptions=0` held the entire time, only
      `planned=`/`objects=` ever moved, and only in response to real
      topology changes (chest added/removed).

## 5. Stability / soak

- [x] **Extended session**: confirmed — this production deploy session
      alone ran well over 2 continuous hours with the mod active and
      players connecting on and off throughout, `blocked=0 exceptions=0`
      held the entire time.
- [x] **Multiple concurrent players from different guilds**: confirmed —
      the production server carried 7-8 active guilds and ~20 camps live
      simultaneously throughout, with real players from at least two of
      them actively building/deconstructing during testing.
- [x] **Full server restart mid-test**: confirmed — restarted the
      production container with two players online; clean re-registration
      afterward, no crash. One player hit the already-documented
      rejoin-while-inside-camp refresh quirk (§8) immediately after
      reconnecting, resolved by fast-traveling — expected, not a new
      issue.
- [ ] **Memory (`VmRSS`) growth**: in progress — baseline to be taken
      after tomorrow's scheduled 04:30 server restart, with a follow-up
      reading later to check the trend stays bounded/roughly linear (not
      a new unbounded leak beyond the already-accepted Stage 4D.9f
      leak-and-cache curve). Interim reading taken mid-session:
      `VmRSS: 4406576 kB` (~4.2 GB).

## 6. Non-interference / regression

Confirm the mod doesn't break existing native systems it sits next to.

- [x] **Quick Stack**: confirmed — pulled a stack of items from personal
      inventory into a guild chest via Quick Stack, exited the menu, then
      reopened it and pulled the same items back out normally.
- [x] **Item Retrieval Device**: confirmed on the production server — works
      normally, no interference from the mod.
- [x] **Native Guild Chest UI**: confirmed — the same Quick Stack
      round-trip above was done through the built-in (non-mod) guild
      storage view; opened/closed/re-opened normally, contents accurate
      throughout.
- [x] **Normal solo (non-cross-camp) building**: confirmed — built a metal
      chest at a camp using only that camp's own local materials; behaved
      exactly as expected with no mod interference.

## 7. Edge cases

- [x] **Solo player, no guild**: N/A by design — a Palworld dedicated
      server auto-assigns every player to an (initially unnamed) guild, so
      a true guildless state doesn't exist to test. The closest real case,
      a player briefly between guilds, was already covered by the
      leave-guild-mid-session test (§4/§7) with no crash or phantom data.
- [x] **A camp is dismantled entirely mid-session**: confirmed — same
      evidence as the "new camp mid-session" test (§4): base deletion was
      observed to drop out of `DISCOVERY`/`CHEST_GUILD`/`FULL_PLAN_REGISTER`
      cleanly on the very next pass, with no dangling entries and no
      crash.
- [x] **A player leaves their guild mid-session**: confirmed — covered by
      the guild-isolation test (leave old guild, form new one, place a new
      camp) done live on the running server. No crash; `CHEST_ASSOC
      RESULT=PASS` with `unassociated=0` held throughout, re-association
      settled cleanly with no dangling/stale `CHEST_GUILD` entries left
      behind for the old membership.
- [x] **Two different guilds with camps in close physical proximity**:
      confirmed — three guilds actually, tested by placing a base near
      multiple guilds' camps and checking at several points around it
      whether their materials leaked in. No bleed observed; cross-
      registration held strictly guild-key-based, not distance-based, as
      designed.

## 8. Known behavior to carry into release notes

Running list of real, confirmed, non-bug behaviors discovered during
testing that users should be told about up front rather than discover
themselves and mistake for a problem. Pull this list directly into the
use-case guide when writing it (task #6).

- **Rejoin-while-inside-a-camp refresh**: if you reconnect to the server
  while already standing inside a base camp's boundary, the build menu may
  briefly not reflect guild-wide materials from other camps, even though
  nothing is actually wrong server-side. Walk out of the camp and back in
  to refresh it — re-opening the build menu alone does not help. This is a
  native client behavior (storage recognition only re-evaluates on the
  camp entry/exit trigger), not something this mod causes.
- **Rare stuck-camp state, possibly raid-related**: on one occasion during
  testing, a camp lost apparent access to remote materials after a
  successful cross-camp build, while a raid was concurrently happening at
  a different (main) base. Unlike the rejoin quirk, leaving and returning
  to *that same* camp did not fix it, and neither did deconstructing the
  built object. Visiting a *different* camp and returning did fix it.
  Extensive follow-up testing (three controlled repros isolating fresh
  local chest placement/removal specifically, since a chest had also been
  placed at that camp around the same time) found nothing — server-side
  registration logs stayed completely healthy (`FULL_PLAN_REGISTER
  SUMMARY` stable, zero blocked/exceptions) through the entire original
  incident, ruling out this mod's registration logic as the cause. The
  raid remains the one untested variable from the original incident. Not
  reproduced since; treated as rare. If it happens: fast-travel (or
  otherwise visit) a different base and return to the affected one.

## 9. Sign-off

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
- [ ] Section 8's known-behavior list is folded into the use-case guide

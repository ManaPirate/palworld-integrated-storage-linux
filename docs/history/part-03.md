### Candidate identity

```text
Version:
0.1.0-linux-stage4c.2-would-register

Source SHA256:
12ee389499b6c5a5e944b7c4f34f70b378b775d7527df080ebc1e80cce5b8865

Artifact SHA256:
0d494b86751d317f102812622ecc5ff48b796d8f2f74cdeedaa2e22d41d2b1a3

Build ID:
427acb4e36a79956ce3c96f97473b507f3a697e2
```

Source safety inventory:

```text
ProcessEvent source calls:
2

Registration function calls:
0
```

The two valid `ProcessEvent` calls were:

1. PalUtility role query
2. Chest ownership query

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c2-plan-20260806-083809
```

Results:

```text
ROLE THREAD=GAME:       1
ROLE RESULT=PASS:       1
Planner passes:         25
Planner incomplete:     0
Planner variants:       1
Per-guild lines:        200
Per-guild runs:         25
Unique guild plans:     8
Metadata passes:        25
Metadata incomplete:    0
Chest passes:           25
Chest incomplete:       0
Invalid thread:         0
Exceptions:             0
Crashes:                0
```

Stable global plan:

```text
Guilds:                 8
Guilds with pairs:      7
Associated chests:      157
Storage modules:        20
Foreign-camp pairs:     285
Own-camp combinations:  157
Duplicate chests:       0
Duplicate storages:     0
Duplicate pairs:        0
Camp conflicts:         0
Guild conflicts:        0
Null camps:             0
Invalid camps:          0
Missing guilds:         0
Zero guilds:            0
Camps without storage:  0
```

One guild has only one camp and therefore no foreign target.

Per-guild plan:

| Guild | Chests | Storages | Foreign pairs | Own-camp excluded |
|---|---:|---:|---:|---:|
| `20f979c33446e7f1f8cea19499aad71a` | 22 | 3 | 44 | 22 |
| `4fda64b78a4ae58954126eb13ec06dd3` | 3 | 1 | 0 | 3 |
| `5c21c345d94ea28f2dd2fb842cb20be4` | 32 | 3 | 64 | 32 |
| `64ad3b316644502f780ceebd2a31ff99` | 22 | 2 | 22 | 22 |
| `966b6b8eca48b42eaa08b3a92e673d00` | 15 | 3 | 30 | 15 |
| `9af4ac3e4a49def1993afeaced626523` | 31 | 4 | 93 | 31 |
| `a21c73d1fd4d4539161573b06df671f8` | 10 | 2 | 10 | 10 |
| `df4d6e7ea84f3b7db90b5ab07bc41b3e` | 22 | 2 | 22 | 22 |

The fingerprints are process-local because they include Unreal object
addresses.

Accepted commit:

```text
153a3c35f0d14f4dbd679659daa6a246e18aa165
feat(linux): add deterministic registration planner
```

---

## Stage 4c.3 — controlled single registration

### Goal

Make the smallest possible real registration mutation:

```text
one chest -> one foreign same-guild storage
```

### Safety design

The Stage 4c.3 candidate adds:

- A default-disabled normal package
- Adjacent arm file:
  `dlls/main.so.stage4c3-arm`
- A process-lifetime one-shot guard
- Planner-completion validation
- Dedicated-server validation
- Unreal game-thread validation
- Same-guild validation
- Different-camp validation
- Storage-class validation
- Reflected registration metadata validation
- Zeroed parameter buffer
- `memcpy` of chest pointer at reflected offset
- One registration `ProcessEvent` call site
- No full loop
- No reconciliation
- No routing test
- No production deployment

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.3-single-registration

Source SHA256:
3ef9fd7d9c0452ed750fb65fd97811a225b55bea0ca76164b57d65bbb4cfd5f6

Artifact SHA256:
f86c7a27b7b5273a572a835448a035da856bc5c48b5763ef5bc90856da073e32

Build ID:
c10b62417231f2dea11e9256d43d9704c769ae6f
```

Build exports:

```text
start_mod:
0000000000003970

uninstall_mod:
0000000000003ba0
```

Source inventory:

```text
ProcessEvent source calls:
3

Controlled registration ProcessEvent calls:
1
```

### Static build acceptance

Warnings were limited to the previously observed SDK warnings:

- Two `Atomic.hpp` switch warnings
- Two deprecated enum-conversion warnings
- One undefined-inline warning in `UnrealType.hpp`

The build completed and linked.

Verified:

- Stage 4c.3 version string
- Registration function string
- Armed gate marker
- Disabled gate marker
- Called marker
- Blocked marker
- Exception marker
- Arm-file suffix
- Planner marker
- Metadata marker
- Role marker
- No arm file in normal staged package
- No `FName::ToString` import
- No UObject discovery-vector destructor import

### Unarmed runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c3-unarmed-20260806-095324
```

Results:

```text
Gate disabled markers:   1
Gate armed markers:      0
Registration called:     0
Registration blocked:    0
Registration exceptions: 0
Planner passes:          25
Planner incomplete:      0
Metadata passes:         25
Metadata incomplete:     0
Chest passes:            25
Chest incomplete:        0
Invalid thread markers:  0
Other exceptions:        0
Crash markers:           0
```

Final plan remained:

```text
guilds=8
active_guilds=7
chests=157
storages=20
pairs=285
own_camp=157
all conflict and error counts=0
```

The isolated mod and save were restored exactly.

Production remained:

```text
PalServer PID:
171

Container StartedAt:
2026-08-06T08:33:34.425634823Z
```

### Armed runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c3-armed-20260806-100441
```

An arm file was created only inside the isolated candidate:

```text
dlls/main.so.stage4c3-arm
```

Results:

```text
Gate armed markers:         1
Gate disabled markers:      0
Registration detail lines:  1
Registration called:        1
Registration blocked:       0
Registration exceptions:    0
Post-call planner passes:    24
Planner incomplete:         0
Post-call metadata passes:   24
Metadata incomplete:        0
Post-call chest passes:      25
Chest incomplete:           0
Invalid thread markers:      0
Other exception markers:     0
Crash markers:               0
New crash files:             0
```

Controlled call detail:

```text
run=1
plan=1
chest=1
chest_camp=1
target=1
target_camp=1
different_camps=1
same_guild=1
storage_class=1
game_thread=1
dedicated=1
metadata=1
parms=8
offset=0
size=8
```

The server remained stable for 180 seconds after the call.

The one-shot guard prevented additional registration calls across later
scans.

The pre-restoration `Level.sav` hash changed:

```text
e6a3a55f272bcc3535827e4c707e97397bc8726626a13847ff366f71b54cd436
```

This is not treated as proof of registration persistence because a
running Palworld server normally updates its save and no equal-duration
unarmed control save was compared.

After the test:

```text
Restored isolated mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72

Restored Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Restored player saves:
19
```

Production remained unchanged:

```text
PalServer PID:
171

Container StartedAt:
2026-08-06T08:33:34.425634823Z
```

### Stage 4c.3 acceptance statement

Accepted:

- One real reflected registration call completed
- The call used the Unreal game thread
- All planned safety checks passed
- Exactly one call occurred
- The server remained stable for 180 seconds
- No native exception or crash evidence appeared
- The isolated environment was restored
- Production was unchanged

Not accepted yet:

- Observable storage membership change
- Duplicate-call idempotency
- Complete pair registration
- Periodic reconciliation
- Restart persistence
- Stale registration removal
- Production use

---

## 10. Known operational mistakes and lessons

The following failures occurred during development and are retained here
to prevent recurrence.

### Shell and harness issues

- Nested quoting expanded variables under `set -u`
- Large heredocs were mangled by chat rendering
- Embedded Markdown fences ended shell blocks early
- `fc -ln -1` was not suitable for recovering multiline heredocs
- Long terminal echo could look corrupted even when execution succeeded
- Host has no `python3`; Python must run in the development container
- `grep | head | tee` under `set -o pipefail` produced SIGPIPE status 141
- Evidence report generation must not invalidate an otherwise accepted
  runtime test
- `pgrep -f 'PalServer-Linux-Shipping'` can match the shell command performing
  the lookup; use the self-excluding pattern `[P]alServer-Linux-Shipping`
- Host-level diagnostic collectors must not assume `python3`; Python belongs in
  the development container unless host availability was explicitly checked

### Runtime harness issues

- An early harness checked `UE4SS.log` instead of Docker stderr
- Startup checks initially had no grace period
- Initial populated-world runs stopped at transient `EMPTY`
- Save-clone assumptions failed repeatedly
- A broad grep for `archive` matched unrelated `FArchiveState`
- Post-slice summary counts can be one lower than PASS markers when the
  slice begins at the first PASS line rather than its preceding summary

### Source patch issues

- Several patch scripts guessed source shape incorrectly
- Stage 4c.1d had a duplicate anchor
- Conditional or ternary selection of marker literals decays them to
  `const char*`; literal-only `emit_marker` calls must use explicit branches
- A correct build was rolled back by an incorrect string-encoding audit
- Stage 4c.2 initially called `.c_str()` on `std::array<char, 33>`
- A survey searched for a contiguous registration string even though the
  C++ source split it across two adjacent literals
- The first Stage 4c.3 audit searched for the complete runtime arm
  filename in source even though source only stored the suffix
- Stage 4d.5b checkpoint v1 incorrectly required exactly one textual
  `RESULT=INCOMPLETE` exit even though the probe legitimately had seven; the
  wrapper failed before `git add`
- Later build packages retained stale Stage 4d.2 `BUILD-PROVENANCE.txt` scope
  prose after the source had advanced; build provenance text must be audited as
  part of the next accepted checkpoint

### Safety lessons

- Artifact-level `ProcessEvent` import does not prove registration
  mutation because role and ownership queries legitimately import it
- Source call-site inventory is authoritative for controlled mutation
- A successful `ProcessEvent` return does not prove gameplay effect
- Textual absence of a removal function is not proof of runtime absence
- An ambiguous standalone module-pin crash must not be treated as a
  definitive pin failure
- Production must be checked independently before claiming it remained
  unchanged
- Docker `Running` can survive a child process boot loop
- A manager-membership oracle is not automatically the semantic definition of
  Integrated Storage success. `GetGroupIdByItemContainerId` proves one manager
  layer; it does not prove upstream registration is ineffective when unchanged.
- An accepted stage checkpoint must update the engineering runsheet in the same
  commit as the accepted code/build changes. Stage 4d.4r broke this discipline and Stage 4d.5b repeated the mistake,
  leaving the runsheet at Stage 4d.2 while source advanced through both commits.
  No further accepted push may repeat it.

---

- A semantic fingerprint containing an embedded `FName` is
  same-process evidence only. Runtime harnesses must not compare it with a
  fingerprint produced by another PalServer process.
- A pre-readiness `SINGLE_REGISTER RESULT=BLOCKED` scan performs no
  `ProcessEvent` call. Harnesses must distinguish registration detail lines
  from the sole `RESULT=CALLED` mutation.

## 11. Current safety rules

The following rules remain mandatory:

- No global `LD_PRELOAD`
- No second UE4SS installation
- No direct production mutation during development
- No Unreal `ProcessEvent` from worker threads
- No incoming chat `FText` mutation
- No direct `ModuleArray` mutation when a reflected server function exists
- No full 285-pair call loop before effect observability
- No duplicate-call test before effect observability
- No periodic reconciliation before exact-pair idempotency
- No claim of persistence from a changed save hash alone
- No production deployment before rollback and reconciliation are proven
- Preserve upstream MIT attribution
- Preserve `src/dllmain.cpp`
- Keep normal staged mutation candidates unarmed by default
- Snapshot and restore isolated mod and save around every mutation test
- Verify production PID and container start time after every isolated test
- Use `pgrep -f '[P]alServer-Linux-Shipping'` for child-PID detection
- Do not claim upstream registration failed solely because
  `GetGroupIdByItemContainerId` remains zero
- Do not perform further guild-registration archaeology until the server-side
  upstream parity audit defines the actual required semantic effect
- Do not commit/push an accepted stage unless `docs/linux-port-status.md` is
  updated, its structure audit passes, and the current position / sole next
  action advance in the same checkpoint
- Do not accept stale `BUILD-PROVENANCE.txt` scope prose in a checkpoint package

---

## 12. Stage 4c.4 plan — effect observability

### Historical objective

Stage 4c.4 originally attempted to find a readable state proving that a selected
foreign chest had been registered into a target storage.

The investigation eventually established a reliable
`GetGroupIdByItemContainerId` membership oracle, but later work treated a
zero-to-guild transition on that oracle as if it were necessarily the upstream
Integrated Storage success condition.

### 2026-08-09 scope correction

That stronger requirement is not established by the upstream server path.

The recorded upstream server implementation performs foreign-camp registration
through:

```text
PalBaseCampModuleItemStorage.OnAvailableConcreteModel_ServerInternal(chest)
```

It does not, in the source evidence currently recorded by this runsheet,
explicitly require a `PalContainerId -> GuildId` manager-membership transition.

Therefore the following distinction is mandatory:

```text
membership transition absent
!=
upstream Integrated Storage effect absent
```

Stage 4c.4j had already warned that an unchanged aggregate fingerprint did not
prove registration had no effect; routing, visibility, ownership, or another
state surface could carry that effect. That warning now governs the project.

### Revised acceptance problem

Before another registration mutation, first define the actual server-side
behaviour that upstream relies on. A valid observable must be tied to that
behaviour, such as a server-side material/container query whose result changes
when a foreign same-guild chest is registered.

A future mutation test must then measure:

1. the functional observable before registration;
2. one exact upstream-equivalent registration call;
3. the same observable immediately after registration;
4. the observable after a stability interval;
5. server stability, thread validity, isolated restoration, and production
   continuity.

The existing membership query may remain a secondary diagnostic, but it is not
the primary acceptance condition unless the upstream parity audit proves that it
is semantically required.

### Prohibited until parity is re-established

- Full 285-pair mutation
- Duplicate-call/idempotency mutation
- Periodic reconciliation
- Production deployment
- New speculative lifecycle callback invocation
- Direct manager-map inspection
- Broad reflected graph traversal
- Treating unchanged manager membership as proof of no Integrated Storage effect

---

## 13. Later stages

### Stage 4d.6 — server-side upstream parity audit

- Diff the dedicated-server logic in `src/dllmain.cpp` against `src/linux/main.cpp`
- Enumerate upstream server discovery, filtering, registration, reentrancy, and
  reconciliation behaviour
- Classify omitted code as client-only, Windows-only infrastructure, or genuinely
  missing server-side behaviour
- Determine whether upstream ever depends on manager guild membership
- Define one direct functional server-side acceptance observable
- No runtime mutation

### Functional single-pair acceptance

- Measure the parity-audit-selected functional observable
- Call one exact same-guild foreign-camp pair through the upstream-equivalent
  server registration function
- Re-measure the same functional observable
- Retain manager membership only as secondary telemetry unless proven required
- Confirm no duplicate/side-effect assumptions yet
- Restore isolated state

### Exact-pair idempotency

- Observe the functional effect before first call
- Call the selected pair once
- Observe the effect
- Call the same pair a second time
- Confirm the functional result is stable and no duplicate state is created
- Confirm no crash
- Restore isolated save

### Complete planned registration

- Explicit feature gate
- Execute all 285 deduplicated pairs
- Reentrancy guard
- Per-pair success/failure diagnostics
- No periodic reconciliation initially
- Observe the validated functional storage effect
- Long populated-world stability window
- Full restoration

### Reconciliation

- Periodic rebuild
- Idempotent repeat registration
- World-transition handling
- Camp and guild changes
- New chest handling
- New camp handling
- Stale pointer handling
- Removal-path investigation

### Restart and persistence

- Server restart
- World reload
- Rebuild registration state
- Confirm validated storage behaviour after restart
- Determine whether registration is runtime-only or serialised

### User-facing configuration

- Enable/disable
- Reconcile interval
- Diagnostic verbosity
- Explicit dedicated-server guard
- Safe defaults

### First usable release gate

The first usable release requires:

- Upstream server-side parity explicitly audited
- Observable functional registration effect
- Exact-pair idempotency
- Full-plan execution
- Reconciliation
- Populated-world stability
- Restart validation
- Installation guide
- Rollback guide
- Troubleshooting guide
- Production acceptance plan

---

## 14. Latest evidence paths

### Accepted Stage 4c.4j semantic observation

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/
integrated-storage-stage4c4j-semantic-observation-20260806-161619
```

Accepted result:

```text
SEMANTIC_OBSERVATION RESULT=UNCHANGED
```

The Stage 4c.4j interpretation remains important: an unchanged aggregate
fingerprint did not prove the registration call had no effect outside the
observed aggregate slots.

### Later accepted evidence identities

Late Stage 4c evidence archive SHA256 values:

```text
Stage 4c.4u:
c110b6ceca9446283abd405914ede80a553234dcf274f34c484111067bae2dc6

Stage 4c.4v:
346b83766b58b54c5f7ae04c08e46f4ed76492f344867b6aba2ebb376faf82e3

Stage 4c.4w:
d78db90fa9cf0a90cd86a742b6faa90d1e963ce758de59777832745d139236b9

Stage 4c.4x:
1d048eb2eac6509551c054881eebc1756e77fbf209b7db0b872436dce9acdad7
```

Read-only / diagnostic Stage 4d evidence:

```text
Stage 4d.0 accepted runtime evidence:
6f274fb62cf9be7626c6d17843619205308b3a9532ad3113a89a701042f4311a

Stage 4d.1 accepted runtime evidence:
5e5fc3901e33e64dabc7ced580ea3bd6a150dc4794f5f1eb91669e18c0a93477

Stage 4d.2 accepted runtime evidence:
5afabb231cb198ffe00fc70c34f7b14725caa873d3596751b435598348920b21

Stage 4d.3 accepted read-only source/offline survey evidence:
49fd35437f15438ea3cdaece156fd4e560106db777c62c5926ac13d64a571524

Stage 4d.4 rejected/incomplete offline ELF ownership evidence:
4f3b1521f8c2a963c0852983d7b6f8d6868b17f40e9490acd1ed1898cc1d7961

Stage 4d.4r runtime postmortem evidence:
c18b6f5e9930ab34137f6ab02d2123c00f56e33026de3a7e5d1da39b75f8fd38

Stage 4d.4r accepted checkpoint evidence:
6a0e661bf0155a640fa88fb64bdedd3ad16c7dffeefc88f0599dabc17c266537

Stage 4d.5a source-topology extraction evidence:
a021a8a8f42585306980a92147f73190ff59efe19df0491a1108ffc0921af331

Stage 4d.5b controlled-negative postmortem evidence:
582fadfa947eeae62b874c1d0bf1fb3a44ea1c568309fa74087f99aa72ed0add
```

The normal isolated state remains:

```text
Mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72

Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Player saves:
19
```

Current production continuity reference:

```text
PID:
83

StartedAt:
2026-08-08T18:30:02.661576192Z

RestartCount:
0
```

Earlier accepted evidence paths remain recorded in their chronological stage
sections.


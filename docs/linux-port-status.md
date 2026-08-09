# Linux Port Engineering Status

Single source of truth for the native Linux dedicated-server port of
Palworld Integrated Storage. Current state first, chronological log at the
bottom.

This file replaces the previous three-file split
(`linux-port-status.md` / `linux-port-history.md` /
`linux-port-evidence-index.md` + `docs/history/part-01.md`…`part-09.md`),
which existed mainly to satisfy GitHub's Markdown preview size limit and
tracked a SHA-256 for every chunk purely for byte-reproducibility. That
byte-for-byte history is not lost — it's still in git (`git log --follow`
on those paths, or `git show <old-commit>:docs/linux-port-history.md`) —
it's just no longer duplicated or checksummed in the working tree. Going
forward, log new stages in the table at the bottom of this file: one row,
plain language, no per-entry hash.

## 1. Target and scope

```text
Palworld native Linux dedicated server, x86-64
NullPrism RE-UE4SS-Linux
native C++ user mod: main.so
branch: claude/palworld-linux-storage-mod-gx9n5d (based on linux/nullprism-dedicated-server)
```

- Dedicated-server native Linux support only. Windows clients keep using the
  upstream Windows DLL; native Linux client support is out of scope.
- Windows source must not be touched by Linux port work.
- Production stays isolated from dev/runtime probes unless a stage explicitly
  authorizes production deployment.

## 2. Pinned dependency baseline

```text
NullPrism/RE-UE4SS-Linux tag linux-v0.1.0, commit 5d33654755efed844336497e8a9a15e6716b5d6c
Official release loader SHA256: 26dffce875fb771fb2ac2a63325e7effb5551a03a35598810f13d2e6c854a1ff

Current PalServer ELF SHA256: c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e
Current PalServer ELF Build ID: 787f7f8c15edb8fb
```

## 3. Accepted runtime baseline (Stage 4d.8a R2)

The last artifact accepted as production-quality (before this session's
diagnostic-only 4D.9 stages) is Stage 4d.8a R2. It runtime-proves:

- dedicated-server role resolution;
- mature camp/guild/storage/chest planner (`DISCOVERY`, `WOULD_REGISTER`);
- same-guild foreign-camp registration planning;
- bounded exact chest → module → container-id → container-manager lookup;
- reflected `PalItemContainer` slot-array inspection for planner-selected
  chests only;
- exact request/reply RPC metadata and camp GUID layout;
- exact `PalItemId.StaticId` FName layout;
- same-guild-minus-own-camp material-pool aggregation (bounded transport
  pool).

Bounded transport pool result (stable, repeated hundreds of ticks):

```text
foreign_chests=22  containers=22  slot_arrays=22  slot_objects=850
positive_slots=288  fully_read_slots=288  layout_failures=0  exceptions=0
unique_items=272  total_quantity=69227  passed=1
```

Registration executor (Stage 4d.7a), one-shot, arm-gated:

```text
planned=285 attempted=285 completed=285 blocked=0 exceptions=0
function_mismatches=0 guild_mismatches=0 camp_mismatches=0
storage_class_mismatches=0 game_thread=1 dedicated=1 metadata=1 RESULT=PASS
```

**Known gap:** the executor is one-shot. Stage 4d.7b proved the mature
planner correctly detects a new same-guild camp created *after* server
startup (20→21 storages, 285→307 pairs), but the one-shot executor never
applies the expanded plan. A periodic/topology-aware reconcile executor is
still required.

**Known gap:** Stage 4d.7b also proved that server-side registration alone
is insufficient for a real Windows client — the client only shows Guild
Chest resources it already understands; it never surfaces the wider
same-guild material pool without the actual upstream wire transport
(`ISREQ`/`IS1`, below).

## 4. Transport wire protocol (upstream, not yet implemented server-side)

```text
client -> server:  PalPlayerController:Debug_CheatCommand_ToServer(FString), NetServer reliable
  request sentinel: "ISREQ|"
  request:          "ISREQ|<32-hex campGuid>"

server -> client:  PalPlayerController:Debug_ReceiveCheatCommand_ToClient(FString), NetClient reliable
  reply sentinel:   "IS1|"
  reply:            "IS1|id:cnt,id:cnt,"
```

Server side needs to: resolve the requester camp → resolve its guild →
aggregate same-guild storage excluding the requester's own camp → serialize
item IDs as strings (FName indices are process-local, can't cross the wire
as raw indices) → send `IS1|...` reply. This is currently blocked entirely
on the FName-stringification problem below.

## 5. FName stringification — current status (Stage 4D.9, this session)

**Bottom line, confirmed by direct runtime evidence this session:**
`FName::ToString()` is unsafe to call anywhere in this mod, in this exact
NullPrism-Linux + Palworld build, regardless of how the FName is obtained.
This *reconfirms* the original Stage 4d.8b finding (below), which an
earlier truncated log read in this session had briefly and incorrectly
cast doubt on.

Exact behavior observed, twice, independently:

- `Stage 4D.9a` — FName from `UClass::GetFName()` (e.g. `PalBaseCampModel`):
  `ToString()` returns a **fully correct** decoded string, then the process
  dies shortly after with `FMallocBinned2 Attempt to realloc an
  unrecognized block ... canary == 0x0 != 0xb7`.
- `Stage 4D.9b` — FName memcpy'd out of a live `PalItemId.StaticId`
  reflection property (the same bytes the bounded transport pool already
  reads safely, e.g. decodes to `YakushimaBlade003_3`): same result —
  correct decode, then the identical crash shortly after.
- Control: with **no** FName probe armed, the same server ran 288+ ticks
  (full discovery/registration/pool-walk cycles) with zero crashes. The
  850-slot pool walk itself is proven safe on its own.

So the crash is not caused by *which* FName you call `ToString()` on, and
it is not caused by the bounded pool walk. It is caused by calling
`ToString()` at all — the call itself returns valid data, then something
it does internally (most likely: an allocation that crosses the
cross-DSO/allocator boundary between the mod's `main.so` and the engine's
`FMallocBinned2`) corrupts the heap, and the *next* unrelated allocation
after that is where the corruption is detected and the process dies.

`Stage 4D.9c` (in progress, this session) isolates the remaining open
variable: does the corruption come from the engine's own
`FName::ToString()` → `FString` allocation, or from `RC::to_string()`'s
char16_t→`std::string` conversion helper on the mod side? It calls
`ToString()` on the same pool-sourced FName as 4D.9b but never calls
`RC::to_string()` on the result. If it still crashes, the bug is inside the
engine-side call and `ToString()` is a dead end for this port entirely. If
it does not crash, `RC::to_string()` is the culprit and can be reworked.

This matches and refines the original (pre-session) Stage 4d.8b finding:

```text
Stage 4d.8b R2 (original wire-serialization attempt):
LowLevelFatalError, MallocBinned2.cpp
FMallocBinned2 Attempt to realloc an unrecognized block, canary mismatch
Signal 11, Segmentation fault
```

`FName::GetPlainNameString()` is also blocked (Stage 4d.8c proved the
pinned implementation just calls `FName::ToString()` internally).

Static/offline recovery work (Stage 4d.8d–4d.8g) established that
patternsleuth's `FNamePool` resolver semantics are known
(`FNamePool(pub u64)` = direct static address on UE 4.23+), but the
current NullPrism `patternsleuth_bind` bridge doesn't expose it and no
usable `FNameEntry` decoder chain has been recovered — so there is currently
**no known safe way to turn a Palworld-Linux FName into a string**, direct
or offline-assisted.

FName number semantics (needed once/if a decoder exists): `NAME_NO_NUMBER_INTERNAL = 0`,
`NAME_INTERNAL_TO_EXTERNAL(x) = x - 1`. At least one observed bounded-pool
FName had a non-zero number, so any eventual decoder must preserve the
numeric suffix, not just the base name.

## 6. Blocked / exhausted paths — do not retry without new evidence

1. Direct `ItemContainerMap_InServer` manager-map inspection — allocator corruption.
2. Broad reflected graph / `TFieldRange` traversal — allocator corruption.
3. Bulk `FindAllOf("PalItemContainer")` — allocator corruption.
4. `FName::ToString()` / `FName::GetPlainNameString()` — see §5, allocator fatal (SIGSEGV) after a correct decode.
5. Selected-chest property guesses, fixed accessor guesses — exhausted negative.
6. Standalone registration does not prove membership transition.
7. `OnReadyItemContainerGuildChest`, `OnUpdateItemContainerModule`, `OnUpdateItemContainer` transition paths — all negative.
8. Stage 4d.0 lifecycle exact-name probes — zero matches.
9. GuildChestModel module route — controlled negative.

The accepted transport pool must keep using bounded planner-selected chest
lookups only (§3), never broad enumeration.

## 7. Environment reference

Production (last confirmed Stage 4d.8g R3):

```text
container: Palworld
PalServer PID: 83 (as of 2026-08-08T18:30:02Z, RestartCount 0 — a continuity
  reference, not a guarantee the PID never changes)
internal server UDP: 8211 (hex 2013 — do NOT use the host-published 18211
  hex value when validating the socket from inside the container)
```

Isolated test environment:

```text
container: Palworld-NullPrism-Test
host UDP: 127.0.0.1:18211 -> container UDP 8211
admin TCP: 127.0.0.1:35575 -> 25575
restart policy: no
public lobby: disabled
world ID: 6E97A850BDBA49838BCFCC6190C15217
baseline Level SHA256: a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6
```

Keep the isolated server stopped outside explicitly authorized runtime
stages.

Dev container (build/deploy):

```text
container: palworld-mod-dev
host repo mount: /mnt/disk1/Development/palworld-linux-mods/workspace -> /workspace
staging output: /staging/ModIntegratedStorageCpp (host: .../staging)
runtime deploy target: /mnt/disk1/Development/palworld-linux-mods/runtime-test/serverfiles/Pal/Binaries/Linux/Mods/ModIntegratedStorageCpp/
```

## 8. Immediate next action

1. Land Stage 4D.9c's result (this session, in progress) — confirms whether
   `ToString()` corruption is engine-side or `RC::to_string()`-side.
2. If engine-side: `FName::ToString()` and everything downstream of it is a
   dead end on this runtime. Pursue the offline patternsleuth `FNamePool` +
   `FNameEntry` decoder path (Stage 4d.8h scope below) as the only route to
   safe name serialization.
3. If `RC::to_string()`-side: rework the char16_t→string conversion (e.g.
   avoid whatever allocation pattern crosses the DSO boundary) and re-test.
4. Independently of FName serialization: still need a periodic/topology-aware
   registration reconcile executor (§3 known gap).

Stage 4d.8h scope (still valid, not yet started): offline PalServer FName
resolver + bounded disassembly. Re-verify PalServer SHA256, copy into
`palworld-mod-dev`, compile a tiny offline helper against the already-proven
`ps_scan_file_ue4ss` / `PsFileResolutionResults` C ABI, link only against
the pinned `libUE4SS.so`, call it once, and require: engine version `5.1`,
`fname_tostring != 0`, the returned address inside an executable `PT_LOAD`,
and `objdump` decoding real instructions at that address — before trusting
it enough to disassemble the FName decoder. No PalServer execution, no
`FName::ToString()` execution, no accepted-source modification during this
stage.

## 9. Stage log

Chronological, most recent first. One line per stage: what it did, what it
proved, accept/reject. Full byte-for-byte historical detail for stages
before this table existed is preserved in git (see top of file), not
duplicated here.

| Stage | Result |
|---|---|
| 4D.9c | *(in progress)* ToString() call with no `RC::to_string()` conversion — isolates engine-side vs. mod-side corruption source. |
| 4D.9b | Accepted diagnostic. `ToString()` on a memcpy'd/pool-sourced FName decodes correctly, then crashes shortly after (§5). |
| 4D.9a | Accepted diagnostic. `ToString()` on a `GetFName()`-sourced FName decodes correctly, then crashes shortly after (§5). |
| 4d.8g R3 | Accepted static ABI + ELF provenance. Confirmed current PalServer ELF identity and `ps_scan_file_ue4ss` C ABI. |
| 4d.8g R1/R2 | Rejected, wrapper/pre-flight failures, no scanner/runtime reached. |
| 4d.8f R2 | Accepted static characterization. Pinned patternsleuth `FNamePool` resolver semantics. |
| 4d.8f R1 | Rejected, missing-dependency wrapper assumption. |
| 4d.8e | Accepted static, incomplete. patternsleuth `FNamePool` resolver audit. |
| 4d.8d | Accepted static, incomplete. Lower-level FName entry/name-pool C++ API audit — no complete decoder chain found. |
| 4d.8c | Accepted static negative. Proved `GetPlainNameString()` calls `ToString()` internally (also blocked). |
| 4d.8b recovery | Accepted. Restored accepted 4d.8a source/artifact after the 4d.8b crash. |
| 4d.8b postmortem | Accepted failure analysis of the 4d.8b R2 crash. |
| 4d.8b R2 | Rejected, runtime-crashed. First direct `FName::ToString()` wire-serialization attempt — `FMallocBinned2` SIGSEGV. |
| 4d.8b R1 | Rejected, compile failure (wchar/char16_t mismatch). |
| 4d.8a R2 | **Accepted runtime baseline** (§3). Transport metadata + bounded foreign-pool runtime. |
| 4d.8a R1 | Rejected, compile failure. |
| 4d.8 | Accepted static transport parity audit (remote-client). |
| 4d.7b | Accepted. Real Windows client vs. isolated Linux server — `CLIENT_BLOCKED` (§3 known gap). Proved dynamic planner topology detection. |
| 4d.7a | **Accepted runtime executor** (§3). Arm-gated full-plan one-shot registration. |
| 4d.6 | Accepted static parity audit, server-side upstream comparison. |
| 4d.5b | Rejected/postmortem. GuildChestModel null-module controlled negative. |
| 4d.5 | Live GuildChestModel anchor comparison. |
| 4d.4r | Exact runtime reflection-owner matrix. |
| 4d.4 | Offline ELF symbol ownership attempt. |
| 4d.3 | Guild-storage anchor source survey. |
| 4d.2 | Access-owner native class identity. |
| 4d.1 | Access-owner lifecycle metadata discovery. |
| 4d.0 | Bounded registration-lifecycle metadata discovery — zero matches. |
| 4c.4a–4c.4x | Long observability/characterization series (class metadata, reflection, slot/container layout, callback probes, access-interface signatures). Consolidated into the Stage 4c conclusions that fed the 4c.3/4d.7a registration executor. Individually superseded by later accepted stages; exact per-run detail in git history. |
| 4c.4j | Accepted semantic observation, controlled before/after registration. |
| 4c.3 | Accepted. Controlled single registration — first accepted registration executor precursor. |
| 4c.2 | Deterministic would-register planner. |
| 4c.1–4c.1f | Upstream registration-path survey, read-only metadata probe, game-thread role hardening. |
| 4b.1–4b.2a | Chest object discovery, callback thread probe, chest→camp→guild association. |
| 4a | Camp, guild, and storage discovery — first read-only discovery stage. |

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
(`ISREQ`/`IS1`, below). Stage 4E.1 (§9) implemented the server side of
this transport; it has not yet been validated against a real Windows
client (§8 item 2).

## 4. Transport wire protocol (Stage 4E.1: server side implemented)

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
as raw indices) → send `IS1|...` reply. This was blocked entirely on the
FName-stringification problem below until Stage 4D.9g (§9) cleared it.

**Stage 4E.1 implementation (§9):** the request is parsed and queued by a
new `UObjectGlobals::RegisterHook` on
`/Script/Pal.PalPlayerController:Debug_CheatCommand_ToServer` — the hook
callback itself does nothing but parse the FString and enqueue a
`{controller, requester_camp_id}` record, deliberately avoiding any
reflection/`FindAllOf`/`ProcessEvent` work inside a call context
(RPC dispatch) this mod has never exercised before. The actual pool build
and reply happen on the next `on_engine_tick` (the same proven-safe
context `run_read_only_chest_association` already uses hundreds of times
per discovery pass), against a `{camp id → camp, camp → guild,
guild → RegistrationPlanGuild}` snapshot cached at the end of the most
recent discovery pass (refreshed every `DiscoveryInterval`, cleared on
world change). The pool walk itself
(`build_transport_pool_for_request`) is a standalone duplicate of the
accepted §3 bounded planner-selected-chest walk — not a refactor of the
existing diagnostic probe — so this new path can never change the
already-proven probe's behavior. The outbound `IS1|...` reply is built as
a plain `std::string`, widened into a deliberately-leaked (never freed)
raw `TCHAR` buffer, and handed to `ProcessEvent` as a raw
`{data,num,max}` triple — no `RC::Unreal::FString` object is ever
constructed for the reply, so no engine-owned destructor for it can ever
run inside `main.so` (same leak-and-never-destruct rationale as §5's
`resolve_transport_item_name()`, applied to the one other place this mod
hands an Unreal-visible string-shaped buffer back to the engine).
Not yet build-verified or runtime-tested (§8 item 1 code-complete,
item 2 pending).

## 5. FName stringification — current status (Stage 4D.9, this session)

**Bottom line, revised and narrowed by Stage 4D.9d:** `FName::ToString()`
itself is safe to call from this mod's DSO, on this exact NullPrism-Linux +
Palworld build, regardless of how the FName is obtained. The corruption
does **not** come from the call. It comes from letting the returned
FString-like object's destructor run inside `main.so`. If that destructor
is never invoked (the result is deliberately leaked instead), the server
keeps running normally with zero crashes. This reopens direct
`ToString()`-based name serialization as a viable production path,
provided results are never destructed the normal way — see the leak/cache
design note below.

Exact behavior observed:

- `Stage 4D.9a` — FName from `UClass::GetFName()` (e.g. `PalBaseCampModel`):
  `ToString()` returns a **fully correct** decoded string, its destructor
  runs normally, and the process dies shortly after with `FMallocBinned2
  Attempt to realloc an unrecognized block ... canary == 0x0 != 0xb7`.
- `Stage 4D.9b` — FName memcpy'd out of a live `PalItemId.StaticId`
  reflection property (the same bytes the bounded transport pool already
  reads safely, e.g. decodes to `YakushimaBlade003_3`): same result —
  correct decode, destructor runs normally, identical crash shortly after.
- `Stage 4D.9c` — same pool-sourced FName as 4D.9b, `ToString()` called with
  no `RC::to_string()` conversion (only `.size()` read from the result,
  `length=23`), destructor still runs normally: **still crashed**, same
  signature, same place. This ruled out the mod-side `RC::to_string()`
  conversion helper as the cause — the corruption is inside whatever runs
  when `ToString()`'s result is torn down, not in the mod's own conversion
  code.
- `Stage 4D.9d` — same pool-sourced FName again, but the returned value was
  placement-constructed into a static raw byte buffer and its destructor
  was **deliberately never called** (a controlled, single-shot,
  diagnostic-only leak of one small string buffer). Result: `OBTAINED
  comparison_index=6642944 number=0` → `TOSTRING_RETURNED` → `LENGTH
  length=17` → `RESULT=PASS`, and the server then continued running
  normally for 283+ further ticks with **zero crashes**. This is the
  decisive result: suppressing the destructor alone — nothing else changed
  versus 4D.9c — eliminated the crash entirely.
- `Stage 4D.9e` — same pool-sourced FName and same leaked/undestructed
  placement-new pattern as 4D.9d, but this time the mod also runs
  `RC::to_string()` on the still-alive leaked result and logs the converted
  character data (not just `.size()`). Result: `RESULT=PASS` — reading
  character data out of a leaked, never-destructed `ToString()` result does
  not reintroduce the corruption. This closes the gap 4D.9d left open (see
  "Proven" below): both existence and content are safely readable off a
  leaked result.
- `Stage 4D.9f` — production-facing repeating probe: leaks one `ToString()`
  result **per engine tick** on the same pool-sourced FName, reads its
  character data via `RC::to_string()` each time (never destructing the
  original), and logs cumulative `leak_count` alongside live process RSS
  every 5 seconds. Deployed to the production container and monitored for
  2+ continuous hours in a single, uninterrupted boot, covering roughly
  121,000 leak-and-read cycles (`leak_count` 217,616 → 338,743) with **zero
  crashes**. RSS grew from a ~1.45GB delta above baseline to a ~1.57GB delta
  over the window — a real but slow, roughly linear leak (~1KB per leaked
  object, ~60MB/hour) — fully absorbed by the existing daily 04:30
  production restart. This is the strongest evidence yet that leak-and-cache
  is safe at sustained, worst-case-frequency production load.
- Control (established earlier in this session): with **no** FName probe
  armed, the same server ran 288+ ticks with zero crashes. The 850-slot
  pool walk itself is proven safe on its own.

**Conclusion:** the crash is not caused by *which* FName you call
`ToString()` on, not caused by the bounded pool walk, and — as of 4D.9d —
not caused by the `ToString()` call itself. It is caused specifically by
running the returned FString-like object's destructor inside `main.so`.
The leading theory is a cross-allocator/cross-DSO mismatch: the engine
(inside `PalServer-Linux-Shipping`) allocates the result's internal buffer
through its own `FMallocBinned2` instance, and the mod's compiled
destructor (in `main.so`) doesn't route the free back through that exact
same allocator instance, corrupting allocator bookkeeping. The corruption
doesn't crash immediately — it's detected on a later, unrelated
allocation/reallocation, which is why every prior stage crashed shortly
*after* a correct decode rather than during the call itself.

**Practical implication:** `FName::ToString()` can be used in production for
the `ISREQ`/`IS1` wire transport's item-name serialization, as long as the
mod never lets a `ToString()` result destruct normally. Stage 4D.9g (§9)
implemented this: `resolve_transport_item_name()` calls `ToString()` once
per unique FName (keyed by the same raw 8-byte `TransportItemNameKey` bytes
already used for the transport pool), copies the character data out into a
cached plain `std::string` (safely destructible — it uses the mod's own
libstdc++ allocator, not `FMemory`), and leaks the original `ToString()`
result deliberately (never destructs it). Item names are a small bounded
set (272 unique items observed in the test guild's pool), so the leaked
footprint is negligible and bounded for the life of the server process.
`TRANSPORT_POOL_ITEM` log lines now report the resolved name alongside the
raw hex key.

**Proven (as of 4D.9e/4D.9f):** reading character data out of a leaked,
never-destructed `ToString()` result — via `RC::to_string()` — is safe,
both single-shot (4D.9e) and under sustained per-tick production load
(4D.9f: ~121,000 cycles over 2+ hours, zero crashes). The leak-and-cache
design (§8) is no longer gated on further diagnostics; it is cleared for
production implementation.

The offline patternsleuth `FNamePool`/`FNameEntry` decoder path (Stage
4d.8h, §8) remains a fallback if the character-data read turns out to be
unsafe, but is no longer believed to be the only route to safe name
serialization.

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
4. `FName::ToString()` **destructed normally** / `FName::GetPlainNameString()` — see §5, allocator fatal (SIGSEGV) after a correct decode, triggered by the result's destructor. `ToString()` with the result deliberately leaked (never destructed) — including reading its character data via `RC::to_string()`, single-shot and under sustained per-tick production load — is proven safe (Stage 4D.9d/4D.9e/4D.9f, §5), no longer blocked, and now implemented in production as `resolve_transport_item_name()` (Stage 4D.9g, §9).
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

Stage 4E.1 (§9) implemented the server-side `ISREQ`/`IS1` wire handler
(§4): a `Debug_CheatCommand_ToServer` hook queues parsed requests,
`on_engine_tick` drains the queue against a cached registration-plan
snapshot, builds the guild-minus-own-camp pool via a standalone bounded
walk, and replies with a leaked raw-buffer `IS1|...` FString. This is
code-complete but has not been built or runtime-tested yet.

1. Build, deploy, and monitor Stage 4E.1 the same way as every prior
   stage (dev container build → isolated test server → log monitoring)
   before trusting it. Specifically watch for: `TRANSPORT_HOOK
   registered=1` at startup, `TRANSPORT_CACHE camps=N guilds=N` each
   discovery pass, and — once a real client sends a request —
   `TRANSPORT_REQUEST queued=1` followed by `TRANSPORT_REQUEST
   RESULT=SENT items=N len=N` with zero crashes.
2. Validate end-to-end against a real Windows client, not just server logs —
   4d.7b showed server-side registration alone doesn't surface the wider
   pool to a real client without the actual wire transport (§3 known gap).
   This also validates the two biggest unproven assumptions in Stage
   4E.1: that `UObjectGlobals::RegisterHook` on an incoming server RPC is
   a safe call context for this mod, and that a raw leaked-buffer
   `RawTArray` (never an `RC::Unreal::FString`) is accepted by
   `ProcessEvent` as a valid outbound FString parameter.
3. Independently of FName serialization: still need a periodic/topology-aware
   registration reconcile executor (§3 known gap) — the executor is one-shot,
   and 4d.7b proved it misses camps created after server startup (20→21
   storages, plan never re-applied).

Stage 4d.8h scope (fallback, not currently the priority): offline PalServer
FName resolver + bounded disassembly. Re-verify PalServer SHA256, copy into
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
| 4E.1 | Implemented, not yet build-verified or runtime-tested. Server-side `ISREQ`/`IS1` wire handler (§4): `RegisterHook` on `Debug_CheatCommand_ToServer` parses and queues requests; `on_engine_tick` drains the queue every tick against a registration-plan snapshot cached at the end of each discovery pass (cleared on world change); pool build is a standalone duplicate of the accepted §3 bounded chest walk (`build_transport_pool_for_request`), never a refactor of the existing diagnostic probe; reply is sent as a deliberately-leaked raw `TCHAR` buffer (never an `RC::Unreal::FString`) via `ProcessEvent`, applying the same leak-and-never-destruct rationale as `resolve_transport_item_name()` (§5) to the outbound direction. Closes §8 item 1 code-wise; §8 items 2-3 remain. |
| 4D.9g | Accepted production implementation. Replaced the diagnostic-only 4D.9a-4D.9f probes (and their arm-file gating, attempt-flags, and per-tick leak/log globals) with a permanent `resolve_transport_item_name()` leak-and-cache helper, deduplicated by raw `TransportItemNameKey` bytes. Wired into the transport pool's `TRANSPORT_POOL_ITEM` log line, which now reports the resolved item name alongside the raw hex key. Closes §8 steps 1 and 5 from the prior entry. |
| 4D.9f | Accepted production diagnostic. Repeating per-tick leak-and-read probe deployed to production, monitored 2+ continuous hours in one uninterrupted boot (~121,000 leak/read cycles), zero crashes. RSS delta grew ~1.45GB→~1.57GB over the window (~60MB/hour, roughly linear), fully absorbed by the existing daily restart. Confirms leak-and-cache is production-safe at sustained worst-case frequency (§5, §8). |
| 4D.9e | Accepted diagnostic, conclusive. Reads character data (`RC::to_string()`) off the same leaked/undestructed result as 4D.9d, still never destructing it. `RESULT=PASS` — closes the gap 4D.9d left open; character-data reads are safe, not just `.size()` (§5). |
| 4D.9d | Accepted diagnostic, conclusive, supersedes 4D.9c's "dead end" framing. Same pool-sourced `ToString()` call as 4D.9c, but the result is placement-constructed into a static buffer and its destructor is deliberately never run. Server ran 283+ further ticks with zero crashes. Proves the corruption is in the result's destructor/deallocation path, not in `ToString()` itself — leak-and-cache is now a viable production path (§5, §8). |
| 4D.9c | Accepted diagnostic. `ToString()` call with no `RC::to_string()` conversion, result destructed normally, still crashes identically — ruled out the mod-side conversion helper as the cause (§5). |
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

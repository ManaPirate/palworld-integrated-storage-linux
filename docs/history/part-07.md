### Documentation defect

The Stage 4d.5b controlled-negative checkpoint was in fact committed and pushed.
Like Stage 4d.4r before it, that checkpoint changed only `src/linux/main.cpp`
and again failed to advance `docs/linux-port-status.md`.

The repository source history is coherent; the engineering runsheet is the stale
component. This documentation-repair checkpoint exists to bring the runsheet
forward to the already-pushed `bd19c4e4...` state before Stage 4d.6 begins.

---

## 50. Project health audit — semantic scope correction

### Why this audit was required

The project had accumulated substantial runtime archaeology after the Linux port
had already demonstrated that:

1. the native `.so` loads and executes through NullPrism on Linux;
2. the populated world can be discovered and planned correctly; and
3. the Linux implementation can invoke the same reflected server registration
   function recorded in the upstream Windows implementation.

The concern is therefore not presently "how do we replace the Windows DLL with
a Linux `.so`?" That native entry path is already operational.

### Identified engineering risk

The later investigation established a strong and useful membership oracle:

```text
PalItemContainerManager.GetGroupIdByItemContainerId
```

The engineering process then drifted into treating this condition as required:

```text
foreign chest must transition
zero Guid -> selected guild Guid
```

That requirement is stronger than the upstream server-side evidence currently
recorded by this runsheet.

The upstream registration path recorded in Stage 4c.1 is:

```cpp
g_srvInjecting = true;

for each guild storage:
    for each same-guild chest from another camp:
        storage->OnAvailableConcreteModel_ServerInternal(chest);

g_srvInjecting = false;
```

No recorded upstream evidence says this call must mutate
`GetGroupIdByItemContainerId` membership.

Stage 4c.4j itself explicitly warned that an unchanged observed aggregate did
not prove registration had no effect. Stage 4c.4o later proved only that the
standalone upstream registration call did not change the selected manager
membership oracle.

The corrected interpretation is:

```text
Stage 4c.4o proves:
OnAvailableConcreteModel_ServerInternal(chest)
    does not independently create the observed manager-membership transition.

Stage 4c.4o does NOT prove:
OnAvailableConcreteModel_ServerInternal(chest)
    fails to provide the routing/availability behaviour Integrated Storage needs.
```

### Required recovery action

Do not continue hunting for a hidden guild-association primitive until the Linux
server path has been compared line-by-line with the upstream server path.

The next engineering decision must come from parity, not from another guessed
runtime callback.

---

---

## 51. Stage 4d.6 — server-side upstream parity audit

### Classification

```text
STATIC / READ-ONLY — ACCEPTED
```

Evidence archive SHA256:

```text
00b381468e6acf6efccafb352585480752204da7101145fad08171660e0277f4
```

Repository state captured by the audit:

```text
HEAD / origin:
3ddb8c60466b76a75db0eb879954049db64159a7

Working tree:
clean

Linux source SHA256:
7a63fd68448290aa9a3b6e78099e3948618c97198254ef9ff09860773b256092

Windows source SHA256:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c

Runsheet SHA256 before this Stage-4d.6 update:
70f6c4cecf90d518ed0fa7ce705e79be34d8ee419d5ec2c844c54a427b392679
```

The preserved Windows source matched `upstream/main` exactly.

### Accepted parity result

The Linux implementation already reproduces the upstream dedicated-server
discovery/planning semantics needed to create foreign registration pairs:

- every base camp, including empty camps;
- guild association through `GroupIdBelongTo`;
- storage discovery from `ModuleArray`;
- chest discovery;
- current chest camp through `GetBaseCampModelBelongTo`;
- same-guild grouping;
- own-camp exclusion;
- eight-second discovery/planning cadence;
- exact reflected
  `PalBaseCampModuleItemStorage.OnAvailableConcreteModel_ServerInternal(chest)`
  registration function and argument shape.

The populated-world Linux planner remains the mature 157-chest / 20-storage /
285-pair plan.

### Upstream dependency correction

The exact upstream server source contains zero references to:

```text
GetGroupIdByItemContainerId
GetItemContainerModule
OnReadyItemContainerGuildChest
OnUpdateItemContainerInGuildItemStorage
OnUpdateItemContainerModule
OnUpdateItemContainer
```

Those late discovery surfaces are not prerequisites in upstream's server
cross-registration implementation.

The unchanged manager-membership oracle therefore does not establish that the
upstream registration call is ineffective.

### `g_srvInjecting`

Upstream's `g_srvInjecting` variable is written around the reconcile registration
loop and reset on world change, but the current upstream source never reads or
branches on it. It is not an effective upstream behavioural dependency.

### Actual parity gap

The Linux source already replans every eight seconds, but normal runtime does not
execute the mature plan.

The only implemented mutation path is the Stage-4c.3 development helper
`run_controlled_single_registration`, which:

1. returns unless the `.stage4c3-arm` file exists; and
2. uses `g_single_registration_attempted` to permit at most one registration
   call for the process.

Upstream instead executes every foreign pair during every authority reconcile.

The actual missing server-side behaviour is therefore:

```text
full-plan registration execution
```

not a hidden membership/lifecycle callback.

---

---

## 52. Stage 4d.7a — arm-gated full-plan registration executor

### Classification

```text
MUTATING ISOLATED RUNTIME — FULL-PLAN EXECUTOR ACCEPTED
POST-PASS 180-SECOND STABILITY — HARNESS-INCOMPLETE
```

Candidate identity:

```text
Base HEAD:
15e5243dbed1b3cc4e8c7cf60d5a1eaa879d7b19

Version:
0.1.0-linux-stage4d.7a-arm-gated-full-plan-executor

Source SHA256:
e968bc43d01008808cae58bb7dd9258dc2db2278e5f5ffe017d3fb5349e267b9

Artifact SHA256:
2bbde02085d87d99acce5f0c3f7765e1e95ff6916e875dc25181883cca79c358

Build ID:
8515573a44f3e9acf92a990dbbf67c7b89c35424

Build script SHA256:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288
```

### Build acceptance

Stage 4d.7a replaced the old single-pair development executor with an
explicitly armed one-process-lifetime full-plan executor while preserving the
mature planner.

Static/build acceptance established:

```text
executor ProcessEvent source sites=1
whole-source ProcessEvent source sites=7

old single-registration executor=0
old single-registration globals=0
old .stage4c3-arm mutation gate=0
new .stage4d7a-arm gate=1

executor GetGroupIdByItemContainerId references=0
executor OnReadyItemContainerGuildChest references=0
executor OnUpdateItemContainerInGuildItemStorage references=0
executor OnUpdateItemContainerModule references=0
executor ItemContainerMap_InServer references=0
executor TFieldRange references=0
executor bulk PalItemContainer enumeration=0
```

The staged `BUILD-PROVENANCE.txt` was corrected after the existing build script
ran. It contains Stage 4d.7a scope and zero stale Stage 4d.2 markers.

### Full-plan runtime result

The isolated test explicitly created `.stage4d7a-arm` and executed one complete
mature registration pass.

Accepted telemetry:

```text
FULL_PLAN_REGISTER GATE=ARMED

FULL_PLAN_REGISTER START
run=1
planned=285
parms=8
offset=0
size=8

FULL_PLAN_REGISTER SUMMARY
planned=285
attempted=285
completed=285
blocked=0
exceptions=0
function_mismatches=0
guild_mismatches=0
camp_mismatches=0
storage_class_mismatches=0
game_thread=1
dedicated=1
metadata=1

FULL_PLAN_REGISTER RESULT=PASS
```

The planner in that execution run remained:

```text
guilds=8
active_guilds=7
chests=157
storages=20
pairs=285
own_camp=157

duplicate_chests=0
duplicate_storages=0
duplicate_pairs=0
chest_camp_conflicts=0
storage_camp_conflicts=0
chest_guild_conflicts=0
storage_guild_conflicts=0
null_camps=0
invalid_camps=0
missing_guild=0
zero_guild=0
without_storage=0
```

Therefore the Linux implementation has runtime proof that it can execute the
complete upstream-equivalent server registration plan.

### Stability-harness classification

The intended 180-second post-pass stability window is **not** accepted as
completed.

The original runtime harness checked container-internal `/proc/net/udp` for:

```text
decimal 18211
hex 4723
```

Docker configuration proves the actual mapping is:

```text
container:
8211/udp
hex 2013

host publication:
127.0.0.1:18211 -> container 8211/udp
```

The incorrect UDP assertion was reached only after the same stability-loop
iteration had already passed:

```text
container running=true
PalServer child nonnull
PalServer PID unchanged
```

The captured runtime log contained zero crash/fatal markers before the harness
performed its emergency stop.

Two subsequent zero-rerun collector defects were also identified and retained:

1. postmortem V1 incorrectly required `docker port` runtime output from the
   intentionally stopped restored container;
2. postmortem V2 depended unnecessarily on the old extracted runtime-script
   path.

Postmortem V3 bundled and SHA256-pinned the exact original runtime harness and
removed both defects without rerunning PalServer.

Final V3 result:

```text
EXECUTOR_CLASSIFICATION=ACCEPTED

FULL_PLAN_PLANNED=285
FULL_PLAN_ATTEMPTED=285
FULL_PLAN_COMPLETED=285
FULL_PLAN_BLOCKED=0
FULL_PLAN_EXCEPTIONS=0
FULL_PLAN_FUNCTION_MISMATCHES=0
FULL_PLAN_GUILD_MISMATCHES=0
FULL_PLAN_CAMP_MISMATCHES=0
FULL_PLAN_STORAGE_CLASS_MISMATCHES=0
FULL_PLAN_GAME_THREAD=1
FULL_PLAN_DEDICATED=1
FULL_PLAN_METADATA=1
FULL_PLAN_RESULT=PASS

POST_PASS_STABILITY_CLASSIFICATION=HARNESS_INCOMPLETE
CORRECT_CONTAINER_INTERNAL_GAME_PORT=8211
HOST_PUBLISHED_GAME_PORT=18211
CRASH_FATAL_MARKERS_BEFORE_HARNESS_STOP=0

ISOLATED_RESTORED=1
PRODUCTION_UNCHANGED=1
DEVELOPMENT_CANDIDATE_UNCHANGED=1
POSTMORTEM_RESULT=PASS
```

Zero-rerun postmortem evidence archive SHA256:

```text
ba93cf299fccb1c0dce41690b86f56a4dd20a7fa11c69554be5f24f7553a9298
```

### Accepted conclusion

Stage 4d.7a proves:

```text
the Linux dedicated-server port can execute all 285 planned same-guild
foreign-camp registrations through the same reflected
PalBaseCampModuleItemStorage.OnAvailableConcreteModel_ServerInternal(chest)
operation used by upstream
```

It does not yet prove player-visible/native consume behaviour.

---

---

## 53. Stage 4d.7b — remote-client functional gate characterization

### Classification

```text
MUTATING ISOLATED RUNTIME — ACCEPTED OBSERVATION

REMOTE CLIENT OUTCOME:
CLIENT_BLOCKED

SERVER CONSUME PATH:
NOT EXERCISED

STABILITY:
ACCEPTED
```

Evidence archive SHA256:

```text
1ab94e147e650f02cb98fc1a6416355d9755eb902f2805df3693221f2c665560
```

Screenshot archive SHA256:

```text
cdb1f654795f64f5e4b94478ec9b6c6178e51cd3ef80aae13c60556ad8995e4b
```

Repository/source identity remained:

```text
HEAD / origin:
7761f2507ce08adb1c3635e224132de1c3fa388a

Linux source SHA256:
e968bc43d01008808cae58bb7dd9258dc2db2278e5f5ffe017d3fb5349e267b9

Artifact SHA256:
2bbde02085d87d99acce5f0c3f7765e1e95ff6916e875dc25181883cca79c358

Build ID:
8515573a44f3e9acf92a990dbbf67c7b89c35424

Windows source SHA256:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c

Runsheet SHA256 before this update:
74fed9ceca8658ab439c2455997d518228c16d9a0976b349fb2dc94125f43dae
```

### Correction to the operator-entered label

The finish harness recorded:

```text
FUNCTIONAL_OUTCOME=SERVER_FAILED
```

because that value was entered manually.

The supporting observation says:

```text
CLIENT_UI_RESULT=showed the materials as not available
SERVER_ACTION_RESULT=nothing, couldn't execute due to not having materials
```

Under the Stage 4d.7b test definitions, that is not `SERVER_FAILED`.

The accepted evidence-based classification is:

```text
CLIENT_BLOCKED
```

because the client refused the action before an authoritative server build/craft
request could exercise cross-camp consumption.

The original manual `SERVER_FAILED` label is retained in the evidence archive as
historical raw input and must not be silently rewritten.

### Screenshot result

The screenshot archive characterized four same-guild camps:

```text
Camp A
main base=yes
literal Guild Chest present=yes
existed before server boot=yes

Camp B
main base=no
literal Guild Chest present=no
existed before server boot=yes

Camp C
main base=no
literal Guild Chest present=yes
existed before server boot=yes

Camp D
main base=no
literal Guild Chest present=no
existed before server boot=no
```

The client build menu showed a strong local-visibility split.

At Camps A and C, which had a literal Guild Chest present, multiple material
counts were visible and multiple tested recipes had those materials available.

At Camp B, which existed before boot but had no literal Guild Chest, the client
showed several tested materials as unavailable and refused the build/craft
action.

At Camp D, which had no literal Guild Chest and was created after boot, the
client likewise showed the tested materials unavailable.

This is direct remote-client gate evidence. It is not proof that the server
would reject a valid request because no valid request reached the server.

### Upstream parity interpretation

Current upstream documentation explicitly separates the roles:

```text
dedicated server:
authoritative cross-registration and consumption

remote client:
display + client-side craft/build gate

transport:
server sends the remote client's far-camp material pool
```

It also explicitly states that without the remote-client display/gate path the
local menu may refuse even though server-authoritative consumption itself does
not depend on that client detour layer.

Therefore the Stage 4d.7b result is consistent with the intentionally incomplete
remote-client half of the current Linux-server integration.

### Full-plan executor remained accepted

The test started from the accepted Stage 4d.7a source and again produced exactly
one clean full-plan execution:

```text
planned=285
attempted=285
completed=285
blocked=0
exceptions=0
function_mismatches=0
guild_mismatches=0
camp_mismatches=0
storage_class_mismatches=0
game_thread=1
dedicated=1
metadata=1
FULL_PLAN_RESULT=PASS
```

No executor failure occurred.

### Stage 4d.7a stability caveat closed by later evidence

Before restore, the finish harness reported:

```text
seconds since full-plan PASS=2265
isolated PalServer PID=244
expected isolated PID=244
correct container-internal UDP 8211=PASS
crash/fatal markers=0
```

The 2,265-second live interval is more than twelve times the previously intended
180-second window.

Therefore the older Stage 4d.7a classification:

```text
POST-PASS 180-SECOND STABILITY — HARNESS-INCOMPLETE
```

is superseded by later accepted runtime evidence:

```text
POST-PASS STABILITY — ACCEPTED
minimum observed interval=2265 seconds
same PalServer PID
internal UDP 8211 available
zero crash/fatal markers
```

### Live camp topology change

The session also produced a useful reconciliation observation.

Initial accepted execution:

```text
run=1
guilds=8
storages=20
pairs=285
```

During the live session the planner later observed:

```text
guild 20f979c33446e7f1f8cea19499aad71a
storages=4

global:
storages=21
pairs=307
```

The screenshot notes independently record that Camp D did not exist before
server boot.

This demonstrates that the mature periodic planner can discover a new same-guild
camp after startup.

The Stage 4d.7a executor is intentionally one-shot, so the new 307-pair plan was
not executed. Camp D therefore cannot be used as evidence against the accepted
startup 285-pair executor.

It does provide direct evidence for the final production requirement:

```text
registration execution must reconcile periodically,
not only once at process startup
```

### Restoration and continuity

The finish harness accepted:

```text
isolated mod restored=PASS
isolated Level.sav restored=PASS
player saves=19
production PID unchanged=PASS
production StartedAt unchanged=PASS
production RestartCount unchanged=PASS
repository unchanged=PASS
```

---

---

## 54. Stage 4d.8 — remote-client transport parity audit

### Classification

```text
STATIC / READ-ONLY — ACCEPTED
```

Evidence archive SHA256:

```text
ba53de5e9e8f05ab7b29c2c4b9f5518cf5cc7dfa06491e3764199d697e795488
```

Accepted repository identity:

```text
HEAD / origin:
6374210fe35bc17b50c2e87bbaecebbe4fcdd769

working tree:
clean

Linux source SHA256:
e968bc43d01008808cae58bb7dd9258dc2db2278e5f5ffe017d3fb5349e267b9

runsheet SHA256:
15ba4621a265cd34f90fa901dc2cfa95af94fc3636dd280b4c2a2e167d54681b

Windows source SHA256:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c

build script SHA256:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288
```

The preserved Windows source remained byte-identical to `upstream/main`:

```text
upstream src/dllmain.cpp SHA256:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c

live preserved src/dllmain.cpp SHA256:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c
```

### Exact upstream request/reply transport

Upstream uses the client-owned `PalPlayerController` as the network carrier.

Request:

```text
function:
PalPlayerController.Debug_CheatCommand_ToServer(FString)

sentinel:
ISREQ|

payload:
ISREQ|<32-hex current camp GUID>
```

Reply:

```text
function:
PalPlayerController.Debug_ReceiveCheatCommand_ToClient(FString)

sentinel:
IS1|

payload:
IS1|item:count,item:count,...
```

The server receives the request on the requesting controller and sends the reply
back through the same controller.

The current Linux source contains zero occurrences of:

```text
ISREQ|
IS1|
Debug_CheatCommand_ToServer
Debug_ReceiveCheatCommand_ToClient
srvCampById
srvBuildForCamp
g_instToCamp
```

Therefore the remote-client transport channel is a real remaining parity gap.

### Upstream camp resolution

The request includes the client's current camp GUID.

Upstream resolves it by:

```text
FindAllOf(PalBaseCampModel)
compare PalBaseCampModel.ID against client-supplied 16-byte GUID
```

Current upstream reads `PalBaseCampModel.ID` at raw offset `0x58`.

For the Linux implementation, that raw offset is not accepted merely because
upstream uses it. The next runtime probe must determine whether the exact `ID`
property can be resolved reflectively and copied as a 16-byte `FGuid`.


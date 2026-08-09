### Conclusion

The access value is a real game-produced shared access proxy/interface backed
by a UObject that is not the physical chest.

---

## 37. Stage 4c.4u — exact ready-callback parameter assembly

Stage 4c.4u proved the ready callback's exact 16-byte argument can be assembled
without constructing or guessing an interface value.

The probe:

1. invoked `GetItemChestContainerAccess` exactly once;
2. revalidated the exact callback metadata;
3. copied the game-returned 16-byte interface value verbatim into the callback
   argument buffer;
4. did **not** invoke the callback.

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4u-ready-callback-assembly

Source SHA256:
a088b12b2be80e37dbe457f5597e286a93da22c1a7f28382ca6a01f2dcc2c0ca

Build script SHA256:
a34d1036bfbf7443a42499a4b0dc4656fb055db4da88006a1e7b735481f1d24b

Artifact SHA256:
aa726a014d1401703d9467b5349d22800409492af9156648786649b8cc945b67

Build ID:
e50ee48a221f956c24a530376b060eebfba7485b
```

Evidence archive SHA256:

```text
c110b6ceca9446283abd405914ede80a553234dcf274f34c484111067bae2dc6
```

Result:

```text
getter_calls=1
callback_calls=0
registration_calls=0
argument_assembly=exact
180-second stability=PASS
```

---

## 38. Stage 4c.4v — ready callback standalone negative

Stage 4c.4v invoked exactly once:

```text
OnReadyItemContainerGuildChest(
    exact game-returned PalMapObjectItemContainerAccessInterface
)
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4v-ready-callback-membership-transition

Source SHA256:
a76768f667eceabd0d31c7ea0e3f436e1195054eede2f5e1d6fd2b48fa6b510a

Build script SHA256:
97e8174c84bb667e2af924090f9954e7aaaffb6363f58bed7baa666df5aaf2e5

Artifact SHA256:
3d52d24528021283f3f7f2de1be7c0e152b6a34d69dab430eacbf29181f35148

Build ID:
f6411069d1b72ec755cfa650aaf66c44ba828ebd
```

Evidence archive SHA256:

```text
346b83766b58b54c5f7ae04c08e46f4ed76492f344867b6aba2ebb376faf82e3
```

Controlled result:

```text
PRE:
PalContainerId = nonzero
membership = zero

callback_calls=1

POST:
same PalContainerId=1
membership = zero

outcome=NO_TRANSITION
```

### Conclusion

`OnReadyItemContainerGuildChest` does not establish guild membership by
itself.

---

## 39. Stage 4c.4w — module-update callback standalone negative

Stage 4c.4w invoked exactly once:

```text
OnUpdateItemContainerModule(
    exact game-returned PalMapObjectItemContainerModule*
)
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4w-update-module-membership-transition

Source SHA256:
cf70117e9747b0d90756d779f8b58b7a99b0fd619bb428715ede2149bb8f544f

Build script SHA256:
769e45db1f592311323f911d91c05f7ba5acb467b6927955958867279a0cd658

Artifact SHA256:
fb181a1473fca9b19de9de011ae14aa992c947b012eae147c423697e94cd5405

Build ID:
2153b864f46078a1ef67fef18161ddbc4598bd9a
```

Evidence archive SHA256:

```text
d78db90fa9cf0a90cd86a742b6faa90d1e963ce758de59777832745d139236b9
```

Controlled result:

```text
same module=1
same PalContainerId=1
POST membership=zero

outcome=NO_TRANSITION
```

### Conclusion

`OnUpdateItemContainerModule` does not establish guild membership by itself.

---

## 40. Stage 4c.4x — container-update callback standalone negative

Stage 4c.4x resolved the selected chest's already-existing container through:

```text
PalItemContainerManager.GetContainer(PalContainerId)
```

and invoked exactly once:

```text
OnUpdateItemContainer(PalItemContainer*)
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4x-update-container-membership-transition

Source SHA256:
d6eab74f0ee5ca668dee9ca2ece95b2bdfd853e366cc91f0108b1e8f37f013df

Build script SHA256:
46a001732e6b238e5907001dec957f5fb18d474ea0e49166f0be2a5940513ed7

Artifact SHA256:
2edf7ba33a860f2cbcd6d2b2e348dc0e3c8c5f59cbaa5aba76ea8b9395741196

Build ID:
dc47e6475143c9992ea16ca5729c2f94abed29ca
```

Evidence archive SHA256:

```text
1d048eb2eac6509551c054881eebc1756e77fbf209b7db0b872436dce9acdad7
```

Controlled result:

```text
same_container=1
same PalContainerId=1
POST membership=zero

outcome=NO_TRANSITION
```

The 180-second stability window passed. No nonempty crash evidence was created
and the isolated world restored exactly.

### Conclusion

`OnUpdateItemContainer(PalItemContainer*)` does not establish guild membership
by itself.

---

## 41. Consolidated Stage 4c conclusions

Stage 4c began with a one-shot target-storage call and no semantic membership
oracle.

It ended with an exact chest/container/membership model.

### Proven physical identity chain

```text
physical chest
  -> GetItemContainerModule
  -> PalMapObjectItemContainerModule*
  -> GetContainerId
  -> PalContainerId
  -> PalItemContainerManager.GetContainer
  -> existing PalItemContainer*
```

### Proven membership chain

```text
PalItemContainerManager.GetGroupIdByItemContainerId(
    object,
    PalContainerId
)
```

Known semantics:

```text
unregistered physical chest
  -> zero Guid

registered selected-guild storage
  -> selected guild Guid
```

Therefore:

```text
container existence and guild membership are separate manager layers.
```

The physical chest's container already exists. The unresolved operation is the
guild association.

### Standalone target-storage callback results

```text
OnAvailableConcreteModel_ServerInternal(chest)
  -> no membership transition

OnReadyItemContainerGuildChest(interface)
  -> NO_TRANSITION

OnUpdateItemContainerModule(module*)
  -> NO_TRANSITION

OnUpdateItemContainer(PalItemContainer*)
  -> NO_TRANSITION
```

The latter three used exact reflected parameter types and game-produced
arguments.

These callbacks are no longer valid standalone-registration hypotheses.

They may still be lifecycle/update notifications that expect association to
have been created elsewhere.

### Access-interface result

```text
GetItemContainerAccess
GetItemChestContainerAccess
```

both return the same coherent non-chest backing UObject/interface pair.

The ready-callback argument can be copied verbatim from the game-returned
16-byte interface value.

### Permanent safety exclusions

Do not reopen without a materially different rationale:

```text
direct ItemContainerMap_InServer / FMapProperty inspection
broad reflected graph / TFieldRange traversal
bulk PalItemContainer result processing
16 exhausted fixed property guesses
30 exhausted fixed accessor guesses
standalone ready/module/container update callbacks
```

For the bulk `PalItemContainer` route, the precise finding is:

```text
FindAllOf("PalItemContainer") returned 9,874 objects.
Allocator corruption occurred during subsequent bulk processing before the
first per-container record.
```

The processing strategy is blocked. The initial query alone was not isolated
as the cause.

### Stage 4c final engineering question

The remaining problem is not:

```text
Which target-storage callback should be called?
```

It is:

```text
What operation creates PalContainerId -> GuildId membership?
```

---

## 42. Stage 4d.0 — bounded registration-lifecycle metadata discovery

Stage 4d.0 stopped guessing mutation callbacks.

It resolved the selected unregistered chest through the accepted read-only
module/`PalContainerId`/manager path, then searched twelve binary-derived
lifecycle names across:

```text
physical chest
target storage
item-container module
PalItemContainer
PalItemContainerManager
```

Result:

```text
targets=5
names=12
lookups=60
found=0
parameter_lines=0
candidate_calls=0
```

Evidence archive SHA256:

```text
6f274fb62cf9be7626c6d17843619205308b3a9532ad3113a89a701042f4311a
```

The 180-second stability window passed and the isolated state restored exactly.

---

## 43. Stage 4d.1 — access-owner lifecycle metadata discovery

Stage 4d.1 targeted the coherent backing UObject returned by
`GetItemChestContainerAccess`.

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4d.1-access-owner-lifecycle-metadata

Source SHA256:
9718e87ed41cc6e4796a42f04c9e8bc860cb0ff04ea64c6a4a05ee2c9123e88c

Build script SHA256:
65c03f728367e8c90be9f7e003eeabbf3972217de50c2b593752be6c84f57aba

Artifact SHA256:
2ad96fc6902eb2a7d5af9b1d1de00c0b309d78fed202a4c975e2927ec020c48e

Build ID:
3c02857ee2acf535a8474117ba175fdeec0b2572
```

Runtime result:

```text
GetItemChestContainerAccess calls=1

object_nonnull=1
interface_nonnull=1
coherent=1
object_is_chest=0

PalMapObjectItemChestModel=0
PalMapObjectItemStorageModel=0
PalMapObjectGuildChestModel=0
PalMapObjectGlobalPalStorageModel=0

classification=OTHER_EXACT_CLASS

lifecycle targets=1
lifecycle names=12
exact lookups=12
found=0
candidate_calls=0
```

Evidence archive SHA256:

```text
5e5fc3901e33e64dabc7ced580ea3bd6a150dc4794f5f1eb91669e18c0a93477
```

No new crash files were created. The isolated mod/save/player state restored
exactly and production remained unchanged.

---

## 44. Stage 4d.2 — access-owner native class identity

### Goal

Identify the exact runtime class of the coherent backing UObject returned by
`GetItemChestContainerAccess` without reopening the known Linux string-lifetime
hazard or any blocked broad-reflection route.

### Candidate identity

```text
Version:
0.1.0-linux-stage4d.2-access-owner-native-class-identity

Repository HEAD:
309d36452f1e9f7df25c78173989d08c37d9e2cd

Source SHA256:
f0c83cfb73711c5fa3d98c4b435cfa46e28f7a15da13f073e1eb4fc847068b19

Build script SHA256:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288

Artifact SHA256:
aeb061c77fea73b055e7a0d88fe7850977894292589d542e75e4284e5e24ed76

Build ID:
ce4320a4141c2cc53dbee0a88122cdd694061bd7
```

### Identification method

At build time, the live PalServer binary's native `UPal*` / `APal*` vtable
symbols were demangled and converted into exact reflected
`/Script/Pal.<Class>` lookups.

Generated candidate set:

```text
native_candidates=2005
vtable_native_class_names=2005
```

Runtime compared only `UClass*` pointers against the access-owner class and its
direct superclass.

No runtime call was made to:

```text
GetName
GetFullName
GetPathName
FName::ToString
```

The accepted ProcessEvent envelope remained:

```text
physical chest: 2
module:         1
manager:        2
target storage: 0
```

Lifecycle lookups and candidate calls remained zero.

### Runtime result

The selected physical chest remained:

```text
ContainerId:
30df4c2d00486b01c5daecae42017e27

membership:
00000000000000000000000000000000
```

The game-returned access interface remained coherent and nonnull.

Native class-resolution result:

```text
native_candidates=2005
lookups=2005
resolved=2003

class_matches=1
class=PalMapObjectItemContainerModule

direct_super_matches=1
direct_super=PalMapObjectConcreteModelModuleBase

identity_resolution=NATIVE_EXACT_CLASS
```

Process-local FName comparison indexes:

```text
access-owner class:
292821

access-owner object:
292821

direct superclass:
287367
```

### Accepted interpretation

`GetItemChestContainerAccess` is backed by an exact native
`PalMapObjectItemContainerModule` UObject.

Its direct native superclass is:

```text
PalMapObjectConcreteModelModuleBase
```

The access owner is therefore not a separate guild-storage proxy class.

Stage 4d.2 does not yet prove that the returned access-owner pointer is
pointer-identical to the module pointer returned by `GetItemContainerModule`;
that becomes the first comparison in Stage 4d.3.

### Stability and restoration

The identity probe completed exactly once.

```text
runtime name conversion=0
lifecycle lookups=0
candidate calls=0
allocator/fatal markers=0
```

The isolated server remained stable for 180 seconds.

One new UE4SS crash filename appeared:

```text
crash_111.log
```

It was zero bytes and contained no fatal marker.

The mature planner remained:

```text
guilds=8
active_guilds=7
chests=157
storages=20
pairs=285
own_camp=157
```

with all duplicate, conflict, null, invalid, missing, zero-guild, and
without-storage counters equal to zero.

The naturally running `Level.sav` changed during the test, then the isolated
state restored exactly:

```text
Restored mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72

Restored Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Restored player saves:
19
```

Production continuity remained:

```text
PID:
83

StartedAt:
2026-08-08T18:30:02.661576192Z
```

Evidence archive SHA256:

```text
5afabb231cb198ffe00fc70c34f7b14725caa873d3596751b435598348920b21
```

---

## 45. Stage 4d.3 — guild-storage anchor source survey

### Classification

```text
READ_ONLY / STATIC-OFFLINE SURVEY — ACCEPTED
```

Stage 4d.3 performed no source mutation, build, isolated server start,
`ProcessEvent`, save mutation, or production deployment.

It surveyed the accepted Stage 4d.2 source plus the PalServer binary identity and
found relevant native classes / strings including:

```text
UPalGuildItemStorage
UPalMapObjectGuildChestModel
UPalMapObjectItemContainerModule
UPalMapObjectItemContainerAccessInterface
OnUpdateItemContainerInGuildItemStorage
GetItemContainer_ItemContainerAccessInterface
GetTargetContainerId
bIsGuildChestModule
bIsGuildChestContainer
OnReadyItemContainerGuildChest
GetItemChestContainerAccess
```

These were candidate machinery only. The stage did not establish reflected
ownership, runtime availability, or behaviour.

Evidence archive SHA256:

```text
49fd35437f15438ea3cdaece156fd4e560106db777c62c5926ac13d64a571524
```

---

## 46. Stage 4d.4 — offline ELF symbol ownership attempt

### Classification

```text
REJECTED / INCOMPLETE — DO NOT RERUN AS AN OWNERSHIP METHOD
```

The live PalServer ELF was stripped for the relevant method-symbol purpose.
Defined-symbol inspection produced no candidate method ownership, while dynamic
symbols exposed only relevant vtables.

The wrapper later failed because it assumed host `python3`, which is not
installed.

The zero method counts are therefore not absence proof. The route is retained as
a dead end, not as a semantic result.

Evidence archive SHA256:

```text
4f3b1521f8c2a963c0852983d7b6f8d6868b17f40e9490acd1ed1898cc1d7961
```

---

## 47. Stage 4d.4r — exact runtime reflection-owner matrix

### Classification

```text
READ_ONLY RUNTIME — ACCEPTED / COMMITTED / PUSHED
```

Accepted checkpoint:

```text
Commit:
8c072462bb16740c6449ff0ab43072a6a2c57471

Commit message:
Stage 4d.4r: map guild storage reflection owners

Source SHA256:
6008693854db00309c2e4b5f4a1e24ae517fb6007a0985b16356da2014714ff6

Artifact SHA256:
def8cc8284bfe1365b154545b40dc3eab31c7c981c733472788ddeda09471280

Build ID:
a0ca0efed27d77c629554284b334293a525d8b33
```

The probe queried ten exact class owners against fourteen exact function names:

```text
class/function pairs=140
exact path lookups=280
runtime candidate calls=0
ProcessEvent calls in the new probe=0
```

Five exact reflected owners resolved:

```text
PalMapObjectGuildChestModel:OnUpdateItemContainerInGuildItemStorage
PalMapObjectItemContainerModule:GetContainerId
PalBaseCampModuleItemStorage:OnReadyItemContainerGuildChest
PalItemContainerManager:GetGroupIdByItemContainerId
PalMapObjectItemContainerAccessInterface:GetItemContainer_ItemContainerAccessInterface
```

The first runtime wrapper falsely flagged production PID continuity because
`pgrep -f 'PalServer-Linux-Shipping'` could match its own shell command. A
postmortem established that production did not restart and the runtime result was
accepted. Future harnesses use `[P]alServer-Linux-Shipping`.

Postmortem evidence SHA256:

```text
c18b6f5e9930ab34137f6ab02d2123c00f56e33026de3a7e5d1da39b75f8fd38
```

Checkpoint evidence SHA256:

```text
6a0e661bf0155a640fa88fb64bdedd3ad16c7dffeefc88f0599dabc17c266537
```

### Documentation defect

This accepted code checkpoint did not advance this engineering runsheet. That
broke the established checkpoint discipline and is the reason this repair is
required.

The staged build provenance prose also remained stale at Stage 4d.2 even though
the source/artifact identity was Stage 4d.4r.

---

## 48. Stage 4d.5 — live GuildChestModel anchor comparison

### Classification

```text
READ_ONLY RUNTIME — CONTROLLED INCOMPLETE / UNCOMMITTED
```

Candidate identity:

```text
Base HEAD:
8c072462bb16740c6449ff0ab43072a6a2c57471

Source SHA256:
0723e9fe9baaa9f8fa8aadf007df382ac5237476db27f9a303805a76e00df6a1

Artifact SHA256:
924c12b637e87d782b99166661c6b2539c307be6577a9d2d50256df20427ec5b

Build ID:
16ef33212c1522206a71162625244cd0c9d63e65
```

Runtime discovered:

```text
PalMapObjectGuildChestModel objects=13
nonnull=13
exact class=13
model exceptions=0
```

`OnUpdateItemContainerInGuildItemStorage` resolved with its exact one-object
input layout, and the input class was proven to be:

```text
PalGuildItemStorage*
```

All 13 GuildChestModels resolved `GetItemContainerModule` with the expected
reflected getter layout, but none returned an exact normal item-container module.
The first probe intentionally stopped there and returned `INCOMPLETE`.

This stage was not committed.

---

## 49. Stage 4d.5b — GuildChestModel null-module controlled negative

### Classification

```text
READ_ONLY RUNTIME — CONTROLLED NEGATIVE ACCEPTED / COMMITTED / PUSHED
```

Candidate identity:

```text
Base HEAD:
8c072462bb16740c6449ff0ab43072a6a2c57471

Source SHA256:
7a63fd68448290aa9a3b6e78099e3948618c97198254ef9ff09860773b256092

Artifact SHA256:
f6125e3247d8074045e51d387bf2f3ae260db7d68c25611186a482ac6ab5bd8b

Build ID:
0cd24fdb32b1d6563d86b713841e5370880d1c9c
```

The subtype follow-up disproved the Stage 4d.5 hypothesis that live
GuildChestModels were returning a subclass of the normal item-container module.

Postmortem result:

```text
objects=13
nonnull=13
exact_class=13
GetItemContainerModule function=13
GetItemContainerModule layout=13
returned module nonnull=0
module_is_a PalMapObjectItemContainerModule=0
complete normal container chains=0
model exceptions=0
candidate callback calls=0
runtime name conversion=0
crash/fatal lines=0
new crash directories=0
isolated restored=1
production unchanged=1
```

The class/superclass comparison indexes remained `-1/-1`; the probe source only
assigns those indexes when the returned module pointer is nonnull. Together with
the zero downstream getter activity, the accepted interpretation is:

```text
13/13 live PalMapObjectGuildChestModel
    GetItemContainerModule()
        -> nullptr
```

Therefore the ordinary physical-chest
`GetItemContainerModule -> GetContainerId -> manager membership` chain is not a
GuildChestModel architecture.

Postmortem evidence archive SHA256:

```text
582fadfa947eeae62b874c1d0bf1fb3a44ea1c568309fa74087f99aa72ed0add
```

Accepted checkpoint:

```text
Commit:
bd19c4e4adcde9e37df262027eefac6d02b7ac57

Parent:
8c072462bb16740c6449ff0ab43072a6a2c57471

Commit message:
Stage 4d.5b: characterize GuildChest null module route

Files changed:
src/linux/main.cpp
```


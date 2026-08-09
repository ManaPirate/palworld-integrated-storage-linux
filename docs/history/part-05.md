### Survey status

Inspection-only survey completed on:

```text
def2cf9a754ec40d815b1a68f138089bca0c9fa3
```

No source, staged package, runtime, save, or production state was
modified.

### TFieldIterator result

`TFieldIterator<FProperty>` supports the required name-free traversal:

```text
constructor:
TFieldIterator(
    UStruct*,
    EFieldIterationFlags
)

validity:
explicit operator bool()

advance:
operator++()

dereference:
operator*()
operator->()
```

Using `EFieldIterationFlags::None` restricts traversal to direct fields
without inherited, deprecated, or interface fields.

### Safe metadata surface

Each ordinal property can be inspected through:

```text
GetOffset_Internal()
GetSize()
GetElementSize()
GetArrayDim()
IsInContainer()
CastField<...>()
FStructProperty::GetStruct()
UStruct::GetPropertiesSize()
```

No field name is required.

### Name-conversion boundary

`FField::GetName()` and `FFieldClassVariant::GetName()` internally call
`FName::ToString`.

The runtime probe must therefore avoid:

```text
GetName()
GetFName().ToString()
field-name enumeration
```

### Identity-type evidence

The server binary exposes native struct-ops for:

```text
FGuid
FPalContainerId
FPalItemSlotId
FPalItemId
FPalDynamicItemId
```

The survey did not surface a literal reflected script path for `FGuid`.
The runtime probe may test `/Script/CoreUObject.Guid` as an optional
pointer-identity candidate, but acceptance must not depend on that path
resolving.

### Accepted interpretation

An ordinal-only runtime probe is safe and appropriate.

It should map:

1. the 16-byte structure nested at `PalContainerId.Id`;
2. the 32-byte `PalDynamicItemId`;
3. `PalItemSlotId` as a control layout;
4. `PalContainerId` and `PalItemId` as outer controls.

For each direct field, log only ordinal, offset, size, element size,
array dimension, property kind, nested struct size, and optional
known-type pointer matches.

Do not log names and do not read field values yet.

---

## 24. Stage 4c.4h — ordinal nested-field identity layout

### Goal

Map every remaining nested identity component by ordinal without
converting field names or reading values.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4h-ordinal-identity-layout

Source SHA256:
2b7bf6862276d0d38e7f531f0dc92ac7cdd98a77716ee20160ade33f5f30fcaa

Artifact SHA256:
4c7e4d233833e946b80f6084dee450666a5311c21347fd0607d99bd8f731b67c

Build ID:
339ebbfd295f9be5db18bed9dbad739a759df755
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4h-ordinal-layout-20260806-134213
```

```text
ORDINAL_LAYOUT PASS:       1
ORDINAL_LAYOUT INCOMPLETE: 0
ORDINAL_LAYOUT EXCEPTION:  0
Registration called:       0
Gate disabled:             1
Invalid thread:            0
Crash markers:             0
```

### Proven layouts

```text
PalContainerId
└─ FGuid, offset 0, size 16
   ├─ numeric, offset 0,  size 4
   ├─ numeric, offset 4,  size 4
   ├─ numeric, offset 8,  size 4
   └─ numeric, offset 12, size 4

PalItemSlotId
├─ PalContainerId, offset 0,  size 16
└─ numeric index, offset 16, size 4

PalItemId
├─ FName, offset 0, size 8
└─ PalDynamicItemId, offset 8, size 32
   ├─ FGuid, offset 0,  size 16
   └─ FGuid, offset 16, size 16
```

All fields were within bounds and all known structure pointers matched.

### Accepted interpretation

A complete same-process semantic fingerprint can safely hash:

1. slot index;
2. the exact 16-byte `PalContainerId`;
3. the exact 40-byte `PalItemId`; and
4. the exact 4-byte numeric `StackCount`.

These exact structures contain no unknown padding. The embedded
`FName` still means the fingerprint must not be claimed stable across
server restarts.

The isolated environment was restored exactly and production remained
unchanged.

---
## 25. Stage 4c.4i — complete semantic fingerprint repeatability

### Goal

Prove that the complete slot identity-and-stack fingerprint established
by Stage 4c.4h is repeatable across multiple snapshots in one idle server
process.

The probe remained read-only and unarmed. It added no `ProcessEvent`
call, invoked no reflected function, and performed no registration.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4i-semantic-repeatability

Source SHA256:
dd643713610ee6df4cc2edc8885b39aea1dd669fd6ace15622e7d1c24e81bc09

Artifact SHA256:
d1a813ae83faaf7b8d57d8ae7179e8c79f288f4c77b6cad9b74b02324fa66024

Build ID:
beb86f657164492a8b0fdd4b191261ad35620a3d
```

### Initial build failure and correction

The first generated source attempted to pass a ternary-selected marker
to:

```cpp
emit_marker(const char (&message)[Size])
```

The conditional expression decayed the two string literals to
`const char*`, so Clang could not bind the result to the literal-only
array-reference overload.

The accepted correction kept `emit_marker` unchanged and replaced the
ternary with explicit `if` and `else` branches, preserving compile-time
character arrays at both call sites.

The failed build automatically restored the committed Stage 4c.4h source
and staged package. Because the Unraid host does not provide `python3`,
the generator correction was applied through the development container.

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4i-semantic-repeatability-20260806-141404
```

All three snapshots were complete and identical:

| Sample | Valid | Slots | Non-null | Fully read | Exceptions | Fingerprint |
|---:|---:|---:|---:|---:|---:|---|
| 0 | 1 | 54 | 54 | 54 | 0 | `27db2634ac8df4c6` |
| 1 | 1 | 54 | 54 | 54 | 0 | `27db2634ac8df4c6` |
| 2 | 1 | 54 | 54 | 54 | 0 | `27db2634ac8df4c6` |

Exact hashed component coverage per snapshot:

```text
PalContainerId bytes:
864

PalItemId bytes:
2160

StackCount bytes:
216
```

These totals correspond exactly to:

```text
54 × 16-byte PalContainerId
54 × 40-byte PalItemId
54 × 4-byte StackCount
```

Repeatability result:

```text
SEMANTIC_REPEATABILITY PASS:       1
SEMANTIC_REPEATABILITY INCOMPLETE: 0
SEMANTIC_REPEATABILITY EXCEPTION:  0
Semantic samples:                  3
Matching samples:                  3
Registration called:               0
Gate disabled:                     1
Invalid thread:                    0
Crash markers:                     0
Intentional stop exit code:        143
```

### Restoration and production isolation

The isolated environment was restored exactly:

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

### Accepted interpretation

The selected guild aggregate has a stable complete semantic baseline
within one idle server process:

```text
slot_count=54
nonnull_slots=54
fully_read_slots=54
fingerprint=27db2634ac8df4c6
```

The baseline is suitable for controlled before/after comparison around
one registration call.

The embedded `FName` remains process-local. Stage 4c.4i therefore makes
no cross-restart stability or persistence claim.

---
## 26. Stage 4c.4j — controlled semantic before/after registration observation

### Goal

Determine whether the existing validated one-shot registration call changes
the selected guild aggregate's complete slot identity-and-stack state.

The candidate reused the existing `.stage4c3-arm` gate and the sole
`target_storage->ProcessEvent(...)` call site. It added no new
`ProcessEvent` call.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4j-semantic-observation

Source SHA256:
45ba3f973c9a55056ac4ba4c259eedda08b38ce54354e8ebcf257fc1a023bb89

Artifact SHA256:
063b010391e7af1d2a62aa0931646eafbdff8cdb809fbc14e501e9530d191982

Build ID:
b9ac1d68efbb3d70fffbeffbcf2621f6e8269347
```

The normal staged package remained unarmed.

### Observation design

The armed isolated run captured:

1. One complete semantic baseline before registration.
2. One immediate snapshot after the call returned.
3. Three delayed snapshots at 5, 10, and 15 seconds.
4. A 180-second post-observation stability window.

Every snapshot covered the exact validated representation of all 54 slots:

```text
54 × 16-byte PalContainerId
54 × 40-byte PalItemId
54 × 4-byte StackCount
```

The probe classified results as `CHANGED`, `UNCHANGED`, `INCOMPLETE`, or
`EXCEPTION`.

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4j-semantic-observation-20260806-161619
```

Exactly one ready registration call completed:

```text
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

One earlier pre-readiness scan returned `RESULT=BLOCKED`. It had
`plan=0`, performed no `ProcessEvent` call, and did not consume the later
ready registration.

All five snapshots were complete and identical:

| Phase | Delay | Slots | Non-null | Fully read | Exceptions | Fingerprint |
|---|---:|---:|---:|---:|---:|---|
| Baseline | 0 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Immediate | 0 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Delayed | 5 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Delayed | 10 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Delayed | 15 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |

Result:

```text
SEMANTIC_OBSERVATION RESULT=UNCHANGED
immediate_changed=0
delayed_changed=0
delayed_consistent=1
retained_change=0
cross_restart_stable=0
```

The server remained stable for 180 seconds after the observation. There were
no invalid-thread markers, registration exceptions, other probe exceptions,
crash markers, or new crash files.

### Restoration and production isolation

The isolated environment was restored exactly:

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

### Accepted interpretation

The controlled registration call produced no observable change to the
selected guild aggregate's slot membership, item identities, or stack counts
during the 15-second semantic observation window.

This does not prove that registration had no effect. The effect may instead
exist in container-manager membership, container-to-group ownership,
`BelongInfo`, routing, visibility, or another state surface outside the 54
aggregate slots.

The pre-restoration `Level.sav` hash changed while the server was running,
but that cannot be attributed specifically to registration. No persistence
claim is made.

Registration idempotency remains blocked.

---
## 27. Stage 4c.4k — exact container-membership observability selection

### Goal

Select one exact, semantic, read-only surface capable of reporting the group
associated with a validated `PalContainerId`.

### Accepted surface

Stage 4c.4k selected:

```text
UPalItemContainerManager.GetGroupIdByItemContainerId
```

Accepted reflected layout:

| Field | Accepted value |
|---|---:|
| `ParmsSize` | 40 bytes |
| Inputs | 2 |
| Returns | 1 |
| Object inputs | 1 |
| Struct inputs | 1 |
| Struct returns | 1 |
| First input | offset 0, size 8 |
| Second input | offset 8, size 16 |
| Return | offset 24, size 16 |

The second input is the exact 16-byte `PalContainerId`. The return is a
16-byte group identifier.

Stage 4c.4k selected this surface but did **not** invoke it.

### Rejected observation routes

`UPalItemContainerManager.ItemContainerMap_InServer` is permanently blocked as
a direct runtime observation path.

Three progressively narrower isolated manager-map probes all produced the same
`FMallocBinned2` allocator-corruption failure followed by signal 11:

1. full live-map iteration;
2. reflected map metadata/layout only;
3. key/value property-type access only.

No direct manager-map probe remains in the accepted source or artifact.

`UPalItemContainer.BelongInfo` was also rejected as the primary exact
membership oracle because it exposed a group identifier without a tested exact
container identifier.

Accepted Stage 4c.4k candidate identity:

```text
Source SHA256:
00bc9061532b67efaf260011e53464d2e757c119fdb16dd7e6f3d5985e14610d

Artifact SHA256:
0a74a9ea8ffb3e5bac6d48b75f0cc6fce17459d9429074ea02c10a818e312654

Build ID:
93e1a5b418478bbba66edd53e08bbf89177d29bb
```

Accepted runtime evidence directory:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/
integrated-storage-stage4c4k-query-selection-20260806-172555
```

The server remained stable for 180 seconds, the normal staged package remained
unarmed, the isolated mod/save/player state was restored, and production
remained unchanged.

---

## 28. Stage 4c.4l — physical chest to `PalContainerId` bridge

### Accepted result

Stage 4c.4l established the safe semantic identity bridge:

```text
physical chest
  -> GetItemContainerModule
  -> PalMapObjectItemContainerModule*
  -> GetContainerId
  -> exact nonzero PalContainerId
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4l-module-container-id-accessor

Source SHA256:
b43c0d658d692a31c6063316ca51e8259b0d829c717e34f4169589c51d23e838

Build script SHA256:
1d2283f915e7283491b8110a5d0eeabd944c5c375273c705723be10f46dcc789

Artifact SHA256:
fff28a9da91709d654ea670055addbf8f4b71339576de75ae7d814cfdeff0b4d

Build ID:
5a61dad6ccbfe2b4a4f8892963396822e9a53e8d
```

The chest's `PalContainerId` is reacquired dynamically every runtime. Pointer
identity and a previously observed container ID are not reused across
restarts.

---

## 29. Stage 4c.4m — first selected membership-query observation

Using the accepted module-to-`PalContainerId` bridge, the exact selected query
was invoked for the known unregistered physical chest.

Result:

```text
selected physical chest:
PalContainerId = nonzero

membership return:
00000000000000000000000000000000
```

This was the first semantic evidence that the exact zero Guid represents the
absence of group membership for that container.

A positive registered control was still required before accepting that
interpretation.

---

## 30. Stage 4c.4n — negative/positive membership control

The exact query was tested against both known states:

```text
known unregistered selected physical chest
  -> zero Guid
```

```text
known registered selected-guild storage container
  -> 20f979c33446e7f1f8cea19499aad71a
```

Exact layout:

```text
GetGroupIdByItemContainerId
ParmsSize=40

object input:
offset=0
size=8

PalContainerId input:
offset=8
size=16

Guid return:
offset=24
size=16
```

### Accepted semantic conclusion

The exact zero Guid is an **absence sentinel only**.

It must not be treated as a valid semantic guild ID.

This stage established the safe before/after membership oracle used by the
later controlled callback experiments.

---

## 31. Stage 4c.4o — `OnAvailableConcreteModel_ServerInternal` standalone negative

The historically upstream-looking target-storage callback was tested once
against the selected unregistered chest:

```text
target_storage->OnAvailableConcreteModel_ServerInternal(chest)
```

Precondition:

```text
membership = zero
```

Postcondition:

```text
same PalContainerId
membership = zero
target-storage semantic fingerprint = unchanged
```

The server remained stable.

### Conclusion

```text
OnAvailableConcreteModel_ServerInternal(chest)
is not a standalone guild-association primitive.
```

Do not repeat it blindly.

---

## 32. Stage 4c.4p — bounded exact-name function metadata survey

A bounded metadata-only survey inspected 19 exact candidate names across five
already-known objects.

Important target-storage functions found:

```text
OnAvailableConcreteModel_ServerInternal
  ParmsSize=8
  object input

OnReadyItemContainerGuildChest
  ParmsSize=16
  one input

OnUpdateItemContainerModule
  ParmsSize=8
  object input

OnUpdateItemContainer
  ParmsSize=8
  object input
```

Important manager functions confirmed:

```text
TryGetContainer
GetContainer
GetGroupIdByItemContainerId
```

No broad reflected graph traversal was required.

---

## 33. Stage 4c.4q — exact parameter identities

Stage 4c.4q classified the important candidate parameters exactly.

```text
OnReadyItemContainerGuildChest
ParmsSize=16
input:
  FInterfaceProperty
  exact interface:
  /Script/Pal.PalMapObjectItemContainerAccessInterface
```

```text
OnUpdateItemContainerModule
ParmsSize=8
input:
  exact PalMapObjectItemContainerModule*
```

```text
OnUpdateItemContainer
ParmsSize=8
input:
  exact PalItemContainer*
```

```text
GetContainer
input:
  PalContainerId at offset 0, size 16
return:
  PalItemContainer* at offset 16, size 8
```

```text
TryGetContainer
input:
  PalContainerId at offset 0, size 16
out:
  PalItemContainer* at offset 16
return:
  bool at offset 24
```

This removed generic `UObject*` guessing from the remaining manager and
target-storage experiments.

---

## 34. Stage 4c.4r — manager resolution proves existence != membership

The selected unregistered physical chest was resolved through both exact
manager paths:

```text
GetContainer(PalContainerId)
TryGetContainer(PalContainerId)
```

Both resolved the same existing nonnull `PalItemContainer` while:

```text
GetGroupIdByItemContainerId(...) = zero Guid
```

A registered selected-guild storage control resolved consistently to its
existing container.

### Major architectural conclusion

```text
container existence != guild membership
```

The target chest's `PalItemContainer` already exists.

The unresolved Integrated Storage operation is association of that existing
`PalContainerId` with the guild, not creation of the container itself.

---

## 35. Stage 4c.4s — exact access-interface getter signatures

The selected chest exposes:

```text
GetItemContainerAccess
GetItemChestContainerAccess
```

Both return an exact 16-byte `FInterfaceProperty` of:

```text
/Script/Pal.PalMapObjectItemContainerAccessInterface
```

No getter invocation was required for this stage.

---

## 36. Stage 4c.4t — access-interface getter runtime control

Both access getters were invoked exactly once.

Each returned a coherent nonnull interface pair:

```text
object_nonnull=1
interface_nonnull=1
coherent=1
object_is_chest=0
```

The two getters returned the same backing UObject and the same interface
pointer.

No registration or membership transition occurred.


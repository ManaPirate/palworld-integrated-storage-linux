# IntegratedStorageDiag

Throwaway, read-only Windows client-side UE4SS Lua mod. Not part of the
Linux server build, never shipped, kept here only for this
investigation's own record (same reasoning `src/dllmain.cpp` is kept:
see `docs/linux-port-status.md` §8).

## What it's for

Confirms or kills a specific hypothesis in the ongoing "problem 2"
investigation (`docs/V1.0.3_DIAGNOSTIC_PLAN.md`): that
`OnRep_ContainerInfos` on `PalBaseCampModuleItemStorage` never fires on
a connecting client after server-side cross-registration, i.e. the
server-side write (`OnAvailableConcreteModel_ServerInternal`, proven
correct — `ContainerInfos` genuinely gets real entries) never actually
*replicates* to the client that needs it. Found by enumerating the
class's own `UFunction` chain via reflection — zero RE tooling needed.

`OnRep_` functions only ever fire on remote proxies, never on the
authority itself, so nothing server-side can observe whether this
fires. It needs a real client.

## What it does

Hooks `OnRep_ContainerInfos` and `OnRep_GuildContainerInfo` on
`PalBaseCampModuleItemStorage` and logs (`[ISDIAG] ... FIRED
total=N`) every time either fires. Never touches the hook `Context` or
any parameters, never blocks or replaces the real handler, never
mutates game state. Safe to run standalone with no other mods.

## How to use it

1. Install UE4SS (Windows) for your Palworld client if not already
   present.
2. Drop this `IntegratedStorageDiag` folder into `Pal/Binaries/Win64/
   Mods/`.
3. Uninstall any other "Integrated Storage" client mod first — the
   whole point is testing against a vanilla client, matching how this
   project's v1.0.0 is designed to work.
4. Connect to a test server, enter/leave a base camp belonging to a
   guild with other camps (triggers the periodic cross-registration
   reconcile automatically).
5. Check the UE4SS console/log for `[ISDIAG]` lines.

If `OnRep_ContainerInfos FIRED` never appears despite confirmed
server-side registration (correlate against `FULL_PLAN_REGISTER
SUMMARY` timestamps in the server log), that confirms replication is
the actual break. If it does fire, this specific hypothesis is dead
and the bug is further downstream.

-- IntegratedStorageDiag: read-only diagnostic mod. Confirms or kills the
-- hypothesis that OnRep_ContainerInfos never fires on a connecting client
-- after server-side cross-registration (see palworld-integrated-storage-linux
-- problem 2 investigation, 20 Aug 2026).
--
-- Pure observation, zero gameplay side effects: these hooks never touch
-- Context/params, never block/replace the real handler, never mutate any
-- game state. Safe to run alongside anything else, including no other mods
-- at all -- in fact it should be tested with NO other Integrated Storage
-- client mod installed, to match how the Linux server (v1.0.0) is designed
-- to work with a fully vanilla client.

local containerInfosFires = 0
local guildContainerInfoFires = 0

local function log(msg)
    print(string.format("[ISDIAG] %s\n", msg))
end

RegisterHook(
    "/Script/Pal.PalBaseCampModuleItemStorage:OnRep_ContainerInfos",
    function(Context)
        containerInfosFires = containerInfosFires + 1
        log(string.format(
            "OnRep_ContainerInfos FIRED total=%d",
            containerInfosFires
        ))
    end
)

RegisterHook(
    "/Script/Pal.PalBaseCampModuleItemStorage:OnRep_GuildContainerInfo",
    function(Context)
        guildContainerInfoFires = guildContainerInfoFires + 1
        log(string.format(
            "OnRep_GuildContainerInfo FIRED total=%d",
            guildContainerInfoFires
        ))
    end
)

log("IntegratedStorageDiag loaded -- watching OnRep_ContainerInfos / OnRep_GuildContainerInfo on PalBaseCampModuleItemStorage")

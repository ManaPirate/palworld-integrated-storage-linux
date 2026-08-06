// Integrated Storage native Linux dedicated-server port.
//
// Stage 4a implements read-only discovery only:
//
// - Dedicated-server role detection
// - World-change detection and state reset
// - Base-camp enumeration
// - Guild grouping using copied 16-byte GUID keys
// - Base-camp storage-module discovery
// - Periodic diagnostic reporting
//
// Deliberately excluded:
//
// - Chest-map traversal
// - Container cross-registration
// - Material pooling or consumption
// - RPC transport
// - Hooks, detours and AOB scanning
// - Any mutation of Unreal objects or containers
//
// The upstream Windows implementation remains in ../dllmain.cpp and
// is intentionally unchanged.

#include <Unreal/UnrealInitializer.hpp>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <unistd.h>

#include <Mod/CppUserModBase.hpp>

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <algorithm>
#include <atomic>
#include <dlfcn.h>
#include <memory>
#include <thread>

namespace ModIntegratedStorageModulePin
{
    // Intentionally retained until process exit.
    //
    // NullPrism's CppMod wrapper owns the original dlopen handle
    // and can close it after uninstall_mod. This additional loader
    // reference keeps callback code mapped until PalServer exits.
    void* g_process_lifetime_pin{};

    auto pin_for_process_lifetime() noexcept -> bool
    {
        if (g_process_lifetime_pin != nullptr)
        {
            return true;
        }

        Dl_info module_info{};

        if (
            dladdr(
                static_cast<const void*>(
                    &g_process_lifetime_pin
                ),
                &module_info
            ) == 0 ||
            module_info.dli_fname == nullptr ||
            module_info.dli_fname[0] == '\0'
        )
        {
            return false;
        }

        g_process_lifetime_pin =
            dlopen(
                module_info.dli_fname,
                RTLD_NOW | RTLD_LOCAL
            );

        return g_process_lifetime_pin != nullptr;
    }
}


namespace
{
    using Clock = std::chrono::steady_clock;
    using GuildKey = std::array<std::uint8_t, 16>;

    constexpr auto WorldProbeInterval = std::chrono::seconds{1};
    constexpr auto DiscoveryInterval = std::chrono::seconds{8};

    struct RawTArray
    {
        std::uint8_t* data{};
        std::int32_t num{};
        std::int32_t max{};
    };

    struct GuildKeyHash
    {
        auto operator()(const GuildKey& key) const noexcept
            -> std::size_t
        {
            std::size_t hash =
                static_cast<std::size_t>(1469598103934665603ULL);

            for (const auto byte : key)
            {
                hash ^= static_cast<std::size_t>(byte);
                hash *= static_cast<std::size_t>(1099511628211ULL);
            }

            return hash;
        }
    };

    struct GuildDiscovery
    {
        std::size_t camp_count{};
        std::unordered_set<RC::Unreal::UObject*> storage_modules{};
    };


    struct RegistrationPlanGuild
    {
        std::unordered_map<
            RC::Unreal::UObject*,
            RC::Unreal::UObject*
        > chest_camps{};

        std::unordered_map<
            RC::Unreal::UObject*,
            RC::Unreal::UObject*
        > storage_camps{};
    };

    struct RegistrationPlanPair
    {
        RC::Unreal::UObject* chest{};
        RC::Unreal::UObject* storage{};

        auto operator==(
            const RegistrationPlanPair& other
        ) const noexcept -> bool
        {
            return
                chest == other.chest &&
                storage == other.storage;
        }
    };

    auto mix_plan_value(std::uint64_t value) noexcept
        -> std::uint64_t
    {
        value += 0x9e3779b97f4a7c15ULL;
        value =
            (value ^ (value >> 30)) *
            0xbf58476d1ce4e5b9ULL;
        value =
            (value ^ (value >> 27)) *
            0x94d049bb133111ebULL;

        return value ^ (value >> 31);
    }

    auto guild_plan_value(
        const GuildKey& guild_key
    ) noexcept -> std::uint64_t
    {
        std::uint64_t value =
            0xcbf29ce484222325ULL;

        for (const auto byte : guild_key)
        {
            value ^= byte;
            value *= 0x100000001b3ULL;
        }

        return mix_plan_value(value);
    }

    auto object_plan_value(
        RC::Unreal::UObject* object
    ) noexcept -> std::uint64_t
    {
        return mix_plan_value(
            static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(
                    object
                )
            )
        );
    }

    struct RegistrationPlanPairHash
    {
        auto operator()(
            const RegistrationPlanPair& pair
        ) const noexcept -> std::size_t
        {
            const auto chest =
                object_plan_value(pair.chest);

            const auto storage =
                object_plan_value(pair.storage);

            return static_cast<std::size_t>(
                chest ^
                (
                    storage +
                    0x9e3779b97f4a7c15ULL +
                    (chest << 6) +
                    (chest >> 2)
                )
            );
        }
    };

    auto registration_pair_fingerprint(
        const GuildKey& guild_key,
        const RegistrationPlanPair& pair
    ) noexcept -> std::uint64_t
    {
        const auto storage =
            object_plan_value(pair.storage);

        const auto rotated_storage =
            (storage << 1) |
            (storage >> 63);

        return mix_plan_value(
            guild_plan_value(guild_key) ^
            object_plan_value(pair.chest) ^
            rotated_storage
        );
    }

    RC::Unreal::UObject* g_pal_utility{};
    RC::Unreal::UObject* g_last_world{};

    std::atomic_int g_is_server{-1};
    std::atomic_int g_is_dedicated{-1};

    Clock::time_point g_last_world_probe{};
    Clock::time_point g_last_discovery{};

    std::uint64_t g_discovery_runs{};

    template <std::size_t Size>
    auto emit_marker(const char (&message)[Size]) noexcept -> void
    {
        static_assert(Size > 1);

        const auto ignored_message_result =
            ::write(STDERR_FILENO, message, Size - 1);

        const auto ignored_newline_result =
            ::write(STDERR_FILENO, "\n", 1);

        static_cast<void>(ignored_message_result);
        static_cast<void>(ignored_newline_result);
    }

    auto emit_format(const char* format, ...) noexcept -> void
    {
        std::va_list arguments;
        va_start(arguments, format);

        std::vfprintf(stderr, format, arguments);

        va_end(arguments);

        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    auto timepoint_is_empty(const Clock::time_point& value) noexcept
        -> bool
    {
        return value.time_since_epoch().count() == 0;
    }

    auto guid_is_zero(const GuildKey& key) noexcept -> bool
    {
        for (const auto byte : key)
        {
            if (byte != 0)
            {
                return false;
            }
        }

        return true;
    }

    auto guid_to_hex(const GuildKey& key) noexcept
        -> std::array<char, 33>
    {
        constexpr char HexDigits[] = "0123456789abcdef";

        std::array<char, 33> result{};

        for (std::size_t index = 0; index < key.size(); ++index)
        {
            result[index * 2] =
                HexDigits[(key[index] >> 4) & 0x0F];

            result[index * 2 + 1] =
                HexDigits[key[index] & 0x0F];
        }

        result[32] = '\0';

        return result;
    }

    auto class_is(
        RC::Unreal::UObject* object,
        RC::Unreal::UClass* expected_class
    ) -> bool
    {
        if (object == nullptr || expected_class == nullptr)
        {
            return false;
        }

        RC::Unreal::UStruct* current = object->GetClassPrivate();
        RC::Unreal::UStruct* expected = expected_class;

        for (
            std::size_t depth = 0;
            current != nullptr && depth < 24;
            ++depth
        )
        {
            if (current == expected)
            {
                return true;
            }

            current = current->GetSuperStruct();
        }

        return false;
    }

    auto get_storage_module_class()
        -> RC::Unreal::UClass*
    {
        static RC::Unreal::UClass* storage_module_class{};

        if (storage_module_class == nullptr)
        {
            storage_module_class = RC::Unreal::UObjectGlobals::StaticFindObject<
                RC::Unreal::UClass*>(
                nullptr,
                nullptr,
                STR("/Script/Pal.PalBaseCampModuleItemStorage")
            );
        }

        return storage_module_class;
    }

    auto copy_guild_key(
        RC::Unreal::UObject* camp,
        GuildKey& output
    ) -> bool
    {
        if (camp == nullptr)
        {
            return false;
        }

        const auto* property_value =
            camp->GetValuePtrByPropertyNameInChain(
                STR("GroupIdBelongTo")
            );

        if (property_value == nullptr)
        {
            return false;
        }

        std::memcpy(
            output.data(),
            property_value,
            output.size()
        );

        return true;
    }

    auto find_storage_module(RC::Unreal::UObject* camp)
        -> RC::Unreal::UObject*
    {
        if (camp == nullptr)
        {
            return nullptr;
        }

        auto* raw_modules =
            camp->GetValuePtrByPropertyNameInChain(
                STR("ModuleArray")
            );

        if (raw_modules == nullptr)
        {
            return nullptr;
        }

        const auto* modules =
            static_cast<const RawTArray*>(raw_modules);

        if (
            modules->data == nullptr ||
            modules->num <= 0 ||
            modules->num > 64 ||
            modules->max < modules->num
        )
        {
            return nullptr;
        }

        auto** module_objects =
            reinterpret_cast<RC::Unreal::UObject**>(
                modules->data
            );

        for (
            std::int32_t index = 0;
            index < modules->num;
            ++index
        )
        {
            auto* module = module_objects[index];

            if (
                class_is(
                    module,
                    get_storage_module_class()
                )
            )
            {
                return module;
            }
        }

        return nullptr;
    }


    template <typename Visitor>
    auto for_each_storage_module(
        RC::Unreal::UObject* camp,
        Visitor&& visitor
    ) -> std::size_t
    {
        if (camp == nullptr)
        {
            return 0;
        }

        auto* raw_modules =
            camp->GetValuePtrByPropertyNameInChain(
                STR("ModuleArray")
            );

        if (raw_modules == nullptr)
        {
            return 0;
        }

        const auto* modules =
            static_cast<const RawTArray*>(
                raw_modules
            );

        if (
            modules->data == nullptr ||
            modules->num <= 0 ||
            modules->num > 64 ||
            modules->max < modules->num
        )
        {
            return 0;
        }

        auto** module_objects =
            reinterpret_cast<
                RC::Unreal::UObject**
            >(modules->data);

        std::size_t count{};

        for (
            std::int32_t index = 0;
            index < modules->num;
            ++index
        )
        {
            auto* module =
                module_objects[index];

            if (
                !class_is(
                    module,
                    get_storage_module_class()
                )
            )
            {
                continue;
            }

            visitor(module);
            ++count;
        }

        return count;
    }

    auto call_pal_utility_bool(
        const RC::CharType* function_name,
        RC::Unreal::UObject* world_context
    ) -> int
    {
        if (
            function_name == nullptr ||
            world_context == nullptr
        )
        {
            return -1;
        }

        if (g_pal_utility == nullptr)
        {
            g_pal_utility =
                RC::Unreal::UObjectGlobals::StaticFindObject<
                    RC::Unreal::UObject*>(
                    nullptr,
                    nullptr,
                    STR("/Script/Pal.Default__PalUtility")
                );
        }

        if (g_pal_utility == nullptr)
        {
            return -1;
        }

        auto* function =
            g_pal_utility->GetFunctionByNameInChain(
                function_name
            );

        if (function == nullptr)
        {
            return -1;
        }

        struct Parameters
        {
            RC::Unreal::UObject* WorldContextObject{};
            bool ReturnValue{};
            std::uint8_t Padding[7]{};
        };

        Parameters parameters{};
        parameters.WorldContextObject = world_context;

        g_pal_utility->ProcessEvent(
            function,
            &parameters
        );

        return parameters.ReturnValue ? 1 : 0;
    }

    std::atomic_bool g_role_probe_requested{false};
    std::atomic_bool g_chest_association_requested{false};
    std::atomic_bool g_chest_association_running{false};
    std::atomic_bool g_chest_association_enabled{false};

    std::atomic_uint32_t g_engine_tick_entries{0};
    std::atomic_uint64_t g_chest_association_runs{0};

    RC::Unreal::Hook::GlobalCallbackId
        g_engine_tick_callback_id{
            RC::Unreal::Hook::ERROR_ID
        };

    class EngineTickEntryGuard final
    {
      public:
        EngineTickEntryGuard() noexcept
        {
            g_engine_tick_entries.fetch_add(
                1,
                std::memory_order_acq_rel
            );
        }

        ~EngineTickEntryGuard()
        {
            g_engine_tick_entries.fetch_sub(
                1,
                std::memory_order_acq_rel
            );
        }

        EngineTickEntryGuard(
            const EngineTickEntryGuard&
        ) = delete;

        auto operator=(
            const EngineTickEntryGuard&
        ) -> EngineTickEntryGuard& = delete;
    };

    class AssociationRunningGuard final
    {
      public:
        AssociationRunningGuard() = default;

        ~AssociationRunningGuard()
        {
            g_chest_association_running.store(
                false,
                std::memory_order_release
            );
        }

        AssociationRunningGuard(
            const AssociationRunningGuard&
        ) = delete;

        auto operator=(
            const AssociationRunningGuard&
        ) -> AssociationRunningGuard& = delete;
    };

    auto reset_world_state() noexcept -> void
    {
        g_is_server.store(
            -1,
            std::memory_order_release
        );

        g_is_dedicated.store(
            -1,
            std::memory_order_release
        );

        g_role_probe_requested.store(
            false,
            std::memory_order_release
        );

        g_last_discovery = Clock::time_point{};
        g_discovery_runs = 0;

        g_chest_association_requested.store(
            false,
            std::memory_order_release
        );

        emit_marker(
            "[ModIntegratedStorageCpp] WORLD state reset"
        );
    }

    auto observe_world(RC::Unreal::UObject* context) -> bool
    {
        if (context == nullptr)
        {
            return false;
        }

        auto* world = context->GetWorld();

        if (world == nullptr)
        {
            return false;
        }

        if (g_last_world == nullptr)
        {
            g_last_world = world;

            emit_marker(
                "[ModIntegratedStorageCpp] WORLD acquired"
            );

            return true;
        }

        if (world != g_last_world)
        {
            reset_world_state();
            g_last_world = world;

            emit_marker(
                "[ModIntegratedStorageCpp] WORLD changed"
            );
        }

        return true;
    }

    auto resolve_dedicated_role(
        RC::Unreal::UObject* context
    ) -> bool
    {
        const int cached_dedicated =
            g_is_dedicated.load(
                std::memory_order_acquire
            );

        if (cached_dedicated >= 0)
        {
            return cached_dedicated == 1;
        }

        const int dedicated_result =
            call_pal_utility_bool(
                STR("IsDedicatedServer"),
                context
            );

        if (dedicated_result < 0)
        {
            return false;
        }

        const int server_result =
            call_pal_utility_bool(
                STR("IsServer"),
                context
            );

        g_is_dedicated.store(
            dedicated_result,
            std::memory_order_release
        );

        g_is_server.store(
            server_result,
            std::memory_order_release
        );

        emit_format(
            "[ModIntegratedStorageCpp] "
            "ROLE server=%d dedicated=%d",
            server_result,
            dedicated_result
        );

        if (dedicated_result == 1)
        {
            if (server_result != 1)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ROLE IsServer context mismatch; "
                    "IsDedicatedServer is authoritative"
                );
            }

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "ROLE RESULT=PASS"
            );

            return true;
        }

        emit_marker(
            "[ModIntegratedStorageCpp] "
            "ROLE RESULT=NOT_DEDICATED"
        );

        return false;
    }

    auto get_camp_discovery_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // FindAllOf populates this vector inside libUE4SS.so.
        //
        // NullPrism and native mods currently carry separate static
        // C++ runtimes. Destroying the vector in this module would
        // therefore free storage allocated by the loader's runtime.
        //
        // Retain one process-lifetime buffer and reuse its capacity.
        // clear() removes pointer elements without releasing storage.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto get_chest_discovery_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // FindAllOf populates this vector inside libUE4SS.so.
        //
        // As with the Stage-4a camp buffer, retain it for process
        // lifetime so main.so never destroys storage allocated while
        // libUE4SS.so was operating on the vector.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto find_discovery_context()
        -> RC::Unreal::UObject*
    {
        auto* manager =
            RC::Unreal::UObjectGlobals::FindFirstOf(
                STR("PalMapObjectManager")
            );

        if (manager != nullptr)
        {
            return manager;
        }

        auto& camps = get_camp_discovery_buffer();
        camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            camps
        );

        for (auto* camp : camps)
        {
            if (camp != nullptr)
            {
                return camp;
            }
        }

        return nullptr;
    }

    auto find_role_context()
        -> RC::Unreal::UObject*
    {
        auto* player_character =
            RC::Unreal::UObjectGlobals::FindFirstOf(
                STR("PalPlayerCharacter")
            );

        if (player_character != nullptr)
        {
            return player_character;
        }

        return find_discovery_context();
    }


    auto get_role_probe_camp_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // Dedicated process-lifetime buffer prevents concurrent use of
        // the worker-thread discovery vector and avoids cross-DSO release.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto find_game_thread_role_context()
        -> RC::Unreal::UObject*
    {
        auto* player_character =
            RC::Unreal::UObjectGlobals::FindFirstOf(
                STR("PalPlayerCharacter")
            );

        if (player_character != nullptr)
        {
            return player_character;
        }

        auto& camps = get_role_probe_camp_buffer();
        camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            camps
        );

        for (auto* camp : camps)
        {
            if (camp != nullptr)
            {
                return camp;
            }
        }

        return nullptr;
    }


    auto get_registration_probe_camp_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // Process-lifetime allocation avoids releasing vector storage
        // across the native-mod and NullPrism runtime boundary.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto run_read_only_discovery() -> void
    {
        auto& camps = get_camp_discovery_buffer();
        camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            camps
        );

        std::unordered_map<
            GuildKey,
            GuildDiscovery,
            GuildKeyHash
        > guilds{};

        std::size_t valid_camps{};
        std::size_t null_camps{};
        std::size_t missing_guild_properties{};
        std::size_t zero_guild_keys{};
        std::size_t storage_count{};
        std::size_t camps_without_storage{};

        for (auto* camp : camps)
        {
            if (camp == nullptr)
            {
                ++null_camps;
                continue;
            }

            GuildKey guild_key{};

            if (!copy_guild_key(camp, guild_key))
            {
                ++missing_guild_properties;
                continue;
            }

            if (guid_is_zero(guild_key))
            {
                ++zero_guild_keys;
                continue;
            }

            ++valid_camps;

            auto& guild = guilds[guild_key];
            ++guild.camp_count;

            auto* storage = find_storage_module(camp);

            if (storage == nullptr)
            {
                ++camps_without_storage;
                continue;
            }

            const auto [ignored_iterator, inserted] =
                guild.storage_modules.insert(storage);

            static_cast<void>(ignored_iterator);

            if (inserted)
            {
                ++storage_count;
            }
        }

        ++g_discovery_runs;

        emit_format(
            "[ModIntegratedStorageCpp] DISCOVERY run=%llu "
            "objects=%zu valid_camps=%zu guilds=%zu storages=%zu "
            "null_camps=%zu missing_guild=%zu zero_guild=%zu "
            "without_storage=%zu",
            static_cast<unsigned long long>(g_discovery_runs),
            camps.size(),
            valid_camps,
            guilds.size(),
            storage_count,
            null_camps,
            missing_guild_properties,
            zero_guild_keys,
            camps_without_storage
        );

        for (const auto& [guild_key, guild] : guilds)
        {
            const auto hex = guid_to_hex(guild_key);

            emit_format(
                "[ModIntegratedStorageCpp] GUILD id=%s "
                "camps=%zu storages=%zu",
                hex.data(),
                guild.camp_count,
                guild.storage_modules.size()
            );
        }

        if (
            valid_camps > 0 &&
            !guilds.empty() &&
            storage_count > 0
        )
        {
            emit_marker(
                "[ModIntegratedStorageCpp] DISCOVERY RESULT=PASS"
            );
        }
        else if (camps.empty())
        {
            emit_marker(
                "[ModIntegratedStorageCpp] DISCOVERY RESULT=EMPTY"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] DISCOVERY RESULT=INCOMPLETE"
            );
        }
    }

    auto association_class_is(
        RC::Unreal::UObject* object,
        RC::Unreal::UClass* expected
    ) -> bool
    {
        if (
            object == nullptr ||
            expected == nullptr
        )
        {
            return false;
        }

        RC::Unreal::UStruct* current =
            object->GetClassPrivate();

        while (current != nullptr)
        {
            if (current == expected)
            {
                return true;
            }

            current = current->GetSuperStruct();
        }

        return false;
    }

    auto get_base_camp_class()
        -> RC::Unreal::UClass*
    {
        static auto* base_camp_class =
            RC::Unreal::UObjectGlobals::StaticFindObject<
                RC::Unreal::UClass*>(
                nullptr,
                nullptr,
                STR("/Script/Pal.PalBaseCampModel")
            );

        return base_camp_class;
    }

    auto request_read_only_chest_association() noexcept
        -> void
    {
        if (
            !g_chest_association_enabled.load(
                std::memory_order_acquire
            )
        )
        {
            return;
        }

        g_chest_association_requested.store(
            true,
            std::memory_order_release
        );
    }


    auto run_read_only_registration_metadata_probe(
        RC::Unreal::UObject* chest,
        RC::Unreal::UObject* target_storage
    ) -> void
    {
        const auto run =
            g_chest_association_runs.load(
                std::memory_order_acquire
            ) + 1;

        RC::Unreal::UFunction* function{};

        if (target_storage != nullptr)
        {
            function =
                target_storage->
                    GetFunctionByNameInChain(
                        STR(
                            "OnAvailableConcreteModel_"
                            "ServerInternal"
                        )
                    );
        }

        std::size_t parameter_bytes{};
        std::size_t input_parameters{};
        std::size_t object_parameters{};

        RC::Unreal::FObjectProperty*
            object_parameter{};

        if (function != nullptr)
        {
            parameter_bytes =
                static_cast<std::size_t>(
                    function->GetParmsSize()
                );

            for (
                auto* property :
                    function->ForEachProperty()
            )
            {
                if (
                    property == nullptr ||
                    !property->HasAnyPropertyFlags(
                        RC::Unreal::CPF_Parm
                    ) ||
                    property->HasAnyPropertyFlags(
                        RC::Unreal::CPF_ReturnParm
                    )
                )
                {
                    continue;
                }

                ++input_parameters;

                auto* object_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectProperty
                    >(property);

                if (object_property != nullptr)
                {
                    ++object_parameters;

                    if (object_parameter == nullptr)
                    {
                        object_parameter =
                            object_property;
                    }
                }
            }
        }

        std::int32_t parameter_offset{-1};
        std::int32_t property_size{-1};
        std::uint64_t property_flags{};

        RC::Unreal::UClass* property_class{};

        if (object_parameter != nullptr)
        {
            parameter_offset =
                object_parameter->
                    GetOffset_Internal();

            property_size =
                object_parameter->GetSize();

            property_flags =
                static_cast<std::uint64_t>(
                    object_parameter->
                        GetPropertyFlags()
                );

            property_class =
                object_parameter->
                    GetPropertyClass();
        }

        const bool bounds_valid =
            parameter_offset >= 0 &&
            property_size ==
                static_cast<std::int32_t>(
                    sizeof(
                        RC::Unreal::UObject*
                    )
                ) &&
            parameter_bytes >=
                sizeof(
                    RC::Unreal::UObject*
                ) &&
            static_cast<std::size_t>(
                parameter_offset
            ) <=
                parameter_bytes -
                    sizeof(
                        RC::Unreal::UObject*
                    );

        const bool chest_compatible =
            class_is(
                chest,
                property_class
            );

        const bool passed =
            chest != nullptr &&
            target_storage != nullptr &&
            function != nullptr &&
            input_parameters == 1 &&
            object_parameters == 1 &&
            object_parameter != nullptr &&
            property_class != nullptr &&
            bounds_valid &&
            chest_compatible;

        emit_format(
            "[ModIntegratedStorageCpp] REG_META "
            "run=%llu candidate=%d target=%d "
            "function=%d parms=%zu inputs=%zu "
            "object_inputs=%zu offset=%d size=%d "
            "flags=0x%llx property_class=%d "
            "compatible=%d",
            static_cast<unsigned long long>(
                run
            ),
            chest != nullptr ? 1 : 0,
            target_storage != nullptr ? 1 : 0,
            function != nullptr ? 1 : 0,
            parameter_bytes,
            input_parameters,
            object_parameters,
            parameter_offset,
            property_size,
            static_cast<unsigned long long>(
                property_flags
            ),
            property_class != nullptr ? 1 : 0,
            chest_compatible ? 1 : 0
        );

        if (passed)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "REG_META RESULT=PASS"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "REG_META RESULT=INCOMPLETE"
            );
        }
    }

    auto run_read_only_chest_association() -> void
    {
        auto& chests = get_chest_discovery_buffer();
        chests.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalMapObjectItemChestModel"),
            chests
        );

        auto* base_camp_class =
            get_base_camp_class();

        RC::Unreal::UFunction* association_function{};

        std::size_t parameter_size{};
        std::size_t return_offset{};

        std::unique_ptr<std::byte[]> parameters{};

        std::unordered_map<
            GuildKey,
            std::size_t,
            GuildKeyHash
        > guild_chests{};

        std::unordered_set<
            RC::Unreal::UObject*
        > associated_camps{};

        std::size_t valid_chests{};
        std::size_t null_chests{};
        std::size_t associated_chests{};
        std::size_t unassociated_chests{};
        std::size_t missing_functions{};
        std::size_t invalid_parameter_layouts{};
        std::size_t invalid_camps{};
        std::size_t missing_guild_properties{};
        std::size_t zero_guild_keys{};

        auto& registration_probe_camps =
            get_registration_probe_camp_buffer();

        registration_probe_camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            registration_probe_camps
        );

        std::unordered_map<
            GuildKey,
            RegistrationPlanGuild,
            GuildKeyHash
        > registration_plan{};

        std::unordered_map<
            RC::Unreal::UObject*,
            GuildKey
        > storage_guilds{};

        std::unordered_map<
            RC::Unreal::UObject*,
            GuildKey
        > chest_guilds{};

        std::size_t plan_null_camps{};
        std::size_t plan_invalid_camps{};
        std::size_t plan_missing_guild{};
        std::size_t plan_zero_guild{};
        std::size_t plan_without_storage{};

        std::size_t duplicate_storage_refs{};
        std::size_t storage_camp_conflicts{};
        std::size_t storage_guild_conflicts{};

        std::size_t duplicate_chest_refs{};
        std::size_t chest_camp_conflicts{};
        std::size_t chest_guild_conflicts{};

        for (
            auto* candidate_camp :
                registration_probe_camps
        )
        {
            if (candidate_camp == nullptr)
            {
                ++plan_null_camps;
                continue;
            }

            if (
                !association_class_is(
                    candidate_camp,
                    base_camp_class
                )
            )
            {
                ++plan_invalid_camps;
                continue;
            }

            GuildKey candidate_guild{};

            if (
                !copy_guild_key(
                    candidate_camp,
                    candidate_guild
                )
            )
            {
                ++plan_missing_guild;
                continue;
            }

            if (guid_is_zero(candidate_guild))
            {
                ++plan_zero_guild;
                continue;
            }

            auto& guild =
                registration_plan[
                    candidate_guild
                ];

            const auto storage_count =
                for_each_storage_module(
                    candidate_camp,
                    [&](RC::Unreal::UObject* storage)
                    {
                        const auto [
                            global_iterator,
                            global_inserted
                        ] =
                            storage_guilds.emplace(
                                storage,
                                candidate_guild
                            );

                        if (
                            !global_inserted &&
                            global_iterator->second !=
                                candidate_guild
                        )
                        {
                            ++storage_guild_conflicts;
                        }

                        const auto [
                            storage_iterator,
                            storage_inserted
                        ] =
                            guild.storage_camps.emplace(
                                storage,
                                candidate_camp
                            );

                        if (!storage_inserted)
                        {
                            ++duplicate_storage_refs;

                            if (
                                storage_iterator->second !=
                                    candidate_camp
                            )
                            {
                                ++storage_camp_conflicts;
                            }
                        }
                    }
                );

            if (storage_count == 0)
            {
                ++plan_without_storage;
            }
        }

        for (auto* chest : chests)
        {
            if (chest == nullptr)
            {
                ++null_chests;
                continue;
            }

            ++valid_chests;

            if (association_function == nullptr)
            {
                association_function =
                    chest->GetFunctionByNameInChain(
                        STR("GetBaseCampModelBelongTo")
                    );

                if (association_function == nullptr)
                {
                    ++missing_functions;
                    continue;
                }

                parameter_size =
                    static_cast<std::size_t>(
                        association_function->
                            GetParmsSize()
                    );

                return_offset =
                    static_cast<std::size_t>(
                        association_function->
                            GetReturnValueOffset()
                    );

                if (
                    parameter_size <
                        sizeof(
                            RC::Unreal::UObject*
                        ) ||
                    return_offset >
                        parameter_size -
                            sizeof(
                                RC::Unreal::UObject*
                            )
                )
                {
                    association_function = nullptr;
                    parameter_size = 0;
                    return_offset = 0;

                    ++invalid_parameter_layouts;
                    continue;
                }

                parameters =
                    std::make_unique<
                        std::byte[]
                    >(parameter_size);
            }

            std::memset(
                parameters.get(),
                0,
                parameter_size
            );

            chest->ProcessEvent(
                association_function,
                parameters.get()
            );

            RC::Unreal::UObject* camp{};

            std::memcpy(
                &camp,
                parameters.get() + return_offset,
                sizeof(camp)
            );

            if (camp == nullptr)
            {
                ++unassociated_chests;
                continue;
            }

            if (
                !association_class_is(
                    camp,
                    base_camp_class
                )
            )
            {
                ++invalid_camps;
                continue;
            }

            GuildKey guild_key{};

            if (!copy_guild_key(camp, guild_key))
            {
                ++missing_guild_properties;
                continue;
            }

            if (guid_is_zero(guild_key))
            {
                ++zero_guild_keys;
                continue;
            }

            ++associated_chests;
            ++guild_chests[guild_key];
            associated_camps.insert(camp);

            auto& plan_guild =
                registration_plan[guild_key];

            const auto [
                global_chest_iterator,
                global_chest_inserted
            ] =
                chest_guilds.emplace(
                    chest,
                    guild_key
                );

            if (
                !global_chest_inserted &&
                global_chest_iterator->second !=
                    guild_key
            )
            {
                ++chest_guild_conflicts;
            }

            const auto [
                chest_iterator,
                chest_inserted
            ] =
                plan_guild.chest_camps.emplace(
                    chest,
                    camp
                );

            if (!chest_inserted)
            {
                ++duplicate_chest_refs;

                if (
                    chest_iterator->second != camp
                )
                {
                    ++chest_camp_conflicts;
                }
            }
        }

        const auto planned_run =
            g_chest_association_runs.load(
                std::memory_order_acquire
            ) + 1;

        std::vector<GuildKey> guild_order{};
        guild_order.reserve(
            registration_plan.size()
        );

        for (
            const auto& [guild_key, ignored_guild] :
                registration_plan
        )
        {
            static_cast<void>(ignored_guild);
            guild_order.push_back(guild_key);
        }

        std::sort(
            guild_order.begin(),
            guild_order.end()
        );

        std::unordered_set<
            RegistrationPlanPair,
            RegistrationPlanPairHash
        > planned_pair_set{};

        RC::Unreal::UObject*
            registration_probe_chest{};

        RC::Unreal::UObject*
            registration_probe_target_storage{};

        std::size_t guilds_with_pairs{};
        std::size_t planned_chests{};
        std::size_t planned_storages{};
        std::size_t planned_pairs{};
        std::size_t own_camp_pairs{};
        std::size_t duplicate_pairs{};

        std::uint64_t fingerprint_xor{};
        std::uint64_t fingerprint_sum{};

        for (const auto& guild_key : guild_order)
        {
            const auto guild_iterator =
                registration_plan.find(
                    guild_key
                );

            if (
                guild_iterator ==
                    registration_plan.end()
            )
            {
                continue;
            }

            const auto& guild =
                guild_iterator->second;

            planned_chests +=
                guild.chest_camps.size();

            planned_storages +=
                guild.storage_camps.size();

            std::size_t guild_pairs{};
            std::size_t guild_own_camp{};

            for (
                const auto& [
                    chest,
                    chest_camp
                ] : guild.chest_camps
            )
            {
                for (
                    const auto& [
                        storage,
                        storage_camp
                    ] : guild.storage_camps
                )
                {
                    if (chest_camp == storage_camp)
                    {
                        ++guild_own_camp;
                        ++own_camp_pairs;
                        continue;
                    }

                    const RegistrationPlanPair pair{
                        chest,
                        storage
                    };

                    const auto [
                        ignored_pair_iterator,
                        pair_inserted
                    ] =
                        planned_pair_set.insert(
                            pair
                        );

                    static_cast<void>(
                        ignored_pair_iterator
                    );

                    if (!pair_inserted)
                    {
                        ++duplicate_pairs;
                        continue;
                    }

                    ++guild_pairs;
                    ++planned_pairs;

                    const auto fingerprint =
                        registration_pair_fingerprint(
                            guild_key,
                            pair
                        );

                    fingerprint_xor ^=
                        fingerprint;

                    fingerprint_sum +=
                        fingerprint;

                    if (
                        registration_probe_chest ==
                            nullptr
                    )
                    {
                        registration_probe_chest =
                            chest;

                        registration_probe_target_storage =
                            storage;
                    }
                }
            }

            if (guild_pairs > 0)
            {
                ++guilds_with_pairs;
            }

            const auto guild_hex =
                guid_to_hex(guild_key);

            emit_format(
                "[ModIntegratedStorageCpp] "
                "WOULD_REGISTER_GUILD "
                "run=%llu guild=%s "
                "chests=%zu storages=%zu "
                "pairs=%zu own_camp=%zu",
                static_cast<unsigned long long>(
                    planned_run
                ),
                guild_hex.data(),
                guild.chest_camps.size(),
                guild.storage_camps.size(),
                guild_pairs,
                guild_own_camp
            );
        }

        const bool plan_complete =
            !registration_plan.empty() &&
            guilds_with_pairs > 0 &&
            planned_chests > 0 &&
            planned_chests ==
                associated_chests &&
            planned_storages > 0 &&
            planned_pairs > 0 &&
            registration_probe_chest != nullptr &&
            registration_probe_target_storage !=
                nullptr &&
            duplicate_chest_refs == 0 &&
            duplicate_storage_refs == 0 &&
            duplicate_pairs == 0 &&
            chest_camp_conflicts == 0 &&
            storage_camp_conflicts == 0 &&
            chest_guild_conflicts == 0 &&
            storage_guild_conflicts == 0 &&
            plan_null_camps == 0 &&
            plan_invalid_camps == 0 &&
            plan_missing_guild == 0 &&
            plan_zero_guild == 0 &&
            plan_without_storage == 0;

        emit_format(
            "[ModIntegratedStorageCpp] "
            "WOULD_REGISTER run=%llu "
            "guilds=%zu active_guilds=%zu "
            "chests=%zu storages=%zu "
            "pairs=%zu own_camp=%zu "
            "duplicate_chests=%zu "
            "duplicate_storages=%zu "
            "duplicate_pairs=%zu "
            "chest_camp_conflicts=%zu "
            "storage_camp_conflicts=%zu "
            "chest_guild_conflicts=%zu "
            "storage_guild_conflicts=%zu "
            "null_camps=%zu invalid_camps=%zu "
            "missing_guild=%zu zero_guild=%zu "
            "without_storage=%zu "
            "fingerprint_xor=%016llx "
            "fingerprint_sum=%016llx",
            static_cast<unsigned long long>(
                planned_run
            ),
            registration_plan.size(),
            guilds_with_pairs,
            planned_chests,
            planned_storages,
            planned_pairs,
            own_camp_pairs,
            duplicate_chest_refs,
            duplicate_storage_refs,
            duplicate_pairs,
            chest_camp_conflicts,
            storage_camp_conflicts,
            chest_guild_conflicts,
            storage_guild_conflicts,
            plan_null_camps,
            plan_invalid_camps,
            plan_missing_guild,
            plan_zero_guild,
            plan_without_storage,
            static_cast<unsigned long long>(
                fingerprint_xor
            ),
            static_cast<unsigned long long>(
                fingerprint_sum
            )
        );

        if (plan_complete)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "WOULD_REGISTER RESULT=PASS"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "WOULD_REGISTER RESULT=INCOMPLETE"
            );
        }

        run_read_only_registration_metadata_probe(
            registration_probe_chest,
            registration_probe_target_storage
        );

        const auto run =
            g_chest_association_runs.fetch_add(
                1,
                std::memory_order_acq_rel
            ) + 1;

        emit_format(
            "[ModIntegratedStorageCpp] CHEST_ASSOC "
            "run=%llu objects=%zu valid=%zu "
            "associated=%zu unassociated=%zu "
            "guilds=%zu camps=%zu null=%zu "
            "missing_function=%zu "
            "invalid_parameters=%zu invalid_camp=%zu "
            "missing_guild=%zu zero_guild=%zu",
            static_cast<unsigned long long>(run),
            chests.size(),
            valid_chests,
            associated_chests,
            unassociated_chests,
            guild_chests.size(),
            associated_camps.size(),
            null_chests,
            missing_functions,
            invalid_parameter_layouts,
            invalid_camps,
            missing_guild_properties,
            zero_guild_keys
        );

        for (
            const auto& [guild_key, chest_count]
                : guild_chests
        )
        {
            const auto hex =
                guid_to_hex(guild_key);

            emit_format(
                "[ModIntegratedStorageCpp] "
                "CHEST_GUILD id=%s chests=%zu",
                hex.data(),
                chest_count
            );
        }

        const bool complete =
            base_camp_class != nullptr &&
            valid_chests > 0 &&
            valid_chests ==
                associated_chests +
                unassociated_chests &&
            null_chests == 0 &&
            missing_functions == 0 &&
            invalid_parameter_layouts == 0 &&
            invalid_camps == 0 &&
            missing_guild_properties == 0 &&
            zero_guild_keys == 0;

        if (complete)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=PASS"
            );
        }
        else if (chests.empty())
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=EMPTY"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=INCOMPLETE"
            );
        }
    }

    auto on_engine_tick(
        RC::Unreal::Hook::TCallbackIterationData<void>&,
        RC::Unreal::UEngine*,
        float,
        bool
    ) -> void
    {
        EngineTickEntryGuard entry_guard{};

        if (
            !g_chest_association_enabled.load(
                std::memory_order_acquire
            )
        )
        {
            return;
        }

        if (!RC::Unreal::IsInGameThreadRaw())
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "ENGINE_TICK THREAD=INVALID"
            );

            return;
        }

        if (
            g_role_probe_requested.exchange(
                false,
                std::memory_order_acq_rel
            )
        )
        {
            try
            {
                auto* context =
                    find_game_thread_role_context();

                if (context == nullptr)
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "ROLE RESULT=NO_CONTEXT"
                    );
                }
                else
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "ROLE THREAD=GAME"
                    );

                    static_cast<void>(
                        resolve_dedicated_role(
                            context
                        )
                    );
                }
            }
            catch (...)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ROLE RESULT=EXCEPTION"
                );
            }
        }

        if (
            !g_chest_association_requested.exchange(
                false,
                std::memory_order_acq_rel
            )
        )
        {
            return;
        }

        if (
            g_chest_association_running.exchange(
                true,
                std::memory_order_acq_rel
            )
        )
        {
            g_chest_association_requested.store(
                true,
                std::memory_order_release
            );

            return;
        }

        AssociationRunningGuard running_guard{};

        try
        {
            run_read_only_chest_association();
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=EXCEPTION"
            );
        }
    }

    class ModIntegratedStorageCpp final
        : public RC::CppUserModBase
    {
      public:
        ModIntegratedStorageCpp()
        {
            ModName =
                STR("IntegratedStorageCpp");

            ModVersion =
                STR("0.1.0-linux-stage4c.2-would-register");

            ModDescription =
                STR(
                    "Linux dedicated-server read-only "
                    "game-thread role, metadata and deterministic would-register planning."
                );

            ModAuthors =
                STR("Sarfflow; Linux port by ManaPirate");

            emit_marker(
                "[ModIntegratedStorageCpp] constructor"
            );
        }

        ~ModIntegratedStorageCpp() override
        {
            g_chest_association_enabled.store(
                false,
                std::memory_order_release
            );

            g_role_probe_requested.store(
                false,
                std::memory_order_release
            );

            g_chest_association_requested.store(
                false,
                std::memory_order_release
            );

            const auto callback_id =
                g_engine_tick_callback_id;

            g_engine_tick_callback_id =
                RC::Unreal::Hook::ERROR_ID;

            if (
                callback_id !=
                    RC::Unreal::Hook::ERROR_ID
            )
            {
                const bool removed =
                    RC::Unreal::Hook::
                        UnregisterCallback(
                            callback_id
                        );

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "ENGINE_TICK unregister=%d",
                    removed ? 1 : 0
                );
            }

            bool quiescent{};

            for (int attempt = 0; attempt < 500; ++attempt)
            {
                if (
                    g_engine_tick_entries.load(
                        std::memory_order_acquire
                    ) == 0 &&
                    !g_chest_association_running.load(
                        std::memory_order_acquire
                    )
                )
                {
                    quiescent = true;
                    break;
                }

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10)
                );
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "ENGINE_TICK quiescent=%d",
                quiescent ? 1 : 0
            );

            emit_marker(
                "[ModIntegratedStorageCpp] destructor"
            );
        }

        auto on_program_start() -> void override
        {
            emit_marker(
                "[ModIntegratedStorageCpp] on_program_start"
            );
        }

        auto on_cpp_mods_loaded() -> void override
        {
            emit_marker(
                "[ModIntegratedStorageCpp] on_cpp_mods_loaded"
            );
        }

        auto on_unreal_init() -> void override
        {
            emit_marker(
                "[ModIntegratedStorageCpp] on_unreal_init"
            );

            auto* object_class =
                RC::Unreal::UObjectGlobals::StaticFindObject<
                    RC::Unreal::UObject*>(
                    nullptr,
                    nullptr,
                    STR("/Script/CoreUObject.Object")
                );

            if (object_class == nullptr)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "StaticFindObject result=null"
                );

                emit_marker(
                    "[ModIntegratedStorageCpp] RESULT=FAIL"
                );

                return;
            }

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "StaticFindObject result=valid"
            );

            emit_marker(
                "[ModIntegratedStorageCpp] RESULT=PASS"
            );

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "STAGE4A read-only discovery enabled"
            );

            if (
                g_engine_tick_callback_id ==
                    RC::Unreal::Hook::ERROR_ID
            )
            {
                const auto callback_id =
                    RC::Unreal::Hook::
                        RegisterEngineTickPreCallback(
                            &on_engine_tick,
                            {}
                        );

                g_engine_tick_callback_id =
                    callback_id;

                const bool registered =
                    callback_id !=
                        RC::Unreal::Hook::ERROR_ID;

                g_chest_association_enabled.store(
                    registered,
                    std::memory_order_release
                );

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "ENGINE_TICK registered=%d",
                    registered ? 1 : 0
                );
            }

        }

        auto on_update() -> void override
        {
            const auto now = Clock::now();

            if (
                timepoint_is_empty(g_last_world_probe) ||
                now - g_last_world_probe >= WorldProbeInterval
            )
            {
                g_last_world_probe = now;

                auto* context = find_role_context();

                if (
                    context != nullptr &&
                    observe_world(context) &&
                    g_is_dedicated.load(
                        std::memory_order_acquire
                    ) < 0
                )
                {
                    g_role_probe_requested.store(
                        true,
                        std::memory_order_release
                    );
                }
            }

            if (
                g_is_dedicated.load(
                    std::memory_order_acquire
                ) != 1
            )
            {
                return;
            }

            if (
                timepoint_is_empty(g_last_discovery) ||
                now - g_last_discovery >= DiscoveryInterval
            )
            {
                g_last_discovery = now;
                run_read_only_discovery();
                request_read_only_chest_association();
            }
        }
    };
}

extern "C"
__attribute__((visibility("default")))
auto start_mod() -> RC::CppUserModBase*
{
    const bool module_pinned =
        ModIntegratedStorageModulePin::
            pin_for_process_lifetime();

    std::fprintf(
        stderr,
        "[ModIntegratedStorageCpp] "
        "MODULE_PIN result=%s\n",
        module_pinned ? "PASS" : "FAIL"
    );

    std::fflush(stderr);

    if (!module_pinned)
    {
        return nullptr;
    }


    emit_marker(
        "[ModIntegratedStorageCpp] start_mod export"
    );

    return new ModIntegratedStorageCpp{};
}

extern "C"
__attribute__((visibility("default")))
auto uninstall_mod(RC::CppUserModBase* mod) -> void
{
    emit_marker(
        "[ModIntegratedStorageCpp] uninstall_mod export"
    );

    delete mod;
}

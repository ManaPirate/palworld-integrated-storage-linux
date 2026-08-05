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
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>

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

    RC::Unreal::UObject* g_pal_utility{};
    RC::Unreal::UObject* g_last_world{};

    int g_is_server{-1};
    int g_is_dedicated{-1};

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

    auto reset_world_state() noexcept -> void
    {
        g_is_server = -1;
        g_is_dedicated = -1;

        g_last_discovery = Clock::time_point{};
        g_discovery_runs = 0;

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
        if (g_is_dedicated >= 0)
        {
            return g_is_dedicated == 1;
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

        g_is_dedicated = dedicated_result;
        g_is_server = server_result;

        emit_format(
            "[ModIntegratedStorageCpp] ROLE server=%d dedicated=%d",
            g_is_server,
            g_is_dedicated
        );

        if (g_is_dedicated == 1)
        {
            if (g_is_server != 1)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ROLE IsServer context mismatch; "
                    "IsDedicatedServer is authoritative"
                );
            }

            emit_marker(
                "[ModIntegratedStorageCpp] ROLE RESULT=PASS"
            );

            return true;
        }

        emit_marker(
            "[ModIntegratedStorageCpp] ROLE RESULT=NOT_DEDICATED"
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

    class ModIntegratedStorageCpp final
        : public RC::CppUserModBase
    {
      public:
        ModIntegratedStorageCpp()
        {
            ModName =
                STR("IntegratedStorageCpp");

            ModVersion =
                STR("0.1.0-linux-stage4a.2");

            ModDescription =
                STR(
                    "Linux dedicated-server read-only "
                    "base-camp, guild and storage discovery."
                );

            ModAuthors =
                STR("Sarfflow; Linux port by ManaPirate");

            emit_marker(
                "[ModIntegratedStorageCpp] constructor"
            );
        }

        ~ModIntegratedStorageCpp() override
        {
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

                if (context != nullptr)
                {
                    observe_world(context);
                    resolve_dedicated_role(context);
                }
            }

            if (g_is_dedicated != 1)
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
            }
        }
    };
}

extern "C"
__attribute__((visibility("default")))
auto start_mod() -> RC::CppUserModBase*
{
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

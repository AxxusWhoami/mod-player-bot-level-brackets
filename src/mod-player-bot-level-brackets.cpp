#include "mod-player-bot-level-brackets-internal.h"

using namespace Acore::ChatCommands;

// =============================================================================
// WORLD SCRIPT
// =============================================================================
class BotLevelBracketsWorldScript : public WorldScript
{
public:
    BotLevelBracketsWorldScript()
        : WorldScript("BotLevelBracketsWorldScript"),
          m_timer(0), m_flaggedTimer(0), m_guildTrackerTimer(0), m_socialListTimer(0), m_teleportTimer(0),
          m_hubPopulateTimer(0), m_startingZoneTimer(0), m_wrongMapTimer(0)
    { }

    void OnStartup() override
    {
        LoadBotLevelBracketsConfig();
        LoadSocialFriendList();
        LoadPersistentGuildTracker();
        LoadPendingResetsFromDB();

        if (!g_BotLevelBracketsEnabled)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module disabled via configuration.");
            return;
        }

        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading",
                     "[BotLevelBrackets] Module loaded. Check: {}s, Flagged: {}s, Social: {}s, GuildTracker: {}s, HubPopulate: {}s, StartingZone: {}s, WrongMap: {}s.",
                     g_BotDistCheckFrequency, g_BotDistFlaggedCheckFrequency,
                     g_SocialListRefreshFrequency, g_GuildTrackerUpdateFrequency,
                     g_HubPopulateFrequency, g_StartingZoneDisperseFrequency, g_WrongMapDisperseFrequency);
            for (uint8 i = 0; i < g_NumRanges; ++i)
                LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {}: {}-{}, Desired: {}%",
                         i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                         g_AllianceLevelRanges[i].desiredPercent);
            for (uint8 i = 0; i < g_NumRanges; ++i)
                LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {}: {}-{}, Desired: {}%",
                         i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper,
                         g_HordeLevelRanges[i].desiredPercent);
            if (g_MaxPendingQueueSize > 0)
                LOG_INFO("server.loading", "[BotLevelBrackets] Pending queue limit: {}.", g_MaxPendingQueueSize);
            if (g_PendingQueueTTL > 0)
                LOG_INFO("server.loading", "[BotLevelBrackets] Pending queue TTL: {}s.", g_PendingQueueTTL);
        }
    }

    void OnUpdate(uint32 diff) override
    {
        if (!g_BotLevelBracketsEnabled)
            return;

        m_timer             += diff;
        m_flaggedTimer      += diff;
        m_guildTrackerTimer += diff;
        m_socialListTimer   += diff;
        m_teleportTimer      += diff;
        m_hubPopulateTimer   += diff;
        m_startingZoneTimer  += diff;
        m_wrongMapTimer       += diff;

        if (m_flaggedTimer >= 5000)
        {
            ProcessPendingLevelResets();
            m_flaggedTimer = 0;
        }

        if (m_teleportTimer >= 2000)
        {
            ProcessPendingTeleports();
            m_teleportTimer = 0;
        }

        if (m_hubPopulateTimer >= g_HubPopulateFrequency * 1000)
        {
            ProcessHubPopulate();
            m_hubPopulateTimer = 0;
        }

        if (m_startingZoneTimer >= g_StartingZoneDisperseFrequency * 1000)
        {
            ProcessStartingZoneDisperse();
            m_startingZoneTimer = 0;
        }

        if (m_wrongMapTimer >= g_WrongMapDisperseFrequency * 1000)
        {
            ProcessWrongMapDisperse();
            m_wrongMapTimer = 0;
        }

        if (m_guildTrackerTimer >= g_GuildTrackerUpdateFrequency * 1000)
        {
            if (g_BotDistFullDebugMode)
                LOG_INFO("server.world", "[BotLevelBrackets] Guild Tracker Update Triggering.");
            UpdatePersistentGuildTracker();
            m_guildTrackerTimer = 0;
        }

        if (m_socialListTimer >= g_SocialListRefreshFrequency * 1000)
        {
            LoadSocialFriendList();
            m_socialListTimer = 0;
        }

        if (m_timer >= g_BotDistCheckFrequency * 1000)
        {
            RunDistributionCycle(nullptr);
            m_timer = 0;
        }
    }

private:
    uint32 m_timer;
    uint32 m_flaggedTimer;
    uint32 m_guildTrackerTimer;
    uint32 m_socialListTimer;
    uint32 m_teleportTimer;
    uint32 m_hubPopulateTimer;
    uint32 m_startingZoneTimer;
    uint32 m_wrongMapTimer;
};


// =============================================================================
// PLAYER SCRIPT
// =============================================================================
class BotLevelBracketsPlayerScript : public PlayerScript
{
public:
    BotLevelBracketsPlayerScript() : PlayerScript("BotLevelBracketsPlayerScript") {}

    void OnPlayerLogout(Player* player) override
    {
        RemoveBotFromPendingResets(player);
        RemoveBotFromPendingTeleports(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
            return;
        if (IsBracketPlayerBot(player))
            return;

        uint32 guildId = player->GetGuildId();
        if (guildId == 0)
            return;

        g_RealPlayerGuildIds.insert(guildId);
        g_PersistentRealPlayerGuildIds.insert(guildId);
        CharacterDatabase.Execute(
            "REPLACE INTO bot_level_brackets_guild_tracker (guild_id, has_real_players) VALUES ({}, 1)",
            guildId);

        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world",
                     "[BotLevelBrackets] Player '{}' logged in - guild {} marked as having real players.",
                     player->GetName(), guildId);
    }
};


// =============================================================================
// GUILD SCRIPT
// =============================================================================
class BotLevelBracketsGuildScript : public GuildScript
{
public:
    BotLevelBracketsGuildScript() : GuildScript("BotLevelBracketsGuildScript") {}

    void OnAddMember(Guild* guild, Player* player, uint8& /*plRank*/) override
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
            return;
        if (!player || IsBracketPlayerBot(player))
            return;

        uint32 guildId = guild->GetId();
        g_RealPlayerGuildIds.insert(guildId);
        g_PersistentRealPlayerGuildIds.insert(guildId);
        CharacterDatabase.Execute(
            "REPLACE INTO bot_level_brackets_guild_tracker (guild_id, has_real_players) VALUES ({}, 1)",
            guildId);

        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world",
                     "[BotLevelBrackets] Real player '{}' joined guild {} - guild marked as having real players.",
                     player->GetName(), guildId);
    }

    void OnRemoveMember(Guild* guild, Player* player, bool /*isDisbanding*/, bool /*isKicked*/) override
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
            return;
        if (!player || IsBracketPlayerBot(player))
            return;

        uint32 guildId = guild->GetId();

        bool otherRealPlayerOnline = false;
        const auto& allPlayers = ObjectAccessor::GetPlayers();
        for (const auto& itr : allPlayers)
        {
            Player* p = itr.second;
            if (!p || !p->IsInWorld() || p == player)
                continue;
            if (!IsBracketPlayerBot(p) && p->GetGuildId() == guildId)
            {
                otherRealPlayerOnline = true;
                break;
            }
        }

        if (!otherRealPlayerOnline)
        {
            g_RealPlayerGuildIds.erase(guildId);
            if (g_BotDistFullDebugMode)
                LOG_INFO("server.world",
                         "[BotLevelBrackets] Real player '{}' left guild {} with no other online real players - "
                         "removed from online cache. Persistent tracker updated by timer.",
                         player->GetName(), guildId);
        }
    }
};


// =============================================================================
// COMMAND SCRIPT
// =============================================================================
class BotLevelBracketsCommandScript : public CommandScript
{
public:
    BotLevelBracketsCommandScript() : CommandScript("BotLevelBracketsCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable botBracketsTable =
        {
            { "reload",       HandleReloadConfig,  SEC_ADMINISTRATOR, Console::Yes },
            { "status",       HandleStatus,        SEC_ADMINISTRATOR, Console::Yes },
            { "force",        HandleForce,         SEC_ADMINISTRATOR, Console::Yes },
            { "pending",      HandlePending,       SEC_ADMINISTRATOR, Console::Yes },
            { "guildcleanup", HandleGuildCleanup,  SEC_ADMINISTRATOR, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "botbrackets", botBracketsTable }
        };
        return commandTable;
    }

    static bool HandleReloadConfig(ChatHandler* handler)
    {
        LoadBotLevelBracketsConfig();
        handler->SendSysMessage("[BotBrackets] Configuration reloaded.");
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        if (!g_BotLevelBracketsEnabled)
        {
            handler->SendSysMessage("[BotBrackets] Module is disabled.");
            return true;
        }

        uint32 totalAlliance = 0, totalHorde = 0;
        std::vector<int> allianceCounts(g_NumRanges, 0);
        std::vector<int> hordeCounts(g_NumRanges, 0);

        const auto& allPlayers = ObjectAccessor::GetPlayers();
        for (const auto& itr : allPlayers)
        {
            Player* player = itr.second;
            if (!player || !player->IsInWorld())
                continue;
            if (!IsBracketPlayerBot(player) || !IsPlayerRandomBot(player))
                continue;
            if (IsBotExcluded(player))
                continue;
            if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
                continue;
            if (g_IgnoreFriendListed && BotInFriendList(player))
                continue;
            if (g_IgnoreArenaTeamBots && BotInArenaTeam(player))
                continue;

            int rangeIndex = GetLevelRangeIndex(player->GetLevel(), player->GetTeamId());
            if (IsAlliancePlayerBot(player))
            {
                totalAlliance++;
                if (rangeIndex >= 0) allianceCounts[rangeIndex]++;
            }
            else if (IsHordePlayerBot(player))
            {
                totalHorde++;
                if (rangeIndex >= 0) hordeCounts[rangeIndex]++;
            }
        }

        handler->PSendSysMessage("[BotBrackets] Status — Alliance: {} bots, Horde: {} bots", totalAlliance, totalHorde);
        handler->PSendSysMessage("{:<4} {:<10} {:<8} {:<8} {:<8} {:<8}",
                                 "Idx", "Range", "A.Des", "A.Act", "H.Des", "H.Act");

        for (int i = 0; i < g_NumRanges; ++i)
        {
            int aDes = (totalAlliance > 0)
                ? static_cast<int>(round((g_AllianceLevelRanges[i].desiredPercent / 100.0) * totalAlliance))
                : 0;
            int hDes = (totalHorde > 0)
                ? static_cast<int>(round((g_HordeLevelRanges[i].desiredPercent / 100.0) * totalHorde))
                : 0;

            handler->PSendSysMessage("{:<4} {:>3}-{:<6} {:<8} {:<8} {:<8} {:<8}",
                                     i + 1,
                                     g_AllianceLevelRanges[i].lower,
                                     g_AllianceLevelRanges[i].upper,
                                     aDes,
                                     allianceCounts[i],
                                     hDes,
                                     hordeCounts[i]);
        }

        handler->PSendSysMessage("[BotBrackets] Pending queue: {} entries.", [&]{ std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex); return g_PendingLevelResets.size(); }());
        return true;
    }

    static bool HandleForce(ChatHandler* handler)
    {
        if (!g_BotLevelBracketsEnabled)
        {
            handler->SendSysMessage("[BotBrackets] Module is disabled.");
            return true;
        }
        handler->SendSysMessage("[BotBrackets] Forcing distribution cycle...");
        RunDistributionCycle(handler);
        handler->SendSysMessage("[BotBrackets] Distribution cycle complete.");
        return true;
    }

    static bool HandlePending(ChatHandler* handler)
    {
        size_t queueSize;
        uint32 oldest = std::numeric_limits<uint32>::max();
        uint32 newest = 0;
        {
            std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
            queueSize = g_PendingLevelResets.size();
            for (const auto& kv : g_PendingLevelResets)
            {
                if (kv.second.enqueuedAt < oldest) oldest = kv.second.enqueuedAt;
                if (kv.second.enqueuedAt > newest) newest = kv.second.enqueuedAt;
            }
        }
        handler->PSendSysMessage("[BotBrackets] Pending queue: {} entries.", queueSize);

        if (g_MaxPendingQueueSize > 0)
            handler->PSendSysMessage("[BotBrackets] Queue cap: {}.", g_MaxPendingQueueSize);
        else
            handler->SendSysMessage("[BotBrackets] Queue cap: unlimited.");

        if (g_PendingQueueTTL > 0)
            handler->PSendSysMessage("[BotBrackets] TTL: {}s per entry.", g_PendingQueueTTL);
        else
            handler->SendSysMessage("[BotBrackets] TTL: disabled.");

        if (queueSize == 0)
        {
            handler->SendSysMessage("[BotBrackets] Queue is empty.");
            return true;
        }

        uint32 now = static_cast<uint32>(time(nullptr));
        handler->PSendSysMessage("[BotBrackets] Oldest entry: {}s ago. Newest: {}s ago.",
                                 now - oldest, now - newest);
        return true;
    }

    static bool HandleGuildCleanup(ChatHandler* handler)
    {
        if (!g_IgnoreGuildBotsWithRealPlayers)
        {
            handler->SendSysMessage("[BotBrackets] Guild bot exclusion is disabled. Cleanup has no effect.");
            return true;
        }

        size_t before = g_PersistentRealPlayerGuildIds.size();
        CleanupGuildTracker();
        size_t after = g_PersistentRealPlayerGuildIds.size();

        handler->PSendSysMessage("[BotBrackets] Guild cleanup complete. Removed {} guild(s). {} guild(s) remain tracked.",
                                 before - after, after);
        return true;
    }
};


// =============================================================================
// ENTRY POINT
// =============================================================================
void Addmod_player_bot_level_bracketsScripts()
{
    new BotLevelBracketsWorldScript();
    new BotLevelBracketsPlayerScript();
    new BotLevelBracketsGuildScript();
    new BotLevelBracketsCommandScript();
}

#include "mod-player-bot-level-brackets-internal.h"

static std::unordered_set<uint32> CollectCurrentRealPlayerGuildIds()
{
    std::unordered_set<uint32> guilds;
    const auto& allPlayers = ObjectAccessor::GetPlayers();
    for (const auto& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
        if (!IsBracketPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
                guilds.insert(guildId);
        }
    }
    return guilds;
}


void LoadSocialFriendList()
{
    g_SocialFriendsList.clear();
    QueryResult result = CharacterDatabase.Query("SELECT friend FROM character_social WHERE flags = 1");

    if (!result || result->GetRowCount() == 0)
        return;

    if (g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] Fetching Social Friend List GUIDs into set");

    do
    {
        uint32 socialFriendGUID = result->Fetch()->Get<uint32>();
        g_SocialFriendsList.insert(static_cast<uint64>(socialFriendGUID));
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.load", "[BotLevelBrackets] Adding GUID {} to Social Friend List", socialFriendGUID);
    } while (result->NextRow());
}


void LoadRealPlayerGuildIds(const std::unordered_map<ObjectGuid, Player*>& players)
{
    g_RealPlayerGuildIds.clear();
    for (const auto& itr : players)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
        if (!IsBracketPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
                g_RealPlayerGuildIds.insert(guildId);
        }
    }
}


void LoadPersistentGuildTracker()
{
    g_PersistentRealPlayerGuildIds.clear();
    QueryResult result = CharacterDatabase.Query("SELECT guild_id FROM bot_level_brackets_guild_tracker WHERE has_real_players = 1");

    if (!result)
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] No guilds with real players found in persistent storage.");
        return;
    }

    if (g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] Loading persistent guild tracker data from database...");

    do
    {
        uint32 guildId = result->Fetch()->Get<uint32>();
        g_PersistentRealPlayerGuildIds.insert(guildId);
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Loaded guild {} as having real players.", guildId);
    } while (result->NextRow());

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Loaded {} guilds with real players from persistent storage.",
                 g_PersistentRealPlayerGuildIds.size());
    }
}


void UpdatePersistentGuildTracker()
{
    if (g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] Starting additive-only persistent guild tracker update...");

    std::unordered_set<uint32> currentRealPlayerGuilds = CollectCurrentRealPlayerGuildIds();

    uint32 processedCount = 0;
    for (uint32 guildId : currentRealPlayerGuilds)
    {
        if (g_PersistentRealPlayerGuildIds.count(guildId) == 0)
        {
            CharacterDatabase.Execute(
                "INSERT INTO bot_level_brackets_guild_tracker (guild_id, has_real_players) VALUES ({}, 1)"
                " ON DUPLICATE KEY UPDATE has_real_players = 1",
                guildId);
            g_PersistentRealPlayerGuildIds.insert(guildId);
            processedCount++;
        }
    }

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.world",
                 "[BotLevelBrackets] Additive guild tracker update complete. {} new guild(s) persisted, {} total tracked.",
                 processedCount, g_PersistentRealPlayerGuildIds.size());
    }
}


void CleanupGuildTracker()
{
    if (g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] Starting guild tracker cleanup...");

    std::unordered_set<uint32> currentRealPlayerGuilds = CollectCurrentRealPlayerGuildIds();

    std::vector<uint32> guildsToRemove;
    for (uint32 trackedGuildId : g_PersistentRealPlayerGuildIds)
        if (currentRealPlayerGuilds.find(trackedGuildId) == currentRealPlayerGuilds.end())
            guildsToRemove.push_back(trackedGuildId);

    uint32 removedCount = 0;
    for (uint32 guildId : guildsToRemove)
    {
        CharacterDatabase.Execute(
            "UPDATE bot_level_brackets_guild_tracker SET has_real_players = 0 WHERE guild_id = {}",
            guildId);
        g_PersistentRealPlayerGuildIds.erase(guildId);
        g_RealPlayerGuildIds.erase(guildId);
        removedCount++;

        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world", "[BotLevelBrackets] Removed guild {} from tracker.", guildId);
    }

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.world", "[BotLevelBrackets] Guild tracker cleanup: {} removed, {} remain.",
                 removedCount, g_PersistentRealPlayerGuildIds.size());
    }
}

#include "mod-player-bot-level-brackets-internal.h"

struct TeleportDestination
{
    uint32 mapId;
    float x, y, z, o;
};

// Default leveling zones per level bracket and faction.
// These are safe starting-town or quest-hub locations appropriate for each range.
static const TeleportDestination s_AllianceZones[] =
{
    { 0,  -8949.95f, -132.49f, 83.53f, 0.0f },   // 1-9:   Elwynn Forest (Goldshire)
    { 0,  -10500.0f, -1157.0f, 27.0f, 0.0f },     // 10-19: Westfall (Sentinel Hill)
    { 0,  -10720.0f, -2480.0f, 14.0f, 0.0f },     // 20-29: Redridge Mountains (Lakeshire)
    { 0,  -3350.0f,  -3090.0f, 28.0f, 0.0f },     // 30-39: STV (Rebel Camp, Northern)
    { 1,  -4400.0f,  2480.0f, 93.0f, 0.0f },      // 40-49: Feralas (Feathermoon)
    { 0,  -10160.0f, -3400.0f, 22.0f, 0.0f },     // 50-59: Burning Steppes
    { 530, -8240.0f, -2700.0f, 134.0f, 0.0f },   // 60-69: Hellfire Peninsula (Alliance base)
    { 530, -3000.0f,  4150.0f, 3.0f, 0.0f },      // 70-79: Zangarmarsh (Telredor)
    { 571,  5800.0f,  500.0f, 660.0f, 0.0f },     // 80:    Borean Tundra (Valiance Keep)
};

static const TeleportDestination s_HordeZones[] =
{
    { 1,  1620.0f,  -4370.0f, 16.0f, 0.0f },      // 1-9:   Durotar (Valley of Trials)
    { 1,  1600.0f,  -4400.0f, 10.0f, 0.0f },      // 10-19: Durotar/Razor Hill
    { 1,  -600.0f,  -4700.0f, 10.0f, 0.0f },      // 20-29: Barrens (Crossroads)
    { 1,  -1600.0f, -3100.0f, 35.0f, 0.0f },      // 30-39: Desolace (Ghost Walker Post)
    { 1,  -3000.0f,  -1400.0f, 12.0f, 0.0f },     // 40-49: Feralas (Camp Mojache)
    { 1,  -1900.0f, -2200.0f, 92.0f, 0.0f },      // 50-59: Un'Goro Crater (entrance)
    { 530, -850.0f,  2700.0f, 48.0f, 0.0f },      // 60-69: Hellfire Peninsula (Horde base)
    { 530, -200.0f,  6850.0f, 21.0f, 0.0f },      // 70-79: Zangarmarsh (Zabra'jin)
    { 571,  2400.0f, -1700.0f, 220.0f, 0.0f },   // 80:    Borean Tundra (Warsong Hold)
};

static constexpr size_t s_NumTeleportZones = 9;


void TeleportBotToLevelZone(Player* bot, uint8 newLevel, uint8 teamID)
{
    if (!bot || !bot->IsInWorld())
        return;

    if (!g_TeleportOnLevelChange)
        return;

    const TeleportDestination* zones = nullptr;
    if (teamID == TEAM_ALLIANCE)
        zones = s_AllianceZones;
    else if (teamID == TEAM_HORDE)
        zones = s_HordeZones;
    else
        return;

    if (s_NumTeleportZones < g_NumRanges)
        return;

    int rangeIndex = GetLevelRangeIndex(newLevel, teamID);
    if (rangeIndex < 0 || rangeIndex >= static_cast<int>(s_NumTeleportZones))
        return;

    const TeleportDestination& dest = zones[rangeIndex];

    if (bot->IsMounted())
        bot->Dismount();

    bot->TeleportTo(dest.mapId, dest.x, dest.y, dest.z, dest.o);

    if (g_BotDistFullDebugMode)
    {
        std::string faction = (teamID == TEAM_ALLIANCE) ? "Alliance" : "Horde";
        LOG_INFO("server.loading",
                 "[BotLevelBrackets] TeleportBotToLevelZone: {} bot '{}' teleported to level zone for range {} (map {}, {:.1f}, {:.1f}, {:.1f}).",
                 faction, bot->GetName(), rangeIndex + 1, dest.mapId, dest.x, dest.y, dest.z);
    }
}


void EnqueuePendingTeleport(Player* bot, uint8 newLevel, uint8 teamID)
{
    if (!bot)
        return;

    g_PendingTeleports[bot->GetGUID()] = PendingTeleportEntry{
        bot->GetGUID(), newLevel, teamID,
        static_cast<uint32>(time(nullptr))
    };

    if (g_BotDistFullDebugMode)
        LOG_INFO("server.loading",
                 "[BotLevelBrackets] EnqueuePendingTeleport: bot '{}' enqueued for teleport to level {} zone.",
                 bot->GetName(), newLevel);
}


void ProcessPendingTeleports()
{
    if (g_PendingTeleports.empty())
        return;

    for (auto it = g_PendingTeleports.begin(); it != g_PendingTeleports.end(); )
    {
        Player* bot = ObjectAccessor::FindPlayer(it->second.botGuid);

        if (!bot)
        {
            it = g_PendingTeleports.erase(it);
            continue;
        }

        if (!bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
        {
            it = g_PendingTeleports.erase(it);
            continue;
        }

        if (bot->IsBeingTeleported())
        {
            ++it;
            continue;
        }

        TeleportBotToLevelZone(bot, it->second.newLevel, it->second.teamID);
        it = g_PendingTeleports.erase(it);
    }
}


void RemoveBotFromPendingTeleports(Player* bot)
{
    if (bot)
        g_PendingTeleports.erase(bot->GetGUID());
}


// =============================================================================
// HUB DISPERSE
// =============================================================================
// Capital cities and hubs where bots congregate. We want to keep a small
// ambiance quota in each and disperse the rest to their leveling zones.
struct HubArea
{
    uint32 mapId;
    float  cx, cy, cz;
    float  radius;
    uint8  teamID;     // TEAM_ALLIANCE or TEAM_HORDE (or both)
};

static const HubArea s_HubAreas[] =
{
    // Stormwind City (Alliance)
    { 0,  -8810.0f,  640.0f,  94.0f, 200.0f, TEAM_ALLIANCE },
    // Orgrimmar (Horde)
    { 1,  1570.0f,  -4400.0f, 8.0f, 200.0f, TEAM_HORDE },
    // Dalaran (Northrend, both factions)
    { 571, 5807.0f,  590.0f,  660.0f, 200.0f, TEAM_ALLIANCE },
    { 571, 5807.0f,  590.0f,  660.0f, 200.0f, TEAM_HORDE },
    // Ironforge (Alliance)
    { 0,  -4980.0f, -940.0f,  501.0f, 150.0f, TEAM_ALLIANCE },
    // Undercity (Horde)
    { 0,  1620.0f,  240.0f,  60.0f, 150.0f, TEAM_HORDE },
    // Darnassus (Alliance)
    { 1,  9950.0f,  2330.0f, 1330.0f, 150.0f, TEAM_ALLIANCE },
    // Thunder Bluff (Horde)
    { 1,  -1290.0f, 150.0f,  130.0f, 150.0f, TEAM_HORDE },
};

static constexpr size_t s_NumHubAreas = sizeof(s_HubAreas) / sizeof(s_HubAreas[0]);


void ProcessHubDisperse()
{
    if (!g_HubDisperseEnabled || !g_TeleportOnLevelChange)
        return;

    uint32 dispersed = 0;
    uint32 now = static_cast<uint32>(time(nullptr));

    for (size_t hubIdx = 0; hubIdx < s_NumHubAreas && dispersed < g_HubDisperseBotsPerCycle; ++hubIdx)
    {
        const HubArea& hub = s_HubAreas[hubIdx];

        std::vector<Player*> botsInHub;
        botsInHub.reserve(20);

        const auto& allPlayers = ObjectAccessor::GetPlayers();
        for (const auto& itr : allPlayers)
        {
            Player* bot = itr.second;
            if (!bot || !bot->IsInWorld())
                continue;
            if (!IsBracketPlayerBot(bot) || !IsPlayerRandomBot(bot))
                continue;
            if (IsBotExcluded(bot))
                continue;
            if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(bot))
                continue;
            if (g_IgnoreFriendListed && BotInFriendList(bot))
                continue;
            if (g_IgnoreArenaTeamBots && BotInArenaTeam(bot))
                continue;
            if (bot->GetTeamId() != hub.teamID)
                continue;
            if (bot->IsBeingTeleported() || bot->IsDuringRemoveFromWorld())
                continue;
            if (!bot->GetSession() || bot->GetSession()->isLogingOut())
                continue;
            if (g_PendingTeleports.count(bot->GetGUID()) > 0)
                continue;

            if (bot->GetMapId() != hub.mapId)
                continue;

            float dx = bot->GetPositionX() - hub.cx;
            float dy = bot->GetPositionY() - hub.cy;
            float dz = bot->GetPositionZ() - hub.cz;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq <= hub.radius * hub.radius)
                botsInHub.push_back(bot);
        }

        if (botsInHub.empty())
            continue;

        uint32 quota = g_HubDisperseMaxBotsPerHub;
        if (botsInHub.size() <= quota)
            continue;

        uint32 toDisperse = static_cast<uint32>(botsInHub.size()) - quota;

        for (uint32 i = 0; i < toDisperse && dispersed < g_HubDisperseBotsPerCycle; ++i)
        {
            Player* bot = botsInHub[i];
            uint8 level = bot->GetLevel();
            uint8 teamID = bot->GetTeamId();

            int rangeIndex = GetLevelRangeIndex(level, teamID);
            if (rangeIndex < 0)
                continue;

            EnqueuePendingTeleport(bot, level, teamID);
            ++dispersed;

            if (g_BotDistFullDebugMode)
                LOG_INFO("server.loading",
                         "[BotLevelBrackets] HubDisperse: bot '{}' (level {}) dispersed from hub {} to leveling zone.",
                         bot->GetName(), level, hubIdx);
        }
    }

    if (dispersed > 0 && g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] HubDisperse: dispersed {} bots this cycle.", dispersed);
}

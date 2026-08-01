#include "mod-player-bot-level-brackets-internal.h"
#include <cmath>

struct TeleportDestination
{
    uint32 mapId;
    float x, y, z, o;
};

// Default leveling zones per level bracket and faction.
// These are safe starting-town or quest-hub locations appropriate for each range.
static const TeleportDestination s_AllianceZones[] =
{
    { 0,  -8949.95f, -132.49f, 83.53f, 0.0f },     // 1-9:   Elwynn Forest (Goldshire)
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
    { 1,  -2985.0f,  -1840.0f, 95.0f, 0.0f },     // 40-49: Feralas (Camp Mojache)
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
        LOG_INFO("server.world",
                 "[BotLevelBrackets] TeleportBotToLevelZone: {} bot '{}' teleported to level zone for range {} (map {}, {:.1f}, {:.1f}, {:.1f}).",
                 faction, bot->GetName(), rangeIndex + 1, dest.mapId, dest.x, dest.y, dest.z);
    }
}


bool EnqueuePendingTeleport(Player* bot, uint8 newLevel, uint8 teamID)
{
    if (!bot)
        return false;

    if (g_PendingTeleports.count(bot->GetGUID()) > 0)
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world",
                     "[BotLevelBrackets] EnqueuePendingTeleport: bot '{}' already has a pending teleport, skipping.",
                     bot->GetName());
        return false;
    }

    PendingTeleportEntry entry;
    entry.botGuid     = bot->GetGUID();
    entry.newLevel    = newLevel;
    entry.teamID      = teamID;
    entry.enqueuedAt  = static_cast<uint32>(time(nullptr));
    entry.useHubDest  = false;
    g_PendingTeleports[bot->GetGUID()] = std::move(entry);

    if (g_BotDistFullDebugMode)
        LOG_INFO("server.world",
                 "[BotLevelBrackets] EnqueuePendingTeleport: bot '{}' enqueued for teleport to level {} zone.",
                 bot->GetName(), newLevel);
    return true;
}

bool EnqueuePendingHubTeleport(Player* bot, uint32 mapId, float x, float y, float z, float o)
{
    if (!bot)
        return false;

    if (g_PendingTeleports.count(bot->GetGUID()) > 0)
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world",
                     "[BotLevelBrackets] EnqueuePendingHubTeleport: bot '{}' already has a pending teleport, skipping.",
                     bot->GetName());
        return false;
    }

    PendingTeleportEntry entry;
    entry.botGuid     = bot->GetGUID();
    entry.newLevel    = bot->GetLevel();
    entry.teamID      = bot->GetTeamId();
    entry.enqueuedAt  = static_cast<uint32>(time(nullptr));
    entry.useHubDest  = true;
    entry.destMapId   = mapId;
    entry.destX       = x;
    entry.destY       = y;
    entry.destZ       = z;
    entry.destO       = o;
    g_PendingTeleports[bot->GetGUID()] = std::move(entry);

    if (g_BotDistFullDebugMode)
        LOG_INFO("server.world",
                 "[BotLevelBrackets] EnqueuePendingHubTeleport: bot '{}' enqueued for teleport to hub (map {}, {:.1f}, {:.1f}, {:.1f}).",
                 bot->GetName(), mapId, x, y, z);
    return true;
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

        if (IsBotInProtectedDuelZone(bot))
        {
            it = g_PendingTeleports.erase(it);
            continue;
        }

        if (bot->IsBeingTeleported())
        {
            ++it;
            continue;
        }

        // Safety check: skip bots that are in combat, dead, in BG, etc.
        // Keep them in the queue to retry next cycle, but enforce a TTL
        // so we don't accumulate stale entries forever.
        if (!IsBotSafeForLevelReset(bot))
        {
            uint32 effectiveTTL = (g_PendingQueueTTL > 0) ? g_PendingQueueTTL : 3600;
            if ((static_cast<uint32>(time(nullptr)) - it->second.enqueuedAt) > effectiveTTL)
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.world",
                             "[BotLevelBrackets] ProcessPendingTeleports: bot '{}' expired (TTL {}s), removing from queue.",
                             bot->GetName(), effectiveTTL);
                it = g_PendingTeleports.erase(it);
            }
            else
            {
                ++it;
            }
            continue;
        }

        if (it->second.useHubDest)
        {
            if (bot->IsMounted())
                bot->Dismount();
            bot->TeleportTo(it->second.destMapId, it->second.destX, it->second.destY, it->second.destZ, it->second.destO);

            if (g_BotDistFullDebugMode)
                LOG_INFO("server.world",
                         "[BotLevelBrackets] ProcessPendingTeleports: bot '{}' teleported to hub (map {}, {:.1f}, {:.1f}, {:.1f}).",
                         bot->GetName(), it->second.destMapId, it->second.destX, it->second.destY, it->second.destZ);
        }
        else
        {
            TeleportBotToLevelZone(bot, it->second.newLevel, it->second.teamID);
        }
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
    uint8  maxLevel;   // only used for starting areas; bots above this level are dispersed
    uint8  minLevel;   // only used for hub areas; bots below this level are dispersed (0 = no min)
};

static const HubArea s_HubAreas[] =
{
    // Stormwind City (Alliance) — all levels welcome
    { 0,  -8810.0f,  640.0f,  94.0f, 200.0f, TEAM_ALLIANCE, 0, 0 },
    // Orgrimmar (Horde) — all levels welcome
    { 1,  1570.0f,  -4400.0f, 8.0f, 200.0f, TEAM_HORDE, 0, 0 },
    // Dalaran (Northrend, both factions) — only levels 68-80
    { 571, 5807.0f,  590.0f,  660.0f, 200.0f, TEAM_ALLIANCE, 80, 68 },
    { 571, 5807.0f,  590.0f,  660.0f, 200.0f, TEAM_HORDE, 80, 68 },
    // Ironforge (Alliance) — all levels welcome
    { 0,  -4980.0f, -940.0f,  501.0f, 150.0f, TEAM_ALLIANCE, 0, 0 },
    // Undercity (Horde) — all levels welcome
    { 0,  1620.0f,  240.0f,  60.0f, 150.0f, TEAM_HORDE, 0, 0 },
    // Darnassus (Alliance) — all levels welcome
    { 1,  9950.0f,  2330.0f, 1330.0f, 150.0f, TEAM_ALLIANCE, 0, 0 },
    // Thunder Bluff (Horde) — all levels welcome
    { 1,  -1290.0f, 150.0f,  130.0f, 150.0f, TEAM_HORDE, 0, 0 },
};

static constexpr size_t s_NumHubAreas = sizeof(s_HubAreas) / sizeof(s_HubAreas[0]);

// Starting zones where bots should never linger — disperse ALL bots found here.
// These are the level 1-10 spawn areas for each race.
static const HubArea s_StartingAreas[] =
{
    // Elwynn Forest (Human start - Northshire Valley)
    { 0,  -8913.0f, -133.0f,  80.0f, 250.0f, TEAM_ALLIANCE, 9, 0 },
    // Dun Morogh (Dwarf/Gnome start - Coldridge Valley)
    { 0,  -6230.0f,  330.0f,  383.0f, 300.0f, TEAM_ALLIANCE, 9, 0 },
    // Teldrassil (Night Elf start - Shadowglen)
    { 1,  10330.0f, 830.0f,  1326.0f, 250.0f, TEAM_ALLIANCE, 9, 0 },
    // Durotar (Orc/Troll start - Valley of Trials)
    { 1,  -620.0f, -4300.0f,  10.0f, 300.0f, TEAM_HORDE, 9, 0 },
    // Mulgore (Tauren start - Red Cloud Mesa)
    { 1,  -2900.0f, -1300.0f, 90.0f, 300.0f, TEAM_HORDE, 9, 0 },
    // Tirisfal Glades (Undead start - Deathknell)
    { 0,  2250.0f,  320.0f,  35.0f, 300.0f, TEAM_HORDE, 9, 0 },
    // Eversong Woods (Blood Elf start - Sunstrider Isle)
    { 530, 8500.0f, -7200.0f, 140.0f, 300.0f, TEAM_HORDE, 9, 0 },
    // Azuremyst Isle (Draenei start - Ammen Vale)
    { 530, -4200.0f, -11500.0f, 120.0f, 300.0f, TEAM_ALLIANCE, 9, 0 },
};

static constexpr size_t s_NumStartingAreas = sizeof(s_StartingAreas) / sizeof(s_StartingAreas[0]);

// Maps where bots of certain level brackets should NOT be.
// If a bot is on one of these maps but its level doesn't match, disperse it.
struct WrongMapRule
{
    uint32 mapId;
    uint8  minLevel;  // inclusive
    uint8  maxLevel;  // inclusive
};

static const WrongMapRule s_WrongMapRules[] =
{
    // Eastern Kingdoms (map 0) — appropriate for levels 1-60 (classic content)
    { 0,   1, 60 },
    // Kalimdor (map 1) — appropriate for levels 1-60 (classic content)
    { 1,   1, 60 },
    // Outland (map 530) — appropriate for levels 58-70
    { 530, 58, 70 },
    // Northrend (map 571) — appropriate for levels 68-80
    { 571, 68, 80 },
};

static constexpr size_t s_NumWrongMapRules = sizeof(s_WrongMapRules) / sizeof(s_WrongMapRules[0]);


static bool IsBotDispersable(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return false;
    if (!IsBracketPlayerBot(bot) || !IsPlayerRandomBot(bot))
        return false;
    if (IsBotExcluded(bot))
        return false;
    if (IsBotInProtectedDuelZone(bot))
        return false;
    if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(bot))
        return false;
    if (g_IgnoreFriendListed && BotInFriendList(bot))
        return false;
    if (g_IgnoreArenaTeamBots && BotInArenaTeam(bot))
        return false;
    if (bot->IsBeingTeleported() || bot->IsDuringRemoveFromWorld())
        return false;
    if (!bot->GetSession() || bot->GetSession()->isLogingOut())
        return false;
    if (g_PendingTeleports.count(bot->GetGUID()) > 0)
        return false;
    if (bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue())
        return false;
    return true;
}


static bool IsInArea(const Player* bot, const HubArea& area)
{
    if (bot->GetMapId() != area.mapId)
        return false;
    if (bot->GetTeamId() != area.teamID)
        return false;

    float dx = bot->GetPositionX() - area.cx;
    if (dx > area.radius || dx < -area.radius)
        return false;
    float dy = bot->GetPositionY() - area.cy;
    if (dy > area.radius || dy < -area.radius)
        return false;
    float dz = bot->GetPositionZ() - area.cz;
    if (dz > area.radius || dz < -area.radius)
        return false;

    return (dx * dx + dy * dy + dz * dz) <= area.radius * area.radius;
}


static bool IsOnWrongMapForLevel(Player* bot)
{
    uint32 mapId = bot->GetMapId();
    uint8  level = bot->GetLevel();

    for (size_t i = 0; i < s_NumWrongMapRules; ++i)
    {
        const WrongMapRule& rule = s_WrongMapRules[i];
        if (mapId == rule.mapId)
        {
            if (level < rule.minLevel || level > rule.maxLevel)
                return true;
        }
    }

    return false;
}


// =============================================================================
// Scheduler 1: Populate & disperse capital city hubs.
// Fills hubs below quota (pulling bots from the wild) and disperses excess
// or wrong-level bots from hubs. Respects per-hub minLevel/maxLevel (e.g.
// Dalaran only accepts levels 68-80).
// =============================================================================
void ProcessHubPopulate()
{
    if (!g_HubDisperseEnabled || !g_TeleportOnLevelChange)
        return;

    uint32 processed = 0;

    struct BotInfo {
        Player* bot;
        int8_t hubIdx;      // -1 if not in any hub
        bool    inStarting;  // true if in any starting area
    };

    const auto& players = ObjectAccessor::GetPlayers();
    std::vector<BotInfo> allBots;
    allBots.reserve(players.size());
    std::vector<uint32> hubCounts(s_NumHubAreas, 0);

    for (const auto& itr : players)
    {
        Player* bot = itr.second;
        if (!IsBotDispersable(bot))
            continue;

        BotInfo info{bot, -1, false};
        for (size_t h = 0; h < s_NumHubAreas; ++h)
        {
            if (IsInArea(bot, s_HubAreas[h]))
            {
                info.hubIdx = static_cast<int8_t>(h);
                ++hubCounts[h];
                break;
            }
        }
        for (size_t s = 0; s < s_NumStartingAreas; ++s)
        {
            if (IsInArea(bot, s_StartingAreas[s]))
            {
                info.inStarting = true;
                break;
            }
        }
        allBots.push_back(info);
    }

    if (allBots.empty())
        return;

    std::unordered_set<ObjectGuid> alreadyProcessed;

    // ---- Part A: Disperse wrong-level and excess bots from hubs. ----
    for (size_t hubIdx = 0; hubIdx < s_NumHubAreas && processed < g_HubDisperseBotsPerCycle; ++hubIdx)
    {
        const HubArea& hub = s_HubAreas[hubIdx];

        std::vector<const BotInfo*> botsInHub;
        botsInHub.reserve(30);

        for (const auto& info : allBots)
        {
            if (info.hubIdx == static_cast<int8_t>(hubIdx))
                botsInHub.push_back(&info);
        }

        if (botsInHub.empty())
            continue;

        // First pass: disperse bots whose level is outside [minLevel, maxLevel].
        for (const BotInfo* info : botsInHub)
        {
            if (processed >= g_HubDisperseBotsPerCycle)
                break;
            if (alreadyProcessed.count(info->bot->GetGUID()) > 0)
                continue;

            uint8 level = info->bot->GetLevel();
            bool wrongLevel = false;
            if (hub.minLevel > 0 && level < hub.minLevel)
                wrongLevel = true;
            if (hub.maxLevel > 0 && level > hub.maxLevel)
                wrongLevel = true;

            if (!wrongLevel)
                continue;

            if (!IsBotSafeForLevelReset(info->bot))
                continue;

            uint8 teamID = info->bot->GetTeamId();
            int rangeIndex = GetLevelRangeIndex(level, teamID);
            if (rangeIndex < 0)
                continue;

            EnqueuePendingTeleport(info->bot, level, teamID);
            alreadyProcessed.insert(info->bot->GetGUID());
            ++processed;

            if (g_BotDistFullDebugMode)
                LOG_INFO("server.world",
                         "[BotLevelBrackets] HubPopulate: bot '{}' (level {}) wrong-level for hub {}, dispersed to leveling zone.",
                         info->bot->GetName(), level, hubIdx);
        }

        // Second pass: if still over quota, disperse excess.
        uint32 quota = g_HubDisperseMaxBotsPerHub;
        uint32 remaining = 0;
        for (const BotInfo* info : botsInHub)
        {
            if (alreadyProcessed.count(info->bot->GetGUID()) > 0)
                continue;
            ++remaining;
        }
        if (remaining <= quota)
            continue;

        uint32 toDisperse = remaining - quota;
        for (const BotInfo* info : botsInHub)
        {
            if (processed >= g_HubDisperseBotsPerCycle)
                break;
            if (toDisperse == 0)
                break;
            if (alreadyProcessed.count(info->bot->GetGUID()) > 0)
                continue;

            if (!IsBotSafeForLevelReset(info->bot))
                continue;

            uint8 level = info->bot->GetLevel();
            uint8 teamID = info->bot->GetTeamId();
            int rangeIndex = GetLevelRangeIndex(level, teamID);
            if (rangeIndex < 0)
                continue;

            EnqueuePendingTeleport(info->bot, level, teamID);
            alreadyProcessed.insert(info->bot->GetGUID());
            ++processed;
            --toDisperse;

            if (g_BotDistFullDebugMode)
                LOG_INFO("server.world",
                         "[BotLevelBrackets] HubPopulate: bot '{}' (level {}) excess in hub {}, dispersed to leveling zone.",
                         info->bot->GetName(), level, hubIdx);
        }
    }

    // ---- Part B: Fill hubs below quota by pulling bots from the wild. ----
    for (size_t hubIdx = 0; hubIdx < s_NumHubAreas && processed < g_HubDisperseBotsPerCycle; ++hubIdx)
    {
        const HubArea& hub = s_HubAreas[hubIdx];

        uint32 currentCount = hubCounts[hubIdx];
        uint32 quota = g_HubDisperseMaxBotsPerHub;
        if (currentCount >= quota)
            continue;

        uint32 needed = quota - currentCount;

        for (const auto& info : allBots)
        {
            if (processed >= g_HubDisperseBotsPerCycle)
                break;
            if (needed == 0)
                break;
            if (alreadyProcessed.count(info.bot->GetGUID()) > 0)
                continue;
            if (info.bot->GetTeamId() != hub.teamID)
                continue;
            if (info.hubIdx >= 0)
                continue;
            if (info.inStarting)
                continue;

            // Respect hub level restrictions (e.g. Dalaran 68-80).
            uint8 level = info.bot->GetLevel();
            if (hub.minLevel > 0 && level < hub.minLevel)
                continue;
            if (hub.maxLevel > 0 && level > hub.maxLevel)
                continue;

            if (!IsBotSafeForLevelReset(info.bot))
                continue;

            float angle  = static_cast<float>(rand_norm()) * 2.0f * static_cast<float>(M_PI);
            float offset = static_cast<float>(rand_norm()) * 30.0f;
            float dx = hub.cx + cosf(angle) * offset;
            float dy = hub.cy + sinf(angle) * offset;

            EnqueuePendingHubTeleport(info.bot, hub.mapId, dx, dy, hub.cz, angle);
            alreadyProcessed.insert(info.bot->GetGUID());
            ++processed;
            --needed;

            if (g_BotDistFullDebugMode)
                LOG_INFO("server.world",
                         "[BotLevelBrackets] HubPopulate: bot '{}' (level {}) pulled INTO hub {} ({}/{}).",
                         info.bot->GetName(), info.bot->GetLevel(), hubIdx, quota - needed, quota);
        }
    }

    if (processed > 0 && g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] HubPopulate: processed {} bots this cycle.", processed);
}


// =============================================================================
// Scheduler 2: Disperse over-leveled bots from starting zones.
// Bots whose level exceeds the starting zone's maxLevel are teleported to
// their correct leveling zone so they can continue questing and progressing.
// =============================================================================
void ProcessStartingZoneDisperse()
{
    if (!g_HubDisperseEnabled || !g_TeleportOnLevelChange)
        return;

    uint32 processed = 0;

    const auto& players = ObjectAccessor::GetPlayers();
    std::vector<Player*> allBots;
    allBots.reserve(players.size());

    for (const auto& itr : players)
    {
        Player* bot = itr.second;
        if (!IsBotDispersable(bot))
            continue;
        allBots.push_back(bot);
    }

    if (allBots.empty())
        return;

    for (Player* bot : allBots)
    {
        if (processed >= g_HubDisperseBotsPerCycle)
            break;

        for (size_t i = 0; i < s_NumStartingAreas; ++i)
        {
            if (!IsInArea(bot, s_StartingAreas[i]))
                continue;

            uint8 level = bot->GetLevel();
            if (level <= s_StartingAreas[i].maxLevel)
                break; // bot is within the correct starting-zone level range

            if (!IsBotSafeForLevelReset(bot))
                break; // not safe to teleport right now; try next cycle

            uint8 teamID = bot->GetTeamId();
            int rangeIndex = GetLevelRangeIndex(level, teamID);
            if (rangeIndex >= 0)
            {
                EnqueuePendingTeleport(bot, level, teamID);
                ++processed;

                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.world",
                             "[BotLevelBrackets] StartingZoneDisperse: bot '{}' (level {}) dispersed from starting area {} to leveling zone.",
                             bot->GetName(), level, i);
            }
            break;
        }
    }

    if (processed > 0 && g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] StartingZoneDisperse: processed {} bots this cycle.", processed);
}


// =============================================================================
// Scheduler 3: Move bots from obsolete zones for their level.
// Bots on a map that doesn't match their level bracket are teleported to the
// correct zone so they can continue progressing.
// =============================================================================
void ProcessWrongMapDisperse()
{
    if (!g_HubDisperseEnabled || !g_TeleportOnLevelChange)
        return;

    uint32 processed = 0;

    const auto& players = ObjectAccessor::GetPlayers();
    std::vector<Player*> allBots;
    allBots.reserve(players.size());

    for (const auto& itr : players)
    {
        Player* bot = itr.second;
        if (!IsBotDispersable(bot))
            continue;
        allBots.push_back(bot);
    }

    if (allBots.empty())
        return;

    for (Player* bot : allBots)
    {
        if (processed >= g_HubDisperseBotsPerCycle)
            break;

        if (!IsOnWrongMapForLevel(bot))
            continue;

        if (!IsBotSafeForLevelReset(bot))
            continue;

        uint8 level = bot->GetLevel();
        uint8 teamID = bot->GetTeamId();
        int rangeIndex = GetLevelRangeIndex(level, teamID);
        if (rangeIndex < 0)
            continue;

        EnqueuePendingTeleport(bot, level, teamID);
        ++processed;

        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world",
                     "[BotLevelBrackets] WrongMapDisperse: bot '{}' (level {}) on wrong map {} dispersed to leveling zone.",
                     bot->GetName(), level, bot->GetMapId());
    }

    if (processed > 0 && g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] WrongMapDisperse: processed {} bots this cycle.", processed);
}

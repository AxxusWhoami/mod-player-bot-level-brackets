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
    { 0,  -8240.0f,  -2700.0f, 134.0f, 0.0f },   // 60-69: Hellfire Peninsula (Alliance base)
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

    if (bot->IsBeingTeleported())
        return;

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

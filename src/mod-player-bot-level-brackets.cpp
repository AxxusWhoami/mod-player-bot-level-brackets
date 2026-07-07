#include "ScriptMgr.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "Configuration/Config.h"
#include "AutoMaintenanceOnLevelupAction.h"
#include "Common.h"
#include "Guild.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <ctime>
#include <utility>
#include <limits>
#include <algorithm>
#include "PlayerbotFactory.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include <string>
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "ArenaTeamMgr.h"

using namespace Acore::ChatCommands;

// Forward declarations.
static bool IsPlayerBot(Player* player);
static bool IsAlliancePlayerBot(Player* bot);
static bool IsHordePlayerBot(Player* bot);
static void ClampAndBalanceBrackets();

// -----------------------------------------------------------------------------
// LEVEL RANGE CONFIGURATION
// -----------------------------------------------------------------------------
// Same boundaries for both factions; only desired percentages differ.
struct LevelRangeConfig
{
    uint8 lower;          ///< Lower bound (inclusive)
    uint8 upper;          ///< Upper bound (inclusive)
    uint8 desiredPercent; ///< Desired percentage of bots in this range
};

// Instead of a fixed constant, load the number of brackets from configuration.
static uint8 g_NumRanges = 9;

// Global variables to restrict bot levels.
static uint8 g_RandomBotMinLevel = 1;
static uint8 g_RandomBotMaxLevel = 80;

// Enable/disable the mod. Default is true.
static bool g_BotLevelBracketsEnabled = true;
// Ignore bots in guilds with a real player. Default is true.
static bool g_IgnoreGuildBotsWithRealPlayers = true;
// Ignore bots in arena teams. Default is true.
static bool g_IgnoreArenaTeamBots = true;

// Use vectors to store the level ranges.
static std::vector<LevelRangeConfig> g_AllianceLevelRanges;
static std::vector<LevelRangeConfig> g_HordeLevelRanges;

static uint32 g_BotDistCheckFrequency        = 300;  // in seconds
static uint32 g_BotDistFlaggedCheckFrequency = 15;   // in seconds
static uint32 g_GuildTrackerUpdateFrequency  = 600;  // in seconds (10 minutes)
static uint32 g_SocialListRefreshFrequency   = 300;  // in seconds
static bool   g_BotDistFullDebugMode         = false;
static bool   g_BotDistLiteDebugMode         = false;
static bool   g_UseDynamicDistribution       = false;
static bool   g_IgnoreFriendListed           = true;
static uint32 g_FlaggedProcessLimit          = 5;    // 0 = unlimited
static uint32 g_MaxPendingQueueSize          = 0;    // 0 = unlimited
static uint32 g_PendingQueueTTL              = 0;    // 0 = no TTL, in seconds

// Real player weight to boost bracket contributions.
static float g_RealPlayerWeight = 1.0f;

// If true, synchronize bracket logic and real player influence across both factions.
static bool g_SyncFactions = false;

// Set for character social list friends (unordered for O(1) lookup).
static std::unordered_set<uint64> g_SocialFriendsList;

// Array for excluded bot names.
static std::vector<std::string> g_ExcludeBotNames;

// Set of guild IDs that have online real players (rebuilt each cycle).
static std::unordered_set<uint32> g_RealPlayerGuildIds;

// Persistent guild tracker - stores guild IDs that have real players (from database).
static std::unordered_set<uint32> g_PersistentRealPlayerGuildIds;

struct PendingResetEntry
{
    ObjectGuid              botGuid;
    int                     targetRange;
    const LevelRangeConfig* factionRanges;
    uint32                  enqueuedAt; ///< Unix timestamp when the entry was added
};

// Keyed by ObjectGuid for O(1) duplicate detection and lookup.
static std::unordered_map<ObjectGuid, PendingResetEntry> g_PendingLevelResets;


/**
 * @brief Loads and initializes the configuration for player bot level brackets.
 *
 * Reads all configuration options from the server config manager. Validates
 * NumRanges bounds, detects overlapping bracket definitions, warns when
 * percentage sums are zero, and warns when FlaggedProcessLimit is very low.
 * Calls ClampAndBalanceBrackets() after loading.
 */
static void LoadBotLevelBracketsConfig()
{
    g_BotLevelBracketsEnabled           = sConfigMgr->GetOption<bool>("BotLevelBrackets.Enabled", true);
    g_IgnoreGuildBotsWithRealPlayers     = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreGuildBotsWithRealPlayers", true);
    g_IgnoreArenaTeamBots               = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreArenaTeamBots", true);

    g_BotDistFullDebugMode              = sConfigMgr->GetOption<bool>("BotLevelBrackets.FullDebugMode", false);
    g_BotDistLiteDebugMode              = sConfigMgr->GetOption<bool>("BotLevelBrackets.LiteDebugMode", false);
    g_BotDistCheckFrequency             = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFrequency", 300);
    g_BotDistFlaggedCheckFrequency      = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFlaggedFrequency", 15);
    g_GuildTrackerUpdateFrequency       = sConfigMgr->GetOption<uint32>("BotLevelBrackets.GuildTrackerUpdateFrequency", 600);
    g_SocialListRefreshFrequency        = sConfigMgr->GetOption<uint32>("BotLevelBrackets.SocialListRefreshFrequency", 300);
    g_UseDynamicDistribution            = sConfigMgr->GetOption<bool>("BotLevelBrackets.Dynamic.UseDynamicDistribution", false);
    g_RealPlayerWeight                  = sConfigMgr->GetOption<float>("BotLevelBrackets.Dynamic.RealPlayerWeight", 1.0f);
    g_SyncFactions                      = sConfigMgr->GetOption<bool>("BotLevelBrackets.Dynamic.SyncFactions", false);
    g_IgnoreFriendListed                = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreFriendListed", true);
    g_FlaggedProcessLimit               = sConfigMgr->GetOption<uint32>("BotLevelBrackets.FlaggedProcessLimit", 5);
    g_MaxPendingQueueSize               = sConfigMgr->GetOption<uint32>("BotLevelBrackets.MaxPendingQueueSize", 0);
    g_PendingQueueTTL                   = sConfigMgr->GetOption<uint32>("BotLevelBrackets.PendingQueueTTL", 0);

    std::string excludeNames = sConfigMgr->GetOption<std::string>("BotLevelBrackets.ExcludeNames", "");
    g_ExcludeBotNames.clear();
    std::istringstream f(excludeNames);
    std::string s;
    while (getline(f, s, ','))
    {
        s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
        if (!s.empty())
            g_ExcludeBotNames.push_back(s);
    }

    g_RandomBotMinLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMinLevel", 1));
    g_RandomBotMaxLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMaxLevel", 80));

    // --- NumRanges validation ---
    uint32 numRangesRaw = sConfigMgr->GetOption<uint32>("BotLevelBrackets.NumRanges", 9);
    if (numRangesRaw == 0)
    {
        LOG_ERROR("server.loading", "[BotLevelBrackets] BotLevelBrackets.NumRanges is 0, which is invalid. Defaulting to 1.");
        numRangesRaw = 1;
    }
    else if (numRangesRaw > 20)
    {
        LOG_WARN("server.loading", "[BotLevelBrackets] BotLevelBrackets.NumRanges is {} which is unusually high and may affect performance.", numRangesRaw);
    }
    g_NumRanges = static_cast<uint8>(numRangesRaw);
    g_AllianceLevelRanges.resize(g_NumRanges);
    g_HordeLevelRanges.resize(g_NumRanges);

    // --- FlaggedProcessLimit warning ---
    if (g_FlaggedProcessLimit > 0 && g_FlaggedProcessLimit < 3)
    {
        LOG_WARN("server.loading",
                 "[BotLevelBrackets] BotLevelBrackets.FlaggedProcessLimit is {} which is very low. "
                 "The pending queue may grow faster than it is processed. Consider a value >= 3.",
                 g_FlaggedProcessLimit);
    }

    // Load Alliance configuration.
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        std::string idx = std::to_string(i + 1);
        g_AllianceLevelRanges[i].lower          = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range" + idx + ".Lower", (i == 0 ? 1 : i * 10)));
        g_AllianceLevelRanges[i].upper          = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range" + idx + ".Upper", (i < g_NumRanges - 1 ? i * 10 + 9 : g_RandomBotMaxLevel)));
        g_AllianceLevelRanges[i].desiredPercent = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range" + idx + ".Pct", 11));
    }

    // Load Horde configuration.
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        std::string idx = std::to_string(i + 1);
        g_HordeLevelRanges[i].lower          = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range" + idx + ".Lower", (i == 0 ? 1 : i * 10)));
        g_HordeLevelRanges[i].upper          = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range" + idx + ".Upper", (i < g_NumRanges - 1 ? i * 10 + 9 : g_RandomBotMaxLevel)));
        g_HordeLevelRanges[i].desiredPercent = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range" + idx + ".Pct", 11));
    }

    // --- Overlap detection ---
    for (uint8 i = 0; i + 1 < g_NumRanges; ++i)
    {
        if (g_AllianceLevelRanges[i].upper >= g_AllianceLevelRanges[i + 1].lower)
        {
            LOG_WARN("server.loading",
                     "[BotLevelBrackets] Alliance bracket overlap: Range{} ({}-{}) overlaps Range{} ({}-{}). "
                     "Adjust Lower/Upper values so ranges do not overlap.",
                     i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                     i + 2, g_AllianceLevelRanges[i + 1].lower, g_AllianceLevelRanges[i + 1].upper);
        }
        if (g_HordeLevelRanges[i].upper >= g_HordeLevelRanges[i + 1].lower)
        {
            LOG_WARN("server.loading",
                     "[BotLevelBrackets] Horde bracket overlap: Range{} ({}-{}) overlaps Range{} ({}-{}). "
                     "Adjust Lower/Upper values so ranges do not overlap.",
                     i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper,
                     i + 2, g_HordeLevelRanges[i + 1].lower, g_HordeLevelRanges[i + 1].upper);
        }
    }

    // If SyncFactions is enabled, bracket definitions must match exactly.
    if (g_SyncFactions)
    {
        for (uint8 i = 0; i < g_NumRanges; ++i)
        {
            if (g_AllianceLevelRanges[i].lower != g_HordeLevelRanges[i].lower ||
                g_AllianceLevelRanges[i].upper != g_HordeLevelRanges[i].upper)
            {
                LOG_ERROR("server.loading",
                          "[BotLevelBrackets] FATAL: Bracket mismatch at index {} when SyncFactions is enabled. "
                          "Alliance: {}-{}, Horde: {}-{}. Both must match exactly.",
                          i, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                          g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper);
                std::terminate();
            }
        }
    }

    ClampAndBalanceBrackets();

    // --- Zero-sum warning (after clamping) ---
    uint32 totalAlliance = 0, totalHorde = 0;
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        totalAlliance += g_AllianceLevelRanges[i].desiredPercent;
        totalHorde    += g_HordeLevelRanges[i].desiredPercent;
    }
    if (totalAlliance == 0)
        LOG_WARN("server.loading", "[BotLevelBrackets] Alliance bracket percentages sum to 0. No Alliance bots will be redistributed.");
    if (totalHorde == 0)
        LOG_WARN("server.loading", "[BotLevelBrackets] Horde bracket percentages sum to 0. No Horde bots will be redistributed.");
}


// -----------------------------------------------------------------------------
// BOT DETECTION HELPERS
// -----------------------------------------------------------------------------
static bool IsPlayerBot(Player* player)
{
    if (!player)
        return false;
    PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
    return botAI && botAI->IsBotAI();
}


static bool IsPlayerRandomBot(Player* player)
{
    if (!player)
        return false;
    return sRandomPlayerbotMgr.IsRandomBot(player);
}


static bool IsAlliancePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_ALLIANCE);
}


static bool IsHordePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_HORDE);
}


/**
 * @brief Removes a bot from the pending level resets map in O(1).
 */
static void RemoveBotFromPendingResets(Player* bot)
{
    g_PendingLevelResets.erase(bot->GetGUID());
}


/**
 * @brief Loads the social friend list from the database into an unordered_set for O(1) lookup.
 */
static void LoadSocialFriendList()
{
    g_SocialFriendsList.clear();
    QueryResult result = CharacterDatabase.Query("SELECT friend FROM character_social WHERE flags = 1");

    if (!result || result->GetRowCount() == 0)
        return;

    if (g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] Fetching Social Friend List GUIDs into set");

    do
    {
        uint32 socialFriendGUID = result->Fetch()->Get<uint32>();
        g_SocialFriendsList.insert(static_cast<uint64>(socialFriendGUID));
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.load", "[BotLevelBrackets] Adding GUID {} to Social Friend List", socialFriendGUID);
    } while (result->NextRow());
}


/**
 * @brief Loads the persistent guild tracker data from the database.
 */
static void LoadPersistentGuildTracker()
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


/**
 * @brief Additive-only update of the persistent guild tracker. Adds guilds with
 *        currently online real players; never removes guilds from the tracker.
 */
static void UpdatePersistentGuildTracker()
{
    if (g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] Starting additive-only persistent guild tracker update...");

    std::unordered_set<uint32> currentRealPlayerGuilds;
    const auto& allPlayers = ObjectAccessor::GetPlayers();
    for (const auto& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
        if (!IsPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
                currentRealPlayerGuilds.insert(guildId);
        }
    }

    uint32 addedCount = 0;
    for (uint32 guildId : currentRealPlayerGuilds)
    {
        CharacterDatabase.Execute(
            "REPLACE INTO bot_level_brackets_guild_tracker (guild_id, has_real_players) VALUES ({}, 1)",
            guildId);
        g_PersistentRealPlayerGuildIds.insert(guildId);
        addedCount++;
    }

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.loading",
                 "[BotLevelBrackets] Additive guild tracker update complete. {} guilds processed, {} total tracked.",
                 addedCount, g_PersistentRealPlayerGuildIds.size());
    }
}


/**
 * @brief Removes guilds from the tracker that have no online real players.
 *        Should be called manually or via command, not automatically on logout.
 */
static void CleanupGuildTracker()
{
    if (g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] Starting guild tracker cleanup...");

    std::unordered_set<uint32> currentRealPlayerGuilds;
    const auto& allPlayers = ObjectAccessor::GetPlayers();
    for (const auto& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
        if (!IsPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
                currentRealPlayerGuilds.insert(guildId);
        }
    }

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
            LOG_INFO("server.loading", "[BotLevelBrackets] Removed guild {} from tracker.", guildId);
    }

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Guild tracker cleanup: {} removed, {} remain.",
                 removedCount, g_PersistentRealPlayerGuildIds.size());
    }
}


/**
 * @brief Rebuilds the in-memory set of guild IDs with online real players.
 */
static void LoadRealPlayerGuildIds(const std::unordered_map<ObjectGuid, Player*>& players)
{
    g_RealPlayerGuildIds.clear();
    for (const auto& itr : players)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
        if (!IsPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
                g_RealPlayerGuildIds.insert(guildId);
        }
    }
}


/**
 * @brief Returns the bracket index for a given level and team, or -1 if out of range.
 */
static int GetLevelRangeIndex(uint8 level, uint8 teamID)
{
    if (level < g_RandomBotMinLevel || level > g_RandomBotMaxLevel)
        return -1;

    if (teamID == TEAM_ALLIANCE)
    {
        for (uint8 i = 0; i < g_NumRanges; ++i)
            if (level >= g_AllianceLevelRanges[i].lower && level <= g_AllianceLevelRanges[i].upper)
                return i;
    }
    else if (teamID == TEAM_HORDE)
    {
        for (uint8 i = 0; i < g_NumRanges; ++i)
            if (level >= g_HordeLevelRanges[i].lower && level <= g_HordeLevelRanges[i].upper)
                return i;
    }

    return -1;
}


static uint8 GetRandomLevelInRange(const LevelRangeConfig& range)
{
    return urand(range.lower, range.upper);
}


/**
 * @brief Randomizes a bot's level to a random value within the specified bracket.
 *        Death Knights are never assigned a level below 55.
 */
static void AdjustBotToRange(Player* bot, int targetRangeIndex, const LevelRangeConfig* factionRanges)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
        return;

    if (targetRangeIndex < 0 || targetRangeIndex >= g_NumRanges)
        return;

    if (bot->IsMounted())
        bot->Dismount();

    uint8 botOriginalLevel = bot->GetLevel();
    uint8 newLevel = 0;

    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        uint8 lowerBound = factionRanges[targetRangeIndex].lower;
        uint8 upperBound = factionRanges[targetRangeIndex].upper;
        if (upperBound < 55)
        {
            if (g_BotDistFullDebugMode)
            {
                std::string playerFaction = IsAlliancePlayerBot(bot) ? "Alliance" : "Horde";
                LOG_INFO("server.loading",
                         "[BotLevelBrackets] AdjustBotToRange: Cannot assign {} DK '{}' ({}) to range {}-{} (below 55).",
                         playerFaction, bot->GetName(), botOriginalLevel, lowerBound, upperBound);
            }
            return;
        }
        if (lowerBound < 55)
            lowerBound = 55;
        if (lowerBound > upperBound)
            return;
        newLevel = urand(lowerBound, upperBound);
    }
    else
    {
        const LevelRangeConfig& range = factionRanges[targetRangeIndex];
        if (range.lower > range.upper)
        {
            if (g_BotDistFullDebugMode)
            {
                std::string playerFaction = IsAlliancePlayerBot(bot) ? "Alliance" : "Horde";
                LOG_INFO("server.loading",
                         "[BotLevelBrackets] AdjustBotToRange: Invalid range {}-{} for {} bot '{}'.",
                         range.lower, range.upper, playerFaction, bot->GetName());
            }
            return;
        }
        newLevel = GetRandomLevelInRange(range);
    }

    PlayerbotFactory newFactory(bot, newLevel);
    newFactory.Randomize(false);

    // Force reset talents if equipment and spec persistence is enabled and bot rolled to max level.
    if (newLevel == g_RandomBotMaxLevel && sPlayerbotAIConfig.equipAndSpecPersistence)
    {
        PlayerbotFactory tempFactory(bot, newLevel);
        tempFactory.InitTalentsTree(false, true, true);
    }

    if (g_BotDistFullDebugMode)
    {
        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        std::string playerClassName = botAI ? botAI->GetChatHelper()->FormatClass(bot->getClass()) : "Unknown";
        std::string playerFaction = IsAlliancePlayerBot(bot) ? "Alliance" : "Horde";
        LOG_INFO("server.loading",
                 "[BotLevelBrackets] AdjustBotToRange: {} Bot '{}' - {} ({}) -> level {} (range {}-{}).",
                 playerFaction, bot->GetName(), playerClassName.c_str(), botOriginalLevel, newLevel,
                 factionRanges[targetRangeIndex].lower, factionRanges[targetRangeIndex].upper);
    }

    ChatHandler(bot->GetSession()).SendSysMessage("[mod-bot-level-brackets] Your level has been reset.");
}


/**
 * @brief Returns true if the bot is in a guild that has at least one real player
 *        (checked against both the online cache and the persistent DB cache).
 */
static bool BotInGuildWithRealPlayer(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
        return false;
    uint32 guildId = bot->GetGuildId();
    if (guildId == 0)
        return false;
    return g_RealPlayerGuildIds.count(guildId) > 0 || g_PersistentRealPlayerGuildIds.count(guildId) > 0;
}


/**
 * @brief Returns true if the bot's GUID is present in any real player's friend list (O(1)).
 */
static bool BotInFriendList(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
        return false;

    bool found = g_SocialFriendsList.count(bot->GetGUID().GetRawValue()) > 0;
    if (found && g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is on a Real Player's friends list",
                 bot->GetName(), bot->GetLevel());
    return found;
}


/**
 * @brief Returns true if the bot is a member of any arena team.
 */
static bool BotInArenaTeam(Player* bot)
{
    if (!bot)
        return false;
    for (uint32 slot = 0; slot < MAX_ARENA_SLOT; ++slot)
        if (sArenaTeamMgr->GetArenaTeamById(bot->GetArenaTeamId(slot)))
            return true;
    return false;
}


/**
 * @brief Clamps bracket bounds to [g_RandomBotMinLevel, g_RandomBotMaxLevel] and
 *        auto-adjusts percentage sums to 100 for each faction.
 */
static void ClampAndBalanceBrackets()
{
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        if (g_AllianceLevelRanges[i].lower < g_RandomBotMinLevel)
            g_AllianceLevelRanges[i].lower = g_RandomBotMinLevel;
        if (g_AllianceLevelRanges[i].upper > g_RandomBotMaxLevel)
            g_AllianceLevelRanges[i].upper = g_RandomBotMaxLevel;
        if (g_AllianceLevelRanges[i].lower > g_AllianceLevelRanges[i].upper)
            g_AllianceLevelRanges[i].desiredPercent = 0;
    }
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        if (g_HordeLevelRanges[i].lower < g_RandomBotMinLevel)
            g_HordeLevelRanges[i].lower = g_RandomBotMinLevel;
        if (g_HordeLevelRanges[i].upper > g_RandomBotMaxLevel)
            g_HordeLevelRanges[i].upper = g_RandomBotMaxLevel;
        if (g_HordeLevelRanges[i].lower > g_HordeLevelRanges[i].upper)
            g_HordeLevelRanges[i].desiredPercent = 0;
    }

    uint32 totalAlliance = 0, totalHorde = 0;
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        totalAlliance += g_AllianceLevelRanges[i].desiredPercent;
        totalHorde    += g_HordeLevelRanges[i].desiredPercent;
    }

    if (totalAlliance != 100 && totalAlliance > 0)
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Alliance pct sum is {} (expected 100). Auto adjusting.", totalAlliance);
        int missing = 100 - totalAlliance;
        while (missing > 0)
            for (uint8 i = 0; i < g_NumRanges && missing > 0; ++i)
                if (g_AllianceLevelRanges[i].lower <= g_AllianceLevelRanges[i].upper && g_AllianceLevelRanges[i].desiredPercent > 0)
                { g_AllianceLevelRanges[i].desiredPercent++; missing--; }
    }

    if (totalHorde != 100 && totalHorde > 0)
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Horde pct sum is {} (expected 100). Auto adjusting.", totalHorde);
        int missing = 100 - totalHorde;
        while (missing > 0)
            for (uint8 i = 0; i < g_NumRanges && missing > 0; ++i)
                if (g_HordeLevelRanges[i].lower <= g_HordeLevelRanges[i].upper && g_HordeLevelRanges[i].desiredPercent > 0)
                { g_HordeLevelRanges[i].desiredPercent++; missing--; }
    }
}


/**
 * @brief Returns true if the bot is in a safe state to perform a level reset.
 */
static bool IsBotSafeForLevelReset(Player* bot)
{
    if (!bot || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Null or invalid bot pointer.");
        return false;
    }
    if (!bot->IsInWorld())
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is not in world.", bot->GetName(), bot->GetLevel());
        return false;
    }
    if (!bot->IsAlive())
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is not alive.", bot->GetName(), bot->GetLevel());
        return false;
    }
    if (bot->IsInCombat())
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is in combat.", bot->GetName(), bot->GetLevel());
        return false;
    }
    if (bot->InBattleground() || bot->InArena() || bot->inRandomLfgDungeon() || bot->InBattlegroundQueue())
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is in BG/arena/dungeon/queue.", bot->GetName(), bot->GetLevel());
        return false;
    }
    if (bot->IsInFlight())
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is in flight.", bot->GetName(), bot->GetLevel());
        return false;
    }
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsInWorld() && !IsPlayerBot(member))
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} has non-bot group member {}.", bot->GetName(), member->GetName());
                return false;
            }
        }
    }
    return true;
}


/**
 * @brief Returns true if the bot's name matches an entry in the exclusion list.
 */
static bool IsBotExcluded(Player* bot)
{
    if (!bot)
        return false;
    const std::string& name = bot->GetName();
    for (const auto& excluded : g_ExcludeBotNames)
        if (excluded == name)
            return true;
    return false;
}


/**
 * @brief Adds a bot to the pending reset queue respecting MaxPendingQueueSize.
 *        Uses unordered_map::emplace so duplicates are silently ignored (O(1)).
 */
static void EnqueuePendingReset(ObjectGuid guid, int targetRange, const LevelRangeConfig* factionRanges)
{
    if (g_MaxPendingQueueSize > 0 && g_PendingLevelResets.size() >= g_MaxPendingQueueSize)
    {
        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_WARN("server.loading",
                     "[BotLevelBrackets] Pending queue is full ({}/{}). Bot {} not enqueued.",
                     g_PendingLevelResets.size(), g_MaxPendingQueueSize, guid.ToString());
        }
        return;
    }
    uint32 now = static_cast<uint32>(time(nullptr));
    g_PendingLevelResets.emplace(guid, PendingResetEntry{guid, targetRange, factionRanges, now});
}


/**
 * @brief Processes pending level resets. Respects FlaggedProcessLimit per cycle
 *        and drops entries that have exceeded PendingQueueTTL.
 */
static void ProcessPendingLevelResets()
{
    if (g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] Processing {} pending resets...", g_PendingLevelResets.size());

    if (g_PendingLevelResets.empty())
        return;

    uint32 now = static_cast<uint32>(time(nullptr));
    uint32 processed = 0;

    for (auto it = g_PendingLevelResets.begin(); it != g_PendingLevelResets.end(); )
    {
        if (g_FlaggedProcessLimit > 0 && processed >= g_FlaggedProcessLimit)
            break;

        // TTL check: drop stale entries.
        if (g_PendingQueueTTL > 0 && (now - it->second.enqueuedAt) > g_PendingQueueTTL)
        {
            if (g_BotDistFullDebugMode)
                LOG_INFO("server.loading", "[BotLevelBrackets] Pending entry for {} expired (TTL {}s). Dropping.",
                         it->first.ToString(), g_PendingQueueTTL);
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        Player* bot = ObjectAccessor::FindPlayer(it->second.botGuid);

        if (!bot)
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        if (!bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        if (IsBotExcluded(bot))
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        int targetRange = it->second.targetRange;

        if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(bot))
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        if (g_IgnoreFriendListed && BotInFriendList(bot))
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        if (g_IgnoreArenaTeamBots && BotInArenaTeam(bot))
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        if (Group* group = bot->GetGroup())
        {
            bool hasRealPlayer = false;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member->IsInWorld() && !IsPlayerBot(member))
                {
                    hasRealPlayer = true;
                    break;
                }
            }
            if (hasRealPlayer)
            {
                it = g_PendingLevelResets.erase(it);
                continue;
            }
        }

        if (IsBotSafeForLevelReset(bot))
        {
            AdjustBotToRange(bot, targetRange, it->second.factionRanges);
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Bot '{}' successfully reset to level range {}-{}.",
                         bot->GetName(),
                         it->second.factionRanges[targetRange].lower,
                         it->second.factionRanges[targetRange].upper);
            }
            it = g_PendingLevelResets.erase(it);
            ++processed;
        }
        else
        {
            ++it;
        }
    }
}


/**
 * @brief Determines the bracket index for a player, or enqueues them for a reset
 *        to the closest bracket if they fall outside all defined ranges.
 */
static int GetOrFlagPlayerBracket(Player* player)
{
    if (IsPlayerBot(player) && IsBotExcluded(player))
        return -1;

    if (IsPlayerBot(player) && g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
        return -1;

    if (IsPlayerBot(player) && g_IgnoreArenaTeamBots && BotInArenaTeam(player))
        return -1;

    if (IsPlayerBot(player))
    {
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member->IsInWorld() && !IsPlayerBot(member))
                {
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.loading",
                                 "[BotLevelBrackets] GetOrFlagPlayerBracket: Bot {} (Level {}) is in group with real player {} - excluded.",
                                 player->GetName(), player->GetLevel(), member->GetName());
                    return -1;
                }
            }
        }
    }

    int rangeIndex = GetLevelRangeIndex(player->GetLevel(), player->GetTeamId());
    if (rangeIndex >= 0)
        return rangeIndex;

    LevelRangeConfig* factionRanges = nullptr;
    if (IsAlliancePlayerBot(player))
        factionRanges = g_AllianceLevelRanges.data();
    else if (IsHordePlayerBot(player))
        factionRanges = g_HordeLevelRanges.data();
    else
        return -1;

    int targetRange = -1;
    int smallestDiff = std::numeric_limits<int>::max();
    for (int i = 0; i < g_NumRanges; ++i)
    {
        if (factionRanges[i].lower > factionRanges[i].upper)
            continue;
        if (player->getClass() == CLASS_DEATH_KNIGHT && factionRanges[i].upper < 55)
            continue;

        int diff = 0;
        if (player->GetLevel() < factionRanges[i].lower)
            diff = factionRanges[i].lower - player->GetLevel();
        else if (player->GetLevel() > factionRanges[i].upper)
            diff = player->GetLevel() - factionRanges[i].upper;

        if (diff < smallestDiff)
        {
            smallestDiff = diff;
            targetRange = i;
        }
    }

    if (targetRange >= 0)
        EnqueuePendingReset(player->GetGUID(), targetRange, factionRanges);

    return -1;
}


// -----------------------------------------------------------------------------
// WORLD SCRIPT: Bot Level Distribution with Faction Separation
// -----------------------------------------------------------------------------
class BotLevelBracketsWorldScript : public WorldScript
{
public:
    BotLevelBracketsWorldScript()
        : WorldScript("BotLevelBracketsWorldScript"),
          m_timer(0), m_flaggedTimer(0), m_guildTrackerTimer(0), m_socialListTimer(0)
    { }

    void OnStartup() override
    {
        LoadBotLevelBracketsConfig();
        LoadSocialFriendList();
        LoadPersistentGuildTracker();

        if (!g_BotLevelBracketsEnabled)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module disabled via configuration.");
            return;
        }

        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading",
                     "[BotLevelBrackets] Module loaded. Check: {}s, Flagged: {}s, Social: {}s, GuildTracker: {}s.",
                     g_BotDistCheckFrequency, g_BotDistFlaggedCheckFrequency,
                     g_SocialListRefreshFrequency, g_GuildTrackerUpdateFrequency);
            for (uint8 i = 0; i < g_NumRanges; ++i)
                LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {}: {}-{}, Desired Percentage: {}%",
                         i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                         g_AllianceLevelRanges[i].desiredPercent);
            for (uint8 i = 0; i < g_NumRanges; ++i)
                LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {}: {}-{}, Desired Percentage: {}%",
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

        // Process pending resets on the fast timer.
        if (m_flaggedTimer >= g_BotDistFlaggedCheckFrequency * 1000)
        {
            if (g_BotDistFullDebugMode)
                LOG_INFO("server.loading", "[BotLevelBrackets] Pending Level Resets Triggering.");
            ProcessPendingLevelResets();
            m_flaggedTimer = 0;
        }

        // Update guild tracker on its own timer.
        if (m_guildTrackerTimer >= g_GuildTrackerUpdateFrequency * 1000)
        {
            if (g_BotDistFullDebugMode)
                LOG_INFO("server.loading", "[BotLevelBrackets] Guild Tracker Update Triggering.");
            UpdatePersistentGuildTracker();
            m_guildTrackerTimer = 0;
        }

        // Refresh social friend list on its own independent timer.
        if (m_socialListTimer >= g_SocialListRefreshFrequency * 1000)
        {
            LoadSocialFriendList();
            m_socialListTimer = 0;
        }

        if (m_timer < g_BotDistCheckFrequency * 1000)
            return;
        m_timer = 0;

        const auto& allPlayers = ObjectAccessor::GetPlayers();

        LoadRealPlayerGuildIds(allPlayers);

        if (g_UseDynamicDistribution)
        {
            std::vector<int> allianceRealCounts(g_NumRanges, 0);
            std::vector<int> hordeRealCounts(g_NumRanges, 0);
            uint32 totalAllianceReal = 0;
            uint32 totalHordeReal = 0;

            for (auto const& itr : allPlayers)
            {
                Player* player = itr.second;
                if (!player || !player->IsInWorld())
                    continue;
                if (IsPlayerBot(player))
                    continue;
                int rangeIndex = GetOrFlagPlayerBracket(player);
                if (rangeIndex < 0)
                    continue;
                if (player->GetTeamId() == TEAM_ALLIANCE)
                { allianceRealCounts[rangeIndex]++; totalAllianceReal++; }
                else if (player->GetTeamId() == TEAM_HORDE)
                { hordeRealCounts[rangeIndex]++; totalHordeReal++; }
            }

            const float baseline = 1.0f;
            std::vector<float> allianceWeights(g_NumRanges, 0.0f);
            std::vector<float> hordeWeights(g_NumRanges, 0.0f);

            if (g_SyncFactions)
            {
                uint32 totalCombinedReal = totalAllianceReal + totalHordeReal;
                for (int i = 0; i < g_NumRanges; ++i)
                {
                    int combinedReal = allianceRealCounts[i] + hordeRealCounts[i];
                    float weight = baseline + g_RealPlayerWeight *
                        (totalCombinedReal > 0 ? (1.0f / float(totalCombinedReal)) : 1.0f) *
                        log(1 + combinedReal);
                    allianceWeights[i] = weight;
                    hordeWeights[i] = weight;
                }
            }
            else
            {
                for (int i = 0; i < g_NumRanges; ++i)
                {
                    if (g_AllianceLevelRanges[i].lower > g_AllianceLevelRanges[i].upper)
                        allianceWeights[i] = 0.0f;
                    else
                        allianceWeights[i] = baseline + g_RealPlayerWeight *
                            (totalAllianceReal > 0 ? (1.0f / totalAllianceReal) : 1.0f) *
                            log(1 + allianceRealCounts[i]);

                    if (g_HordeLevelRanges[i].lower > g_HordeLevelRanges[i].upper)
                        hordeWeights[i] = 0.0f;
                    else
                        hordeWeights[i] = baseline + g_RealPlayerWeight *
                            (totalHordeReal > 0 ? (1.0f / totalHordeReal) : 1.0f) *
                            log(1 + hordeRealCounts[i]);
                }
            }

            auto applyWeights = [](std::vector<LevelRangeConfig>& ranges, const std::vector<float>& weights)
            {
                float total = 0.0f;
                for (int i = 0; i < g_NumRanges; ++i)
                    total += weights[i];
                int pctSum = 0;
                for (int i = 0; i < g_NumRanges; ++i)
                {
                    uint8 pct = (total > 0.0f) ? static_cast<uint8>(round((weights[i] / total) * 100)) : 0;
                    ranges[i].desiredPercent = pct;
                    pctSum += pct;
                }
                int missing = 100 - pctSum;
                for (int i = 0; i < g_NumRanges && missing > 0; ++i)
                    if (ranges[i].lower <= ranges[i].upper && ranges[i].desiredPercent > 0)
                    { ranges[i].desiredPercent++; missing--; }
            };

            applyWeights(g_AllianceLevelRanges, allianceWeights);
            applyWeights(g_HordeLevelRanges, hordeWeights);
            ClampAndBalanceBrackets();

            if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
            {
                for (int i = 0; i < g_NumRanges; ++i)
                    LOG_INFO("server.loading",
                             "[BotLevelBrackets] Final Range {}: {}-{}, Alliance Desired: {}%, Horde Desired: {}%",
                             i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                             g_AllianceLevelRanges[i].desiredPercent, g_HordeLevelRanges[i].desiredPercent);
            }
        }

        uint32 totalAllianceBots = 0;
        std::vector<int> allianceActualCounts(g_NumRanges, 0);
        std::vector<std::vector<Player*>> allianceBotsByRange(g_NumRanges);

        uint32 totalHordeBots = 0;
        std::vector<int> hordeActualCounts(g_NumRanges, 0);
        std::vector<std::vector<Player*>> hordeBotsByRange(g_NumRanges);

        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Starting processing of {} players.", allPlayers.size());

        for (auto const& itr : allPlayers)
        {
            Player* player = itr.second;
            if (!player)
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping null player.");
                continue;
            }
            if (!player->IsInWorld())
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping player '{}' as they are not in world.", player->GetName());
                continue;
            }
            if (!IsPlayerBot(player) || !IsPlayerRandomBot(player))
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping player '{}' as they are not a random bot.", player->GetName());
                continue;
            }
            if (IsBotExcluded(player))
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping excluded bot '{}'.", player->GetName());
                continue;
            }
            if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
                continue;
            if (g_IgnoreFriendListed && BotInFriendList(player))
                continue;
            if (g_IgnoreArenaTeamBots && BotInArenaTeam(player))
                continue;

            if (IsAlliancePlayerBot(player))
            {
                totalAllianceBots++;
                int rangeIndex = GetOrFlagPlayerBracket(player);
                if (rangeIndex >= 0)
                {
                    allianceActualCounts[rangeIndex]++;
                    allianceBotsByRange[rangeIndex].push_back(player);
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' with level {} added to range {}.",
                                 player->GetName(), player->GetLevel(), rangeIndex + 1);
                }
                else if (g_BotDistFullDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' with level {} does not fall into any defined range.",
                             player->GetName(), player->GetLevel());
            }
            else if (IsHordePlayerBot(player))
            {
                totalHordeBots++;
                int rangeIndex = GetOrFlagPlayerBracket(player);
                if (rangeIndex >= 0)
                {
                    hordeActualCounts[rangeIndex]++;
                    hordeBotsByRange[rangeIndex].push_back(player);
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' with level {} added to range {}.",
                                 player->GetName(), player->GetLevel(), rangeIndex + 1);
                }
                else if (g_BotDistFullDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' with level {} does not fall into any defined range.",
                             player->GetName(), player->GetLevel());
            }
        }

        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
            LOG_INFO("server.loading", "[BotLevelBrackets] Total Alliance Bots: {}.", totalAllianceBots);
            LOG_INFO("server.loading", "[BotLevelBrackets] Total Horde Bots: {}.", totalHordeBots);
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
        }

        // ---- Redistribute Alliance bots ----
        if (totalAllianceBots > 0)
        {
            if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");

            std::vector<int> allianceDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                allianceDesiredCounts[i] = static_cast<int>(round((g_AllianceLevelRanges[i].desiredPercent / 100.0) * totalAllianceBots));
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {} ({}-{}): Desired = {}, Actual = {}.",
                             i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                             allianceDesiredCounts[i], allianceActualCounts[i]);
            }

            for (int i = 0; i < g_NumRanges; ++i)
            {
                std::vector<Player*> safeBots, flaggedBots;
                for (Player* bot : allianceBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot)) safeBots.push_back(bot);
                    else                              flaggedBots.push_back(bot);
                }

                std::vector<int> targetRanges;
                for (int j = 0; j < g_NumRanges; ++j)
                    if (allianceActualCounts[j] < allianceDesiredCounts[j])
                        targetRanges.push_back(j);

                size_t targetIdx = 0;
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !safeBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();
                    int targetRange = targetRanges[targetIdx];
                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                    { targetIdx++; continue; }

                    EnqueuePendingReset(bot->GetGUID(), targetRange, g_AllianceLevelRanges.data());
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' flagged for pending level reset to range {}-{}.",
                                 bot->GetName(), g_AllianceLevelRanges[targetRange].lower, g_AllianceLevelRanges[targetRange].upper);

                    allianceActualCounts[i]--;
                    allianceActualCounts[targetRange]++;
                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                        targetIdx++;
                }

                targetIdx = 0;
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !flaggedBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();
                    int targetRange = targetRanges[targetIdx];
                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                    { targetIdx++; continue; }

                    EnqueuePendingReset(bot->GetGUID(), targetRange, g_AllianceLevelRanges.data());
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.loading", "[BotLevelBrackets] Alliance flagged bot '{}' flagged for pending level reset to range {}-{}.",
                                 bot->GetName(), g_AllianceLevelRanges[targetRange].lower, g_AllianceLevelRanges[targetRange].upper);

                    allianceActualCounts[i]--;
                    allianceActualCounts[targetRange]++;
                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                        targetIdx++;
                }
            }
        }

        // ---- Redistribute Horde bots ----
        if (totalHordeBots > 0)
        {
            if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");

            std::vector<int> hordeDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                hordeDesiredCounts[i] = static_cast<int>(round((g_HordeLevelRanges[i].desiredPercent / 100.0) * totalHordeBots));
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {} ({}-{}): Desired = {}, Actual = {}.",
                             i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper,
                             hordeDesiredCounts[i], hordeActualCounts[i]);
            }

            for (int i = 0; i < g_NumRanges; ++i)
            {
                std::vector<Player*> safeBots, flaggedBots;
                for (Player* bot : hordeBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot)) safeBots.push_back(bot);
                    else                              flaggedBots.push_back(bot);
                }

                std::vector<int> targetRanges;
                for (int j = 0; j < g_NumRanges; ++j)
                    if (hordeActualCounts[j] < hordeDesiredCounts[j])
                        targetRanges.push_back(j);

                size_t targetIdx = 0;
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !safeBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();
                    int targetRange = targetRanges[targetIdx];
                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                    { targetIdx++; continue; }

                    EnqueuePendingReset(bot->GetGUID(), targetRange, g_HordeLevelRanges.data());
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' flagged for pending level reset to range {}-{}.",
                                 bot->GetName(), g_HordeLevelRanges[targetRange].lower, g_HordeLevelRanges[targetRange].upper);

                    hordeActualCounts[i]--;
                    hordeActualCounts[targetRange]++;
                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                        targetIdx++;
                }

                targetIdx = 0;
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !flaggedBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();
                    int targetRange = targetRanges[targetIdx];
                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                    { targetIdx++; continue; }

                    EnqueuePendingReset(bot->GetGUID(), targetRange, g_HordeLevelRanges.data());
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.loading", "[BotLevelBrackets] Horde flagged bot '{}' flagged for pending level reset to range {}-{}.",
                                 bot->GetName(), g_HordeLevelRanges[targetRange].lower, g_HordeLevelRanges[targetRange].upper);

                    hordeActualCounts[i]--;
                    hordeActualCounts[targetRange]++;
                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                        targetIdx++;
                }
            }
        }

        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] ========================================= COMPLETE");
            LOG_INFO("server.loading", "[BotLevelBrackets] Distribution adjustment complete. Alliance bots: {}, Horde bots: {}, Pending: {}.",
                     totalAllianceBots, totalHordeBots, g_PendingLevelResets.size());
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
            std::vector<int> allianceDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                allianceDesiredCounts[i] = static_cast<int>(round((g_AllianceLevelRanges[i].desiredPercent / 100.0) * totalAllianceBots));
                LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {} ({}-{}): Desired = {}, Actual = {}.",
                         i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                         allianceDesiredCounts[i], allianceActualCounts[i]);
            }
            LOG_INFO("server.loading", "[BotLevelBrackets] ----------------------------------------");
            std::vector<int> hordeDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                hordeDesiredCounts[i] = static_cast<int>(round((g_HordeLevelRanges[i].desiredPercent / 100.0) * totalHordeBots));
                LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {} ({}-{}): Desired = {}, Actual = {}.",
                         i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper,
                         hordeDesiredCounts[i], hordeActualCounts[i]);
            }
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
        }
    }

    void ManualGuildTrackerCleanup()
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
            return;
        CleanupGuildTracker();
    }

private:
    uint32 m_timer;
    uint32 m_flaggedTimer;
    uint32 m_guildTrackerTimer;
    uint32 m_socialListTimer;
};


// -----------------------------------------------------------------------------
// PLAYER SCRIPT
// -----------------------------------------------------------------------------
class BotLevelBracketsPlayerScript : public PlayerScript
{
public:
    BotLevelBracketsPlayerScript() : PlayerScript("BotLevelBracketsPlayerScript") {}

    void OnPlayerLogout(Player* player) override
    {
        RemoveBotFromPendingResets(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
            return;
        if (IsPlayerBot(player))
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
            LOG_INFO("server.loading",
                     "[BotLevelBrackets] Player '{}' logged in - guild {} marked as having real players.",
                     player->GetName(), guildId);
    }
};


// -----------------------------------------------------------------------------
// GUILD SCRIPT
// -----------------------------------------------------------------------------
class BotLevelBracketsGuildScript : public GuildScript
{
public:
    BotLevelBracketsGuildScript() : GuildScript("BotLevelBracketsGuildScript") {}

    void OnAddMember(Guild* guild, Player* player, uint8& /*plRank*/) override
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
            return;
        if (!player || IsPlayerBot(player))
            return;

        uint32 guildId = guild->GetId();
        g_RealPlayerGuildIds.insert(guildId);
        g_PersistentRealPlayerGuildIds.insert(guildId);
        CharacterDatabase.Execute(
            "REPLACE INTO bot_level_brackets_guild_tracker (guild_id, has_real_players) VALUES ({}, 1)",
            guildId);

        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading",
                     "[BotLevelBrackets] Real player '{}' joined guild {} - guild marked as having real players.",
                     player->GetName(), guildId);
    }

    void OnRemoveMember(Guild* guild, Player* player, bool /*isDisbanding*/, bool /*isKicked*/) override
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
            return;
        if (!player || IsPlayerBot(player))
            return;

        uint32 guildId = guild->GetId();

        // Check if any other online real player is still in this guild.
        bool otherRealPlayerOnline = false;
        const auto& allPlayers = ObjectAccessor::GetPlayers();
        for (const auto& itr : allPlayers)
        {
            Player* p = itr.second;
            if (!p || !p->IsInWorld() || p == player)
                continue;
            if (!IsPlayerBot(p) && p->GetGuildId() == guildId)
            {
                otherRealPlayerOnline = true;
                break;
            }
        }

        if (!otherRealPlayerOnline)
        {
            // Remove from the online runtime cache only. The persistent DB tracker
            // is managed by UpdatePersistentGuildTracker / CleanupGuildTracker on their timers.
            g_RealPlayerGuildIds.erase(guildId);

            if (g_BotDistFullDebugMode)
                LOG_INFO("server.loading",
                         "[BotLevelBrackets] Real player '{}' left guild {} with no other online real players - "
                         "removed from online cache. Persistent tracker updated by timer.",
                         player->GetName(), guildId);
        }
    }
};


// -----------------------------------------------------------------------------
// COMMAND SCRIPT
// -----------------------------------------------------------------------------
class BotLevelBracketsCommandScript : public CommandScript
{
public:
    BotLevelBracketsCommandScript() : CommandScript("BotLevelBracketsCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "reload", HandleReloadConfig, SEC_ADMINISTRATOR, Console::No }
        };
        return commandTable;
    }

    static bool HandleReloadConfig(ChatHandler* handler)
    {
        LoadBotLevelBracketsConfig();
        handler->SendSysMessage("Bot level brackets config reloaded.");
        return true;
    }
};


// -----------------------------------------------------------------------------
// ENTRY POINT: Register the Bot Level Distribution Module
// -----------------------------------------------------------------------------
void Addmod_player_bot_level_bracketsScripts()
{
    new BotLevelBracketsWorldScript();
    new BotLevelBracketsPlayerScript();
    new BotLevelBracketsGuildScript();
    new BotLevelBracketsCommandScript();
}

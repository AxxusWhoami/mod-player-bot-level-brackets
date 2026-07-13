#pragma once

// AzerothCore / Playerbots includes used across all translation units.
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
#include "PlayerbotFactory.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "PlayerbotAIConfig.h"
#include "ArenaTeamMgr.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// =============================================================================
// SHARED TYPES
// =============================================================================

/// Defines a level bracket: inclusive [lower, upper] range and its target share.
struct LevelRangeConfig
{
    uint8 lower;          ///< Inclusive lower bound
    uint8 upper;          ///< Inclusive upper bound
    uint8 desiredPercent; ///< Desired percentage of bots for this bracket
};

/// An entry in the pending level reset queue.
struct PendingResetEntry
{
    ObjectGuid              botGuid;
    int                     targetRange;
    const LevelRangeConfig* factionRanges;
    uint32                  enqueuedAt; ///< Unix timestamp when the entry was added
};

// =============================================================================
// GLOBAL STATE  (defined in mod-player-bot-level-brackets-globals.cpp)
// =============================================================================

extern uint8  g_NumRanges;
extern uint8  g_RandomBotMinLevel;
extern uint8  g_RandomBotMaxLevel;

extern bool   g_BotLevelBracketsEnabled;
extern bool   g_IgnoreGuildBotsWithRealPlayers;
extern bool   g_IgnoreArenaTeamBots;

extern std::vector<LevelRangeConfig> g_AllianceLevelRanges;
extern std::vector<LevelRangeConfig> g_HordeLevelRanges;

extern uint32 g_BotDistCheckFrequency;
extern uint32 g_BotDistFlaggedCheckFrequency;
extern uint32 g_GuildTrackerUpdateFrequency;
extern uint32 g_SocialListRefreshFrequency;
extern bool   g_BotDistFullDebugMode;
extern bool   g_BotDistLiteDebugMode;
extern bool   g_UseDynamicDistribution;
extern bool   g_IgnoreFriendListed;
extern uint32 g_FlaggedProcessLimit;
extern uint32 g_MaxPendingQueueSize;
extern uint32 g_PendingQueueTTL;

extern float  g_RealPlayerWeight;
extern bool   g_SyncFactions;

extern std::unordered_set<uint64>                            g_SocialFriendsList;
extern std::unordered_set<std::string>                       g_ExcludeBotNames;
extern std::unordered_set<uint32>                            g_RealPlayerGuildIds;
extern std::unordered_set<uint32>                            g_PersistentRealPlayerGuildIds;
extern std::unordered_map<ObjectGuid, PendingResetEntry>     g_PendingLevelResets;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

// --- config ---
void ClampAndBalanceBrackets();
void LoadBotLevelBracketsConfig();

// --- helpers ---
bool IsBracketPlayerBot(Player* player);
bool IsPlayerRandomBot(Player* player);
bool IsAlliancePlayerBot(Player* bot);
bool IsHordePlayerBot(Player* bot);
bool BotInGuildWithRealPlayer(Player* bot);
bool BotInFriendList(Player* bot);
bool BotInArenaTeam(Player* bot);
bool IsBotSafeForLevelReset(Player* bot);
bool IsBotExcluded(Player* bot);

// --- guild / social ---
void LoadSocialFriendList();
void LoadRealPlayerGuildIds(const std::unordered_map<ObjectGuid, Player*>& players);
void LoadPersistentGuildTracker();
void UpdatePersistentGuildTracker();
void CleanupGuildTracker();

// --- queue ---
void RemoveBotFromPendingResets(Player* bot);
void EnqueuePendingReset(ObjectGuid guid, int targetRange, const LevelRangeConfig* factionRanges);
void ProcessPendingLevelResets();

// --- distribution ---
int   GetLevelRangeIndex(uint8 level, uint8 teamID);
uint8 GetRandomLevelInRange(const LevelRangeConfig& range);
void  AdjustBotToRange(Player* bot, int targetRangeIndex, const LevelRangeConfig* factionRanges);
int   GetOrFlagPlayerBracket(Player* player);

/// Full distribution cycle. Pass a non-null ChatHandler to receive a summary reply.
void RunDistributionCycle(ChatHandler* handler);

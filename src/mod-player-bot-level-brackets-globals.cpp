#include "mod-player-bot-level-brackets-internal.h"

uint8  g_NumRanges                   = 9;
uint8  g_RandomBotMinLevel           = 1;
uint8  g_RandomBotMaxLevel           = 80;

bool   g_BotLevelBracketsEnabled     = true;
bool   g_IgnoreGuildBotsWithRealPlayers = true;
bool   g_IgnoreArenaTeamBots         = true;

std::vector<LevelRangeConfig> g_AllianceLevelRanges;
std::vector<LevelRangeConfig> g_HordeLevelRanges;

uint32 g_BotDistCheckFrequency       = 300;
uint32 g_BotDistFlaggedCheckFrequency = 15;
uint32 g_GuildTrackerUpdateFrequency  = 600;
uint32 g_SocialListRefreshFrequency   = 300;
bool   g_BotDistFullDebugMode        = false;
bool   g_BotDistLiteDebugMode        = false;
bool   g_UseDynamicDistribution      = false;
bool   g_IgnoreFriendListed          = true;
uint32 g_FlaggedProcessLimit         = 5;
uint32 g_MaxPendingQueueSize         = 0;
uint32 g_PendingQueueTTL             = 0;

float  g_RealPlayerWeight            = 1.0f;
bool   g_SyncFactions                = false;
bool   g_TeleportOnLevelChange       = true;

std::unordered_set<uint64>                        g_SocialFriendsList;
std::unordered_set<std::string>                   g_ExcludeBotNames;
std::unordered_set<uint32>                        g_RealPlayerGuildIds;
std::unordered_set<uint32>                        g_PersistentRealPlayerGuildIds;
std::unordered_map<ObjectGuid, PendingResetEntry>   g_PendingLevelResets;
std::unordered_map<ObjectGuid, PendingTeleportEntry> g_PendingTeleports;

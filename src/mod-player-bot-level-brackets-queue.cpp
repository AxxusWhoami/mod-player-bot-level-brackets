#include "mod-player-bot-level-brackets-internal.h"

void RemoveBotFromPendingResets(Player* bot)
{
    if (bot)
    {
        std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
        g_PendingLevelResets.erase(bot->GetGUID());
    }
}


bool EnqueuePendingReset(ObjectGuid guid, int targetRange, bool isAlliance)
{
    std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
    if (g_MaxPendingQueueSize > 0 && g_PendingLevelResets.size() >= g_MaxPendingQueueSize)
    {
        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_WARN("server.world",
                     "[BotLevelBrackets] Pending queue is full ({}/{}). Bot {} not enqueued.",
                     g_PendingLevelResets.size(), g_MaxPendingQueueSize, guid.ToString());
        }
        return false;
    }
    uint32 now = static_cast<uint32>(time(nullptr));
    auto result = g_PendingLevelResets.emplace(guid, PendingResetEntry{guid, targetRange, isAlliance, now});
    if (!result.second)
    {
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world", "[BotLevelBrackets] Bot {} already in pending queue, skipping.", guid.ToString());
        return false;
    }
    return true;
}


void ProcessPendingLevelResets()
{
    std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
    if (g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] Processing {} pending resets...", g_PendingLevelResets.size());

    if (g_PendingLevelResets.empty())
        return;

    uint32 now = static_cast<uint32>(time(nullptr));
    uint32 processed = 0;

    for (auto it = g_PendingLevelResets.begin(); it != g_PendingLevelResets.end(); )
    {
        if (g_FlaggedProcessLimit > 0 && processed >= g_FlaggedProcessLimit)
            break;

        // TTL check: use configured TTL, or fallback to 1 hour if TTL is 0 (disabled)
        // to prevent perpetual queue entries from bots stuck in combat, BG, etc.
        uint32 effectiveTTL = (g_PendingQueueTTL > 0) ? g_PendingQueueTTL : 3600;
        if ((now - it->second.enqueuedAt) > effectiveTTL)
        {
            if (g_BotDistFullDebugMode)
                LOG_INFO("server.world", "[BotLevelBrackets] Pending entry for {} expired (TTL {}s). Dropping.",
                         it->first.ToString(), effectiveTTL);
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

        if (IsBotInProtectedDuelZone(bot))
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

        int  targetRange   = it->second.targetRange;
        const LevelRangeConfig* factionRanges = it->second.isAlliance
            ? g_AllianceLevelRanges.data()
            : g_HordeLevelRanges.data();

        if (targetRange < 0 || targetRange >= g_NumRanges)
        {
            it = g_PendingLevelResets.erase(it);
            continue;
        }

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

        if (IsBotSafeForLevelReset(bot))
        {
            AdjustBotToRange(bot, targetRange, factionRanges);
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.world", "[BotLevelBrackets] Bot '{}' successfully reset to level range {}-{}.",
                         bot->GetName(),
                         factionRanges[targetRange].lower,
                         factionRanges[targetRange].upper);
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

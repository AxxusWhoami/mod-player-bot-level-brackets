#include "mod-player-bot-level-brackets-internal.h"

void RemoveBotFromPendingResets(Player* bot)
{
    g_PendingLevelResets.erase(bot->GetGUID());
}


void EnqueuePendingReset(ObjectGuid guid, int targetRange, const LevelRangeConfig* factionRanges)
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


void ProcessPendingLevelResets()
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

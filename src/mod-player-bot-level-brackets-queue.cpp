#include "mod-player-bot-level-brackets-internal.h"

void RemoveBotFromPendingResets(Player* bot)
{
    if (bot)
    {
        std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
        g_PendingLevelResets.erase(bot->GetGUID());
    }
}


bool IsPendingQueueFull()
{
    std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
    return g_MaxPendingQueueSize > 0 && g_PendingLevelResets.size() >= g_MaxPendingQueueSize;
}


bool EnqueuePendingReset(ObjectGuid guid, int targetRange, bool isAlliance)
{
    std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
    if (g_MaxPendingQueueSize > 0 && g_PendingLevelResets.size() >= g_MaxPendingQueueSize)
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.world",
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
    CharacterDatabase.Execute(
        "REPLACE INTO bot_level_brackets_pending_resets (bot_guid, target_range, is_alliance, enqueued_at) VALUES ({}, {}, {}, {})",
        guid.GetRawValue(), targetRange, isAlliance ? 1 : 0, now);
    return true;
}


static void DeletePendingFromDB(ObjectGuid guid)
{
    CharacterDatabase.Execute(
        "DELETE FROM bot_level_brackets_pending_resets WHERE bot_guid = {}",
        guid.GetRawValue());
}


void ProcessPendingLevelResets()
{
    struct ProcessCandidate {
        ObjectGuid guid;
        int        targetRange;
        bool       isAlliance;
    };

    std::vector<ProcessCandidate> candidates;

    // Phase 1: under lock, validate and collect candidates; drop invalid entries.
    {
        std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.world", "[BotLevelBrackets] Processing {} pending resets...", g_PendingLevelResets.size());

        if (g_PendingLevelResets.empty())
            return;

        uint32 now = static_cast<uint32>(time(nullptr));
        uint32 effectiveTTL = (g_PendingQueueTTL > 0) ? g_PendingQueueTTL : 3600;

        // Phase 1a: sweep ALL expired entries regardless of batch position.
        // Without this, stale entries beyond the batch window never get
        // cleaned and the queue stays permanently full.
        uint32 expiredCount = 0;
        for (auto it = g_PendingLevelResets.begin(); it != g_PendingLevelResets.end(); )
        {
            if ((now - it->second.enqueuedAt) > effectiveTTL)
            {
                DeletePendingFromDB(it->first);
                it = g_PendingLevelResets.erase(it);
                ++expiredCount;
            }
            else
                ++it;
        }
        if (expiredCount > 0 && (g_BotDistFullDebugMode || g_BotDistLiteDebugMode))
            LOG_INFO("server.world", "[BotLevelBrackets] Expired {} stale entries from pending queue (TTL {}s).",
                     expiredCount, effectiveTTL);

        if (g_PendingLevelResets.empty())
            return;

        // Phase 1b: adaptive batch size — drain faster when queue is large.
        size_t batchSize = g_FlaggedProcessLimit;
        if (batchSize == 0)
            batchSize = g_PendingLevelResets.size();
        else
            batchSize = std::max(static_cast<size_t>(batchSize), g_PendingLevelResets.size() / 4);

        for (auto it = g_PendingLevelResets.begin(); it != g_PendingLevelResets.end(); )
        {
            if (candidates.size() >= batchSize)
                break;

            Player* bot = ObjectAccessor::FindPlayer(it->second.botGuid);

            if (!bot || !bot->IsInWorld() || !bot->GetSession() ||
                bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
            {
                DeletePendingFromDB(it->first);
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            if (IsBotExcluded(bot) || IsBotInProtectedDuelZone(bot))
            {
                DeletePendingFromDB(it->first);
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            int targetRange = it->second.targetRange;
            if (targetRange < 0 || targetRange >= g_NumRanges)
            {
                DeletePendingFromDB(it->first);
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            if ((g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(bot)) ||
                (g_IgnoreFriendListed && BotInFriendList(bot)) ||
                (g_IgnoreArenaTeamBots && BotInArenaTeam(bot)))
            {
                DeletePendingFromDB(it->first);
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            if (IsBotSafeForLevelReset(bot))
                candidates.push_back({it->first, targetRange, it->second.isAlliance});

            ++it;
        }
    }

    // Phase 2: mutex released — do the expensive level reset.
    std::vector<ObjectGuid> processed;
    for (auto& cand : candidates)
    {
        Player* bot = ObjectAccessor::FindPlayer(cand.guid);
        if (!bot || !bot->IsInWorld() || !bot->GetSession() ||
            bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
            continue;

        const LevelRangeConfig* factionRanges = cand.isAlliance
            ? g_AllianceLevelRanges.data()
            : g_HordeLevelRanges.data();

        AdjustBotToRange(bot, cand.targetRange, factionRanges);
        processed.push_back(cand.guid);

        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.world", "[BotLevelBrackets] Bot '{}' successfully reset to level range {}-{}.",
                     bot->GetName(),
                     factionRanges[cand.targetRange].lower,
                     factionRanges[cand.targetRange].upper);
        }
    }

    // Phase 3: under lock, remove successfully processed entries.
    if (!processed.empty())
    {
        std::lock_guard<std::mutex> lock(g_PendingLevelResetsMutex);
        for (const auto& guid : processed)
        {
            g_PendingLevelResets.erase(guid);
            DeletePendingFromDB(guid);
        }
    }
}


void LoadPendingResetsFromDB()
{
    uint32 now = static_cast<uint32>(time(nullptr));
    uint32 effectiveTTL = (g_PendingQueueTTL > 0) ? g_PendingQueueTTL : 3600;

    auto result = CharacterDatabase.Query(
        "SELECT bot_guid, target_range, is_alliance, enqueued_at FROM bot_level_brackets_pending_resets");
    if (!result)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Loaded 0 pending resets from database.");
        return;
    }

    uint32 loaded = 0;
    uint32 expired = 0;
    do
    {
        uint64 rawGuid = (*result)[0].Get<uint64>();
        int    targetRange = (*result)[1].Get<int8_t>();
        bool   isAlliance = (*result)[2].Get<uint8>() != 0;
        uint32 enqueuedAt = (*result)[3].Get<uint32>();

        if ((now - enqueuedAt) > effectiveTTL)
        {
            ++expired;
            continue;
        }

        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(rawGuid);
        g_PendingLevelResets.emplace(guid, PendingResetEntry{guid, targetRange, isAlliance, enqueuedAt});
        ++loaded;
    } while (result->NextRow());

    if (expired > 0)
    {
        CharacterDatabase.Execute(
            "DELETE FROM bot_level_brackets_pending_resets WHERE ({} - enqueued_at) > {}",
            now, effectiveTTL);
    }

    LOG_INFO("server.loading", "[BotLevelBrackets] Loaded {} pending resets from database ({} expired entries dropped).",
             loaded, expired);
}

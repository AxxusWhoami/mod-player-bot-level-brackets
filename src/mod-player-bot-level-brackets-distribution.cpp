#include "mod-player-bot-level-brackets-internal.h"

int GetLevelRangeIndex(uint8 level, uint8 teamID)
{
    if (level < g_RandomBotMinLevel || level > g_RandomBotMaxLevel)
        return -1;

    // Fast path: contiguous 10-level brackets (1-9, 10-19, ..., 80).
    // Works for the default config where ranges are contiguous and non-overlapping.
    static thread_local int8_t allianceLut[256];
    static thread_local int8_t hordeLut[256];
    static thread_local bool lutInit = false;
    static thread_local uint32 lutGeneration = 0;

    if (!lutInit || lutGeneration != g_ConfigGeneration)
    {
        std::fill_n(allianceLut, 256, int8_t(-1));
        std::fill_n(hordeLut, 256, int8_t(-1));
        for (uint8 i = 0; i < g_NumRanges; ++i)
        {
            for (uint8 lv = g_AllianceLevelRanges[i].lower; lv <= g_AllianceLevelRanges[i].upper; ++lv)
                allianceLut[lv] = static_cast<int8_t>(i);
            for (uint8 lv = g_HordeLevelRanges[i].lower; lv <= g_HordeLevelRanges[i].upper; ++lv)
                hordeLut[lv] = static_cast<int8_t>(i);
        }
        lutInit = true;
        lutGeneration = g_ConfigGeneration;
    }

    if (teamID == TEAM_ALLIANCE)
        return allianceLut[level];
    else if (teamID == TEAM_HORDE)
        return hordeLut[level];

    return -1;
}


uint8 GetRandomLevelInRange(const LevelRangeConfig& range)
{
    return urand(range.lower, range.upper);
}


void AdjustBotToRange(Player* bot, int targetRangeIndex, const LevelRangeConfig* factionRanges)
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
                LOG_INFO("server.world",
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
                LOG_INFO("server.world",
                         "[BotLevelBrackets] AdjustBotToRange: Invalid range {}-{} for {} bot '{}'.",
                         range.lower, range.upper, playerFaction, bot->GetName());
            }
            return;
        }
        newLevel = GetRandomLevelInRange(range);
    }

    PlayerbotFactory newFactory(bot, newLevel);
    newFactory.Randomize(false);

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
        LOG_INFO("server.world",
                 "[BotLevelBrackets] AdjustBotToRange: {} Bot '{}' - {} ({}) -> level {} (range {}-{}).",
                 playerFaction, bot->GetName(), playerClassName.c_str(), botOriginalLevel, newLevel,
                 factionRanges[targetRangeIndex].lower, factionRanges[targetRangeIndex].upper);
    }

    ChatHandler(bot->GetSession()).SendSysMessage("[mod-bot-level-brackets] Your level has been reset.");

    EnqueuePendingTeleport(bot, newLevel, bot->GetTeamId());
}


int GetOrFlagPlayerBracket(Player* player)
{
    bool isBot = IsBracketPlayerBot(player);

    if (isBot && IsBotExcluded(player))
        return -1;

    if (isBot && IsBotInProtectedDuelZone(player))
        return -1;

    if (isBot && g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
        return -1;

    if (isBot && g_IgnoreArenaTeamBots && BotInArenaTeam(player))
        return -1;

    if (isBot)
    {
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member->IsInWorld() && !IsBracketPlayerBot(member))
                {
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.world",
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
    {
        if (g_PendingLevelResets.count(player->GetGUID()) == 0)
            EnqueuePendingReset(player->GetGUID(), targetRange, factionRanges);
    }

    return -1;
}


// ---------------------------------------------------------------------------
// Helper: applies dynamic real-player weights to both faction range vectors.
// ---------------------------------------------------------------------------
static void ApplyDynamicWeights(
    const std::unordered_map<ObjectGuid, Player*>& allPlayers)
{
    std::vector<int> allianceRealCounts(g_NumRanges, 0);
    std::vector<int> hordeRealCounts(g_NumRanges, 0);
    uint32 totalAllianceReal = 0;
    uint32 totalHordeReal = 0;

    for (auto const& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld() || IsBracketPlayerBot(player))
            continue;
        int rangeIndex = GetLevelRangeIndex(player->GetLevel(), player->GetTeamId());
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
            float weight = baseline + g_RealPlayerWeight *
                (totalCombinedReal > 0 ? (1.0f / float(totalCombinedReal)) : 1.0f) *
                log(1 + allianceRealCounts[i] + hordeRealCounts[i]);
            allianceWeights[i] = weight;
            hordeWeights[i] = weight;
        }
    }
    else
    {
        for (int i = 0; i < g_NumRanges; ++i)
        {
            allianceWeights[i] = (g_AllianceLevelRanges[i].lower > g_AllianceLevelRanges[i].upper) ? 0.0f
                : baseline + g_RealPlayerWeight *
                  (totalAllianceReal > 0 ? (1.0f / totalAllianceReal) : 1.0f) *
                  log(1 + allianceRealCounts[i]);

            hordeWeights[i] = (g_HordeLevelRanges[i].lower > g_HordeLevelRanges[i].upper) ? 0.0f
                : baseline + g_RealPlayerWeight *
                  (totalHordeReal > 0 ? (1.0f / totalHordeReal) : 1.0f) *
                  log(1 + hordeRealCounts[i]);
        }
    }

    auto normaliseWeights = [](std::vector<LevelRangeConfig>& ranges, const std::vector<float>& weights)
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

    normaliseWeights(g_AllianceLevelRanges, allianceWeights);
    normaliseWeights(g_HordeLevelRanges, hordeWeights);
    ClampAndBalanceBrackets();

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        for (int i = 0; i < g_NumRanges; ++i)
            LOG_INFO("server.world",
                     "[BotLevelBrackets] Dynamic Range {}: {}-{}, Alliance Desired: {}%, Horde Desired: {}%",
                     i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                     g_AllianceLevelRanges[i].desiredPercent, g_HordeLevelRanges[i].desiredPercent);
    }
}


// ---------------------------------------------------------------------------
// Helper: redistributes bots within one faction between over- and under-
//         populated brackets, using safe bots first, then flagged bots.
// ---------------------------------------------------------------------------
static void RedistributeFaction(
    uint32                                    totalBots,
    std::vector<int>&                         actualCounts,
    std::vector<std::vector<Player*>>&        botsByRange,
    std::vector<LevelRangeConfig>&            factionRanges,
    const char*                               factionLabel)
{
    if (totalBots == 0)
        return;

    std::vector<int> desiredCounts(g_NumRanges, 0);
    for (int i = 0; i < g_NumRanges; ++i)
    {
        desiredCounts[i] = static_cast<int>(round((factionRanges[i].desiredPercent / 100.0) * totalBots));
        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
            LOG_INFO("server.world", "[BotLevelBrackets] {} Range {} ({}-{}): Desired = {}, Actual = {}.",
                     factionLabel, i + 1, factionRanges[i].lower, factionRanges[i].upper,
                     desiredCounts[i], actualCounts[i]);
    }

    for (int i = 0; i < g_NumRanges; ++i)
    {
        std::vector<Player*> safeBots, flaggedBots;
        for (Player* bot : botsByRange[i])
        {
            if (IsBotSafeForLevelReset(bot)) safeBots.push_back(bot);
            else                              flaggedBots.push_back(bot);
        }

        std::vector<int> targetRanges;
        for (int j = 0; j < g_NumRanges; ++j)
            if (actualCounts[j] < desiredCounts[j])
                targetRanges.push_back(j);

        auto drainBatch = [&](std::vector<Player*>& bots, const char* batchLabel)
        {
            size_t targetIdx = 0;
            while (actualCounts[i] > desiredCounts[i] && !bots.empty() && targetIdx < targetRanges.size())
            {
                Player* bot = bots.back();
                bots.pop_back();
                int targetRange = targetRanges[targetIdx];
                if (actualCounts[targetRange] >= desiredCounts[targetRange])
                { targetIdx++; continue; }

                if (EnqueuePendingReset(bot->GetGUID(), targetRange, factionRanges.data()))
                {
                    if (g_BotDistFullDebugMode)
                        LOG_INFO("server.world", "[BotLevelBrackets] {} {}bot '{}' enqueued for range {}-{}.",
                                 factionLabel, batchLabel,
                                 bot->GetName(), factionRanges[targetRange].lower, factionRanges[targetRange].upper);

                    actualCounts[i]--;
                    actualCounts[targetRange]++;
                }
                if (actualCounts[targetRange] >= desiredCounts[targetRange])
                    targetIdx++;
            }
        };

        drainBatch(safeBots, "");
        drainBatch(flaggedBots, "flagged ");
    }
}


void RunDistributionCycle(ChatHandler* handler)
{
    const auto& allPlayers = ObjectAccessor::GetPlayers();

    LoadRealPlayerGuildIds(allPlayers);

    if (g_UseDynamicDistribution)
        ApplyDynamicWeights(allPlayers);

    uint32 totalAllianceBots = 0;
    std::vector<int> allianceActualCounts(g_NumRanges, 0);
    std::vector<std::vector<Player*>> allianceBotsByRange(g_NumRanges);

    uint32 totalHordeBots = 0;
    std::vector<int> hordeActualCounts(g_NumRanges, 0);
    std::vector<std::vector<Player*>> hordeBotsByRange(g_NumRanges);

    if (g_BotDistFullDebugMode)
        LOG_INFO("server.world", "[BotLevelBrackets] Starting processing of {} players.", allPlayers.size());

    for (auto const& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
        if (!IsBracketPlayerBot(player) || !IsPlayerRandomBot(player))
            continue;
        if (IsBotExcluded(player))
            continue;
        if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
            continue;
        if (g_IgnoreFriendListed && BotInFriendList(player))
            continue;
        if (g_IgnoreArenaTeamBots && BotInArenaTeam(player))
            continue;

        if (IsAlliancePlayerBot(player))
        {
            if (IsBotInProtectedDuelZone(player))
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.world", "[BotLevelBrackets] Alliance bot '{}' in protected duel zone, excluded from distribution.",
                             player->GetName());
                continue;
            }
            totalAllianceBots++;
            int rangeIndex = GetOrFlagPlayerBracket(player);
            if (rangeIndex >= 0)
            {
                allianceActualCounts[rangeIndex]++;
                allianceBotsByRange[rangeIndex].push_back(player);
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.world", "[BotLevelBrackets] Alliance bot '{}' with level {} added to range {}.",
                             player->GetName(), player->GetLevel(), rangeIndex + 1);
            }
            else if (g_BotDistFullDebugMode)
                LOG_INFO("server.world", "[BotLevelBrackets] Alliance bot '{}' with level {} does not fall into any defined range.",
                         player->GetName(), player->GetLevel());
        }
        else if (IsHordePlayerBot(player))
        {
            if (IsBotInProtectedDuelZone(player))
            {
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.world", "[BotLevelBrackets] Horde bot '{}' in protected duel zone, excluded from distribution.",
                             player->GetName());
                continue;
            }
            totalHordeBots++;
            int rangeIndex = GetOrFlagPlayerBracket(player);
            if (rangeIndex >= 0)
            {
                hordeActualCounts[rangeIndex]++;
                hordeBotsByRange[rangeIndex].push_back(player);
                if (g_BotDistFullDebugMode)
                    LOG_INFO("server.world", "[BotLevelBrackets] Horde bot '{}' with level {} added to range {}.",
                             player->GetName(), player->GetLevel(), rangeIndex + 1);
            }
            else if (g_BotDistFullDebugMode)
                LOG_INFO("server.world", "[BotLevelBrackets] Horde bot '{}' with level {} does not fall into any defined range.",
                         player->GetName(), player->GetLevel());
        }
    }

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.world", "[BotLevelBrackets] Total Alliance Bots: {}.", totalAllianceBots);
        LOG_INFO("server.world", "[BotLevelBrackets] Total Horde Bots: {}.",    totalHordeBots);
    }

    RedistributeFaction(totalAllianceBots, allianceActualCounts, allianceBotsByRange, g_AllianceLevelRanges, "Alliance");
    RedistributeFaction(totalHordeBots,    hordeActualCounts,    hordeBotsByRange,    g_HordeLevelRanges,    "Horde");

    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.world",
                 "[BotLevelBrackets] Distribution cycle complete. Alliance: {}, Horde: {}, Pending: {}.",
                 totalAllianceBots, totalHordeBots, g_PendingLevelResets.size());
    }

    if (handler)
    {
        handler->PSendSysMessage("[BotBrackets] Alliance: {} bots  Horde: {} bots  Pending: {}",
                                 totalAllianceBots, totalHordeBots, g_PendingLevelResets.size());
    }
}

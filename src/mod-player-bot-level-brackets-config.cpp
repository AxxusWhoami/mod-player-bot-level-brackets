#include "mod-player-bot-level-brackets-internal.h"

#include <sstream>

/**
 * Clamps bracket bounds to [g_RandomBotMinLevel, g_RandomBotMaxLevel] and
 * auto-adjusts per-faction percentage sums to 100.
 */
void ClampAndBalanceBrackets()
{
    auto clampRanges = [](std::vector<LevelRangeConfig>& ranges)
    {
        for (uint8 i = 0; i < g_NumRanges; ++i)
        {
            if (ranges[i].lower < g_RandomBotMinLevel)
                ranges[i].lower = g_RandomBotMinLevel;
            if (ranges[i].upper > g_RandomBotMaxLevel)
                ranges[i].upper = g_RandomBotMaxLevel;
            if (ranges[i].lower > ranges[i].upper)
                ranges[i].desiredPercent = 0;
        }
    };
    clampRanges(g_AllianceLevelRanges);
    clampRanges(g_HordeLevelRanges);

    auto balanceRanges = [](std::vector<LevelRangeConfig>& ranges, const char* factionLabel)
    {
        uint32 total = 0;
        for (uint8 i = 0; i < g_NumRanges; ++i)
            total += ranges[i].desiredPercent;
        if (total == 100 || total == 0)
            return;
        if (g_BotDistFullDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] {} pct sum is {} (expected 100). Auto adjusting.", factionLabel, total);
        int missing = 100 - static_cast<int>(total);
        while (missing > 0)
            for (uint8 i = 0; i < g_NumRanges && missing > 0; ++i)
                if (ranges[i].lower <= ranges[i].upper && ranges[i].desiredPercent > 0)
                { ranges[i].desiredPercent++; missing--; }
    };
    balanceRanges(g_AllianceLevelRanges, "Alliance");
    balanceRanges(g_HordeLevelRanges, "Horde");
}


/**
 * Reads all module configuration options, validates them, and calls
 * ClampAndBalanceBrackets(). Safe to call repeatedly (hot-reload).
 */
void LoadBotLevelBracketsConfig()
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
    g_TeleportOnLevelChange             = sConfigMgr->GetOption<bool>("BotLevelBrackets.TeleportOnLevelChange", true);
    g_IgnoreFriendListed                = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreFriendListed", true);
    g_FlaggedProcessLimit               = sConfigMgr->GetOption<uint32>("BotLevelBrackets.FlaggedProcessLimit", 5);
    g_MaxPendingQueueSize               = sConfigMgr->GetOption<uint32>("BotLevelBrackets.MaxPendingQueueSize", 0);
    g_PendingQueueTTL                   = sConfigMgr->GetOption<uint32>("BotLevelBrackets.PendingQueueTTL", 0);

    g_HubDisperseEnabled                = sConfigMgr->GetOption<bool>("BotLevelBrackets.HubDisperse.Enabled", true);
    g_HubDisperseFrequency              = sConfigMgr->GetOption<uint32>("BotLevelBrackets.HubDisperse.Frequency", 30);
    g_HubDisperseBotsPerCycle           = sConfigMgr->GetOption<uint32>("BotLevelBrackets.HubDisperse.BotsPerCycle", 10);
    g_HubDisperseMaxBotsPerHub          = sConfigMgr->GetOption<uint32>("BotLevelBrackets.HubDisperse.MaxBotsPerHub", 5);

    std::string excludeNames = sConfigMgr->GetOption<std::string>("BotLevelBrackets.ExcludeNames", "");
    g_ExcludeBotNames.clear();
    std::istringstream f(excludeNames);
    std::string s;
    while (getline(f, s, ','))
    {
        s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
        if (!s.empty())
            g_ExcludeBotNames.insert(s);
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

    // SyncFactions: bracket definitions must be identical for both factions.
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

#include "mod-player-bot-level-brackets-internal.h"

static inline bool IsBotInvalidOrLeaving(Player* bot)
{
    return !bot || !bot->IsInWorld() || !bot->GetSession()
        || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld();
}

bool IsPlayerBot(Player* player)
{
    if (!player)
        return false;
    PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
    return botAI && botAI->IsBotAI();
}

bool IsPlayerRandomBot(Player* player)
{
    if (!player)
        return false;
    return sRandomPlayerbotMgr.IsRandomBot(player);
}

bool IsAlliancePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_ALLIANCE);
}

bool IsHordePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_HORDE);
}

bool BotInGuildWithRealPlayer(Player* bot)
{
    if (IsBotInvalidOrLeaving(bot))
        return false;
    uint32 guildId = bot->GetGuildId();
    if (guildId == 0)
        return false;
    return g_RealPlayerGuildIds.count(guildId) > 0 || g_PersistentRealPlayerGuildIds.count(guildId) > 0;
}

bool BotInFriendList(Player* bot)
{
    if (IsBotInvalidOrLeaving(bot))
        return false;
    bool found = g_SocialFriendsList.count(bot->GetGUID().GetRawValue()) > 0;
    if (found && g_BotDistFullDebugMode)
        LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is on a Real Player's friends list",
                 bot->GetName(), bot->GetLevel());
    return found;
}

bool BotInArenaTeam(Player* bot)
{
    if (!bot)
        return false;
    for (uint32 slot = 0; slot < MAX_ARENA_SLOT; ++slot)
        if (sArenaTeamMgr->GetArenaTeamById(bot->GetArenaTeamId(slot)))
            return true;
    return false;
}

bool IsBotSafeForLevelReset(Player* bot)
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

bool IsBotExcluded(Player* bot)
{
    if (!bot)
        return false;
    return g_ExcludeBotNames.count(bot->GetName()) > 0;
}

# AzerothCore Module: Bot Level Brackets

<p align="center">
  <img src="./icon.png" alt="Bot Level Brackets Icon" title="Bot Level Brackets Icon">
</p>

> **Disclaimer:** This module requires the [Playerbots module](https://github.com/liyunfan1223/mod-playerbots). Ensure that the Playerbots module is installed and running before using this module.

## Overview

The Bot Level Brackets module for AzerothCore ensures an even spread of random player bots across configurable level ranges (brackets). It periodically monitors bot levels and automatically redistributes them from overpopulated brackets to those with a deficit by running each bot through the standard Playerbots randomization function, which resets gear, talents and abilities to match the new level.

Bots that cannot be safely reset at a given moment (for example, those in combat, in a battleground, in flight, or grouped with real players) are flagged and retried on a shorter interval until they become safe. Death Knight bots are always protected from being assigned a level below 55.

## Features

- **Configurable faction-specific level brackets** — define independent level ranges and target percentages for Alliance and Horde bots.
- **Desired percentage distribution** — specify how much of the bot population should occupy each bracket; percentages are auto-balanced to sum to 100.
- **Dynamic distribution** — optionally let real player activity shift bracket weights so bots naturally follow where players are levelling.
- **Faction synchronization** — optionally unify bracket definitions and real-player weighting across both factions.
- **Pending reset queue** — bots that fail safety checks are queued and retried automatically, with a configurable per-cycle process limit, optional queue size cap, and optional entry TTL.
- **Death Knight safeguard** — DK bots are never assigned to a bracket whose upper bound is below level 55.
- **Guild bot exclusion** — bots sharing a guild with any real player are excluded from adjustments; guild status is updated in real time on player login and guild join/leave events, and persisted in the database so it survives logouts.
- **Friend list exclusion** — bots on any real player's friend list are excluded; the list is refreshed on its own independent timer.
- **Arena team exclusion** — bots that are members of an arena team are excluded from level adjustments.
- **Name-based exclusion** — specific bots can be excluded by name via a comma-separated configuration list.
- **Startup configuration validation** — warns on invalid `NumRanges`, overlapping brackets, zero-sum percentages, and low `FlaggedProcessLimit`.
- **Admin commands** — five in-game/console commands for live monitoring, forced cycles, and queue management without server restarts.
- **Full and lite debug modes** — detailed server log output for monitoring and troubleshooting.

## Minimum and Maximum Bot Level Support

This module reads the global Playerbots level limits and respects them when clamping brackets:

- `AiPlayerbot.RandomBotMinLevel` — default `1`
- `AiPlayerbot.RandomBotMaxLevel` — default `80`

> **Warning:** If `AiPlayerbot.RandomBotMaxLevel` is set below 55, ensure that Death Knight bots are disabled in your Playerbots configuration.

## Requirements

- AzerothCore with the [Playerbots](https://github.com/liyunfan1223/mod-playerbots) module.
- A MariaDB / MySQL database for the characters schema (used by the persistent guild tracker).

## Installation

1. **Clone the module** into your AzerothCore modules directory:

   ```bash
   cd /path/to/azerothcore/modules
   git clone https://github.com/DustinHendrickson/mod-player-bot-level-brackets.git
   ```

2. **Apply the database migration** for the persistent guild tracker table. The SQL file is located at:

   ```
   data/sql/characters/base/2025_07_31_bot_level_brackets_guild_tracker.sql
   ```

   Apply it to your characters database:

   ```bash
   mysql -u <user> -p <characters_db> < data/sql/characters/base/2025_07_31_bot_level_brackets_guild_tracker.sql
   ```

3. **Recompile AzerothCore**:

   ```bash
   cd /path/to/azerothcore
   mkdir -p build && cd build
   cmake ..
   make -j$(nproc)
   ```

4. **Configure the module** by copying the distribution config and editing it:

   ```bash
   cp conf/mod_player_bot_level_brackets.conf.dist conf/mod_player_bot_level_brackets.conf
   ```

   Then edit `mod_player_bot_level_brackets.conf` as described in the [Configuration](#configuration) section below.

5. **Restart the world server**:

   ```bash
   ./worldserver
   ```

## Database

The module creates one table in the characters database:

### `bot_level_brackets_guild_tracker`

Persistently tracks which guilds contain at least one real (non-bot) player so that bots in those guilds are protected from level changes even when the real players are offline.

| Column | Type | Description |
|---|---|---|
| `guild_id` | `INT UNSIGNED` (PK) | Guild ID from the `guild` table |
| `has_real_players` | `TINYINT(1)` | `1` if the guild has real players, `0` otherwise |
| `last_updated` | `TIMESTAMP` | Automatically updated on every write |

The table is populated and kept current by the module itself via the `GuildTrackerUpdateFrequency` timer.

## Configuration

All settings live in `mod_player_bot_level_brackets.conf`.

### Global Settings

| Setting | Description | Default | Valid Values |
|---|---|---|---|
| `BotLevelBrackets.Enabled` | Enables or disables the entire module. | `1` | `0` / `1` |
| `BotLevelBrackets.FullDebugMode` | Enables verbose debug logging for every bot decision. | `0` | `0` / `1` |
| `BotLevelBrackets.LiteDebugMode` | Enables summary-level debug logging (distribution totals and bracket counts). | `0` | `0` / `1` |
| `BotLevelBrackets.CheckFrequency` | Seconds between full distribution checks. | `300` | Positive integer |
| `BotLevelBrackets.CheckFlaggedFrequency` | Seconds between attempts to process queued (pending) level resets. | `15` | Positive integer |
| `BotLevelBrackets.FlaggedProcessLimit` | Maximum bots processed from the pending queue per cycle. `0` = unlimited. Values below 3 produce a startup warning. | `5` | Non-negative integer |
| `BotLevelBrackets.MaxPendingQueueSize` | Maximum number of bots that can be in the pending reset queue at once. `0` = unlimited. Prevents unbounded growth during mass events. | `0` | Non-negative integer |
| `BotLevelBrackets.PendingQueueTTL` | Maximum seconds a bot can remain in the pending queue before its entry is dropped. `0` = no TTL. The bot will be re-evaluated on the next distribution cycle. | `0` | Non-negative integer |
| `BotLevelBrackets.IgnoreGuildBotsWithRealPlayers` | Exclude bots in guilds that have real players (online or offline via DB tracker). Guild status is now also updated in real time via login and guild-join/leave events. | `1` | `0` / `1` |
| `BotLevelBrackets.GuildTrackerUpdateFrequency` | Seconds between periodic guild tracker database updates. | `600` | Positive integer |
| `BotLevelBrackets.IgnoreArenaTeamBots` | Exclude bots that are members of any arena team from level adjustments. | `1` | `0` / `1` |
| `BotLevelBrackets.IgnoreFriendListed` | Exclude bots that appear on any real player's friend list. | `1` | `0` / `1` |
| `BotLevelBrackets.SocialListRefreshFrequency` | Seconds between friend list database refreshes. Independent of the main distribution timer. | `300` | Positive integer |
| `BotLevelBrackets.ExcludeNames` | Comma-separated list of bot names to always exclude from bracket processing (case-insensitive). | `` | String |
| `BotLevelBrackets.NumRanges` | Total number of level brackets. Must match the number of `RangeX` entries defined below. Values above 20 produce a startup warning. `0` is invalid and defaults to `1`. | `9` | Positive integer |

> **Important:** If you increase `NumRanges` beyond the default of 9, you must add the corresponding `RangeX.Lower`, `RangeX.Upper`, and `RangeX.Pct` lines for both Alliance and Horde sections in your `.conf` file.

### Dynamic Distribution Settings

| Setting | Description | Default | Valid Values |
|---|---|---|---|
| `BotLevelBrackets.Dynamic.UseDynamicDistribution` | Recalculate bracket target percentages each cycle based on where real players are. | `0` | `0` / `1` |
| `BotLevelBrackets.Dynamic.RealPlayerWeight` | How strongly real player presence inflates a bracket's target share. `0.0` = even distribution always; `1.0` = mild effect; `10–15` = heavy concentration. | `1.0` | Float `>= 0.0` |
| `BotLevelBrackets.Dynamic.SyncFactions` | Combine both factions' real player counts when computing dynamic weights. Requires identical bracket definitions for Alliance and Horde. Server will not start if brackets mismatch when this is on. | `0` | `0` / `1` |

**Dynamic weight formula (per bracket):**

```
bracket_weight = 1.0 + (RealPlayerWeight × (1 / TotalRealPlayers) × log(1 + RealPlayersInBracket))
```

All bracket weights are normalized so that the target percentages always sum to 100.

**Reference values for `RealPlayerWeight` (9 equal brackets, 10 real players, 6 in one bracket):**

| Weight | Bracket with players | Each empty bracket |
|---|---|---|
| `0.0` | 11.11% | 11.11% |
| `1.0` | ~12.77% | ~10.69% |
| `3.0` | ~15.73% | ~9.93% |
| `5.0` | ~18.31% | ~9.28% |

### Alliance Level Brackets

For each bracket `X` (1 through `NumRanges`):

| Key | Description |
|---|---|
| `BotLevelBrackets.Alliance.RangeX.Lower` | Inclusive lower level bound for bracket X. |
| `BotLevelBrackets.Alliance.RangeX.Upper` | Inclusive upper level bound for bracket X. |
| `BotLevelBrackets.Alliance.RangeX.Pct` | Desired percentage of Alliance bots in bracket X. All brackets must sum to 100. |

**Default (9 brackets, levels 1–80):**

| Bracket | Levels | Pct |
|---|---|---|
| Range1 | 1–9 | 12% |
| Range2 | 10–19 | 11% |
| Range3 | 20–29 | 11% |
| Range4 | 30–39 | 11% |
| Range5 | 40–49 | 11% |
| Range6 | 50–59 | 11% |
| Range7 | 60–69 | 11% |
| Range8 | 70–79 | 11% |
| Range9 | 80 | 11% |

### Horde Level Brackets

Identical structure to Alliance. Each bracket uses the keys `BotLevelBrackets.Horde.RangeX.Lower`, `.Upper`, and `.Pct`. The default distribution mirrors the Alliance defaults above.

### Example: Isolating a single level as its own bracket

To give level 60 its own bracket (e.g., to match a TBC-era content spike), increase `NumRanges` to 10 and split the existing Range7:

```ini
BotLevelBrackets.NumRanges = 10

BotLevelBrackets.Alliance.Range7.Lower = 60
BotLevelBrackets.Alliance.Range7.Upper = 60
BotLevelBrackets.Alliance.Range7.Pct   = 11

BotLevelBrackets.Alliance.Range8.Lower = 61
BotLevelBrackets.Alliance.Range8.Upper = 69
BotLevelBrackets.Alliance.Range8.Pct   = 10
# ... adjust remaining ranges so the sum is still 100
```

## Admin Commands

The module registers the following in-game command. It requires the `Administrator` security level and cannot be used from the console.

| Command | Description |
|---|---|
| `reload` | Reloads the module configuration from disk without restarting the server. |

Usage in-game:

```
.reload
```

> The exact command path depends on how the command is registered in your AzerothCore command table. Check your server's command list if the above does not work.

## Bot Safety Checks

Before a level reset is applied, the module verifies that the bot:

- Is in the world, alive, and not logging out.
- Is not in combat.
- Is not in a battleground, arena, random dungeon, or battleground queue.
- Is not in flight.
- Is not grouped with any real player.

Bots that fail any check are placed in the pending queue and retried at the `CheckFlaggedFrequency` interval.

## Debugging

Enable one of the debug modes in the configuration file:

```ini
BotLevelBrackets.LiteDebugMode = 1   # distribution totals and bracket counts per cycle
BotLevelBrackets.FullDebugMode = 1   # every bot decision, skip reason, and level change
```

Both modes write to the standard server log (`server.loading` channel).

## Admin Commands

All commands require the `Administrator` security level and work both in-game and from the server console.

| Command | Description |
|---|---|
| `.botbrackets reload` | Reload the module configuration from disk without restarting the server. |
| `.botbrackets status` | Print a live bracket table showing actual vs desired bot counts per range for both factions, plus the current pending queue size. |
| `.botbrackets force` | Trigger an immediate full distribution cycle (same logic as the periodic timer). Useful after a config reload or when testing bracket changes. |
| `.botbrackets pending` | Show the pending reset queue size, the queue cap (`MaxPendingQueueSize`), TTL setting, and the age of the oldest and newest entries. |
| `.botbrackets guildcleanup` | Remove from the persistent guild tracker any guild that no longer has real players online. Reports how many guilds were removed and how many remain tracked. |

## Troubleshooting

**Bots are not changing levels.**

Ensure `AiPlayerbot.DisableRandomLevels = 0` in your `playerbots.conf`. If this option is enabled, Playerbots overrides the level set by this module and resets bots to a fixed level.

**Server fails to start with a bracket mismatch error.**

If `BotLevelBrackets.Dynamic.SyncFactions = 1`, the Alliance and Horde bracket definitions must be identical (same count, same `Lower`/`Upper` values for each index). Compare both sections in your `.conf` file and correct any differences.

**Brackets do not cover all levels.**

If a bot's current level falls outside every defined bracket, the module automatically flags the bot for reassignment to the closest available bracket. Enable `FullDebugMode` to see these decisions in the log.

**The pending queue grows without bound.**

Set `BotLevelBrackets.MaxPendingQueueSize` to a reasonable cap (e.g., twice your bot count) and/or set `BotLevelBrackets.PendingQueueTTL` to a value in seconds after which stale entries are discarded. Also ensure `BotLevelBrackets.FlaggedProcessLimit` is high enough (>= 3) to drain the queue faster than it fills.

**Guild tracker is protecting bots in a guild that no longer has real players.**

Run the guild cleanup command (if available) or wait for the `GuildTrackerUpdateFrequency` timer to update the tracker. The module now also removes a guild from the online cache immediately when the last real player leaves via the `OnRemoveMember` guild event hook.

## License

Released under the GNU GPL v2 license, consistent with AzerothCore's licensing model.

## Contribution

Created by Dustin Hendrickson.

Pull requests and issues are welcome. Please ensure that contributions follow AzerothCore's coding standards.

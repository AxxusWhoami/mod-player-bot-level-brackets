-- Bot Level Brackets: register admin commands in the world command table.
-- Apply to the world database.

DELETE FROM `command` WHERE `name` IN (
    'botbrackets',
    'botbrackets reload',
    'botbrackets status',
    'botbrackets force',
    'botbrackets pending',
    'botbrackets guildcleanup'
);

INSERT INTO `command` (`name`, `security`, `help`) VALUES
    ('botbrackets',
     3,
     'Syntax: .botbrackets <subcommand>\r\n\r\nBot Level Brackets administration. Available subcommands: reload, status, force, pending, guildcleanup.'),

    ('botbrackets reload',
     3,
     'Syntax: .botbrackets reload\r\n\r\nReloads the Bot Level Brackets configuration from disk without restarting the server.'),

    ('botbrackets status',
     3,
     'Syntax: .botbrackets status\r\n\r\nPrints a live bracket table showing actual vs desired bot counts per range for both factions, plus the current pending queue size.'),

    ('botbrackets force',
     3,
     'Syntax: .botbrackets force\r\n\r\nTriggers an immediate full distribution cycle. Useful after a config reload or when testing bracket changes.'),

    ('botbrackets pending',
     3,
     'Syntax: .botbrackets pending\r\n\r\nShows the pending reset queue size, the configured cap, the TTL setting, and the age of the oldest and newest entries.'),

    ('botbrackets guildcleanup',
     3,
     'Syntax: .botbrackets guildcleanup\r\n\r\nRemoves from the persistent guild tracker any guild that no longer has real players online. Reports how many guilds were removed and how many remain tracked.');

#ifndef TS_TEXT_H
#define TS_TEXT_H

/* Every string shown on screen or written to the log is 7-bit ASCII: the libnx console font has no other glyphs. */

#define TXT_TITLE "TotK Sync - connect to PC"
#define TXT_LABEL_CONFIG "Config:  %s"
#define TXT_LABEL_PC_ADDRESS "PC:      http://%s:%d"
#define TXT_BUTTONS_READY "A - Send to PC    R - refresh    + - exit"
#define TXT_BUTTONS_BLOCKED "+ - exit"
#define TXT_SEND_NOT_AVAILABLE "Send to PC is not available yet"

#define TXT_STATUS_CHECKING "PC: checking..."
#define TXT_STATUS_ONLINE_SAVE "PC: online, save from %s"
#define TXT_STATUS_ONLINE_NO_SAVE "PC: online, no save"
#define TXT_STATUS_GAME_RUNNING "PC: TotK is running"
#define TXT_STATUS_NOT_REACHABLE "PC: not reachable"
#define TXT_STATUS_ERROR "PC: error"

#define TXT_MSG_NOT_REACHABLE "PC not reachable: is the launcher open? check firewall and same Wi-Fi"
#define TXT_MSG_WRONG_TOKEN "Wrong token: save switch-config.json from the PC launcher"
#define TXT_MSG_NOT_LAUNCHER "Not a TotK Sync launcher at %s:%d"
#define TXT_MSG_FORBIDDEN "PC refused this connection (HTTP 403): Switch must be in the same private network"
#define TXT_MSG_HTTP_ERROR "PC error: HTTP %ld"
#define TXT_MSG_BAD_RESPONSE "PC error: unexpected status response"
#define TXT_MSG_NETWORK_OTHER "Network error: curl %d"

#define TXT_RETRYING "Retrying in %d s..."
#define TXT_LOG_UNAVAILABLE "Log unavailable"
#define TXT_SOCKET_INIT_FAILED "Network init failed (socket): 0x%x"
#define TXT_CURL_INIT_FAILED "curl init failed"

#define TXT_CONFIG_NOT_FOUND "Config not found: %s\nSave switch-config.json from the PC launcher to that path"
#define TXT_CONFIG_READ_ERROR "Cannot read %s"
#define TXT_CONFIG_PARSE_ERROR "Parse error in %s"
#define TXT_CONFIG_PARSE_ERROR_AT "Parse error in %s: line %d, column %d"
#define TXT_CONFIG_FIELD_ERROR "Field \"%s\" missing or empty in %s"
#define TXT_CONFIG_FIELD_TOO_LONG "Field \"%s\" is too long in %s (maximum %d characters)"
#define TXT_CONFIG_LEGACY_PHASE0 "Old Phase 0 config: save switch-config.json from the PC launcher"
#define TXT_CONFIG_NOT_PRIVATE_IPV4 "Field \"pc_host\" must be a private IPv4 address (10.x.x.x, 172.16-31.x.x or 192.168.x.x) in %s"
#define TXT_CONFIG_PORT_INVALID "Field \"pc_port\" must be a number from 1024 to 65535 in %s"
#define TXT_CONFIG_TOKEN_INVALID "Field \"token\" must be printable ASCII without spaces in %s"
#define TXT_CONFIG_KEEP_BACKUPS_INVALID "Field \"keep_backups\" must be a whole number, 1 or more, in %s"

#endif

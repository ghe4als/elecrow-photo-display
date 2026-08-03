// WiFi credentials fallback.
// Real credentials live in .env (gitignored) and are injected as -D flags
// by scripts/load_env.py. See .env.example. This file is just the fallback
// for when no creds are configured; the firmware skips WiFi if it sees
// CHANGE_ME.
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#ifndef WIFI_SSID
#define WIFI_SSID "CHANGE_ME"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "CHANGE_ME"
#endif

#endif // WIFI_CONFIG_H
// netmgr - Wi-Fi lifecycle. Joins the configured network as a station; if none
// is configured, or the join does not succeed within a timeout, it falls back
// to hosting its own open AP with a captive portal so the user can configure it.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Bring up TCP/IP, the event loop, and Wi-Fi. Begins connecting (or falls back
// to AP) according to stored configuration.
void netmgr_init(void);

// Store new station credentials and (re)attempt to join. Safe to call from the
// web handler while running in AP-fallback mode.
void netmgr_set_wifi(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif

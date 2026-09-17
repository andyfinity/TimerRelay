// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "netmgr.h"
#include "appstate.h"
#include "timekeeper.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

static const char *TAG = "netmgr";

#define STA_CONNECT_TIMEOUT_MS 30000
#define STA_RETRY_DELAY_MS     2000
#define AP_CHANNEL             1
#define AP_MAX_CONN            4

static esp_netif_t   *s_sta_netif;
static esp_netif_t   *s_ap_netif;
static TimerHandle_t  s_sta_timer;
static bool           s_connected;
static bool           s_ap_active;
static bool           s_giving_up;      // stop STA retries once we fall back
static TaskHandle_t   s_dns_task;

// ---------------------------------------------------------------------------
// Runtime-state helpers
// ---------------------------------------------------------------------------
static void set_net_state(net_state_t st)
{
    appstate_lock();
    appstate_runtime()->net_state = st;
    appstate_unlock();
}

static void make_ap_ssid(char *out, size_t n)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, n, "TimerRelay-%02X%02X", mac[4], mac[5]);
}

// ---------------------------------------------------------------------------
// Captive-portal DNS: answer every A query with our AP address (192.168.4.1).
// ---------------------------------------------------------------------------
static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "dns socket failed"); vTaskDelete(NULL); return; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[512];
    ESP_LOGI(TAG, "captive DNS server up");
    while (s_ap_active) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (len < 12) continue;

        // Build a response: copy the query, set QR + answer count, and append an
        // A record pointing at 192.168.4.1 for whatever was asked.
        buf[2] |= 0x80;             // QR = response
        buf[3] = (buf[3] & 0x10) | 0x80;  // RA, keep RD
        buf[6] = 0x00; buf[7] = 0x01;     // ANCOUNT = 1
        buf[8] = 0x00; buf[9] = 0x00;     // NSCOUNT
        buf[10] = 0x00; buf[11] = 0x00;   // ARCOUNT

        if (len + 16 > (int)sizeof(buf)) continue;
        uint8_t *p = buf + len;
        *p++ = 0xC0; *p++ = 0x0C;         // name pointer to question
        *p++ = 0x00; *p++ = 0x01;         // type A
        *p++ = 0x00; *p++ = 0x01;         // class IN
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3C;  // TTL 60
        *p++ = 0x00; *p++ = 0x04;         // RDLENGTH 4
        *p++ = 192; *p++ = 168; *p++ = 4; *p++ = 1;

        sendto(sock, buf, p - buf, 0, (struct sockaddr *)&from, fl);
    }
    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// AP fallback
// ---------------------------------------------------------------------------
static void start_ap_fallback(void)
{
    if (s_ap_active) return;
    s_giving_up = true;
    ESP_LOGW(TAG, "starting AP fallback");

    esp_wifi_stop();

    char ssid[WIFI_SSID_MAX];
    make_ap_ssid(ssid, sizeof(ssid));

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = AP_CHANNEL;
    ap.ap.max_connection = AP_MAX_CONN;
    ap.ap.authmode = WIFI_AUTH_OPEN;   // open network per design (trusted-LAN)

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_active = true;
    appstate_lock();
    app_runtime_t *rt = appstate_runtime();
    strlcpy(rt->ap_ssid, ssid, sizeof(rt->ap_ssid));
    strlcpy(rt->ip_addr, "192.168.4.1", sizeof(rt->ip_addr));
    appstate_unlock();
    set_net_state(NET_STATE_AP_FALLBACK);

    if (!s_dns_task) {
        xTaskCreate(dns_task, "captive_dns", 3072, NULL, 5, &s_dns_task);
    }
    ESP_LOGI(TAG, "AP '%s' (open) at 192.168.4.1", ssid);
}

// ---------------------------------------------------------------------------
// STA
// ---------------------------------------------------------------------------
static void sta_timeout_cb(TimerHandle_t t)
{
    (void)t;
    if (!s_connected && !s_ap_active) {
        ESP_LOGW(TAG, "STA connect timed out");
        start_ap_fallback();
    }
}

static void start_sta(void)
{
    char ssid[WIFI_SSID_MAX], pass[WIFI_PASS_MAX];
    appstate_lock();
    strlcpy(ssid, appstate_config()->wifi_ssid, sizeof(ssid));
    strlcpy(pass, appstate_config()->wifi_pass, sizeof(pass));
    appstate_unlock();

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "no SSID configured; going straight to AP");
        start_ap_fallback();
        return;
    }

    s_connected = false;
    s_giving_up = false;

    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    set_net_state(NET_STATE_STA_CONNECTING);
    ESP_LOGI(TAG, "connecting to '%s'", ssid);

    xTimerStop(s_sta_timer, 0);
    xTimerChangePeriod(s_sta_timer, pdMS_TO_TICKS(STA_CONNECT_TIMEOUT_MS), 0);
    xTimerStart(s_sta_timer, 0);
}

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        s_connected = false;
        appstate_lock();
        appstate_runtime()->ip_addr[0] = '\0';
        appstate_unlock();
        if (!s_giving_up && !s_ap_active) {
            set_net_state(NET_STATE_STA_CONNECTING);
            vTaskDelay(pdMS_TO_TICKS(STA_RETRY_DELAY_MS));
            esp_wifi_connect();   // keep retrying until the timeout fires
        }
        break;
    default:
        break;
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    s_connected = true;
    xTimerStop(s_sta_timer, 0);

    appstate_lock();
    app_runtime_t *rt = appstate_runtime();
    snprintf(rt->ip_addr, sizeof(rt->ip_addr), IPSTR, IP2STR(&ev->ip_info.ip));
    appstate_unlock();
    set_net_state(NET_STATE_STA_CONNECTED);
    ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));

    timekeeper_on_got_ip();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void netmgr_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    // Apply hostname to the STA interface.
    appstate_lock();
    const char *host = appstate_config()->hostname;
    if (host[0]) esp_netif_set_hostname(s_sta_netif, host);
    appstate_unlock();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL, NULL));

    s_sta_timer = xTimerCreate("sta_to", pdMS_TO_TICKS(STA_CONNECT_TIMEOUT_MS),
                               pdFALSE, NULL, sta_timeout_cb);

    start_sta();
}

void netmgr_set_wifi(const char *ssid, const char *pass)
{
    appstate_lock();
    app_config_t *cfg = appstate_config();
    strlcpy(cfg->wifi_ssid, ssid ? ssid : "", sizeof(cfg->wifi_ssid));
    strlcpy(cfg->wifi_pass, pass ? pass : "", sizeof(cfg->wifi_pass));
    appstate_unlock();
    appstate_save_config();

    ESP_LOGI(TAG, "new Wi-Fi credentials saved; reconnecting");

    // Tear down AP fallback (and its DNS) if active, then retry STA.
    if (s_ap_active) {
        s_ap_active = false;
        if (s_dns_task) {
            // dns_task loops on s_ap_active and will exit on its own.
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    start_sta();
}

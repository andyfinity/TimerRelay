// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "webserver.h"
#include "appstate.h"
#include "timekeeper.h"
#include "netmgr.h"
#include "controller.h"
#include "macros.h"
#include "schedule.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "web";
static httpd_handle_t s_httpd;

// Big enough for a full schedule or macro-set payload.
#define MAX_BODY 16384

// --- Embedded static assets (see component CMakeLists EMBED_FILES) ----------
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");
extern const char style_css_start[]  asm("_binary_style_css_start");
extern const char style_css_end[]    asm("_binary_style_css_end");
extern const char app_js_start[]     asm("_binary_app_js_start");
extern const char app_js_end[]       asm("_binary_app_js_end");

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static const char *type_to_str(uint8_t t)
{
    switch (t) {
        case SCHED_WEEKLY:  return "weekly";
        case SCHED_DAILY:   return "daily";
        case SCHED_MONTHLY: return "monthly";
        case SCHED_YEARLY:  return "yearly";
        case SCHED_ONESHOT: return "oneshot";
        default:            return "weekly";
    }
}

static uint8_t str_to_type(const char *s)
{
    if (!s) return SCHED_WEEKLY;
    if (!strcmp(s, "daily"))   return SCHED_DAILY;
    if (!strcmp(s, "monthly")) return SCHED_MONTHLY;
    if (!strcmp(s, "yearly"))  return SCHED_YEARLY;
    if (!strcmp(s, "oneshot")) return SCHED_ONESHOT;
    return SCHED_WEEKLY;
}

static const char *mode_to_str(uint8_t m)
{
    switch (m) {
        case RELAY_MODE_ON:  return "on";
        case RELAY_MODE_OFF: return "off";
        default:             return "auto";
    }
}

static uint8_t str_to_mode(const char *s)
{
    if (s) {
        if (!strcmp(s, "on"))  return RELAY_MODE_ON;
        if (!strcmp(s, "off")) return RELAY_MODE_OFF;
    }
    return RELAY_MODE_AUTO;
}

// Resolve a macro name to its config slot index, or -1 if not found.
static int macro_index_by_name(const char *name)
{
    if (!name) return -1;
    int idx = -1;
    appstate_lock();
    app_config_t *cfg = appstate_config();
    for (int i = 0; i < MAX_MACROS; i++) {
        if (cfg->macros[i].used && strcmp(cfg->macros[i].name, name) == 0) {
            idx = i;
            break;
        }
    }
    appstate_unlock();
    return idx;
}

static const char *report_to_str(uint8_t r)
{
    switch (r) {
        case RELAY_REPORT_AUTO_ON:   return "auto_on";
        case RELAY_REPORT_AUTO_OFF:  return "auto_off";
        case RELAY_REPORT_MANUAL_ON: return "manual_on";
        default:                     return "manual_off";
    }
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_sendstr(req, out ? out : "{}");
    cJSON_free(out);
    cJSON_Delete(root);
    return e;
}

static char *read_body(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > MAX_BODY) return NULL;
    char *buf = malloc(total + 1);
    if (!buf) return NULL;
    int off = 0;
    while (off < total) {
        int r = httpd_req_recv(req, buf + off, total - off);
        if (r <= 0) { free(buf); return NULL; }
        off += r;
    }
    buf[total] = '\0';
    return buf;
}

// ---------------------------------------------------------------------------
// Static file handlers
// ---------------------------------------------------------------------------
static esp_err_t serve_asset(httpd_req_t *req, const char *start, const char *end,
                             const char *ctype)
{
    httpd_resp_set_type(req, ctype);
    return httpd_resp_send(req, start, end - start);
}

static esp_err_t h_index(httpd_req_t *req)
{
    return serve_asset(req, index_html_start, index_html_end, "text/html");
}
static esp_err_t h_css(httpd_req_t *req)
{
    return serve_asset(req, style_css_start, style_css_end, "text/css");
}
static esp_err_t h_js(httpd_req_t *req)
{
    return serve_asset(req, app_js_start, app_js_end, "application/javascript");
}

// Captive-portal probes -> redirect to the config page.
static esp_err_t h_captive(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

// ---------------------------------------------------------------------------
// GET /api/status
// ---------------------------------------------------------------------------
static esp_err_t h_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    time_t now = time(NULL);
    appstate_lock();
    app_config_t  *cfg = appstate_config();
    app_runtime_t *rt  = appstate_runtime();

    cJSON_AddBoolToObject(root, "time_valid", rt->time_valid);
    const char *src = rt->time_source == TIME_SRC_NTP ? "ntp"
                    : rt->time_source == TIME_SRC_MANUAL ? "manual" : "none";
    cJSON_AddStringToObject(root, "time_source", src);
    cJSON_AddBoolToObject(root, "ntp_reachable", rt->ntp_reachable);
    cJSON_AddNumberToObject(root, "epoch", (double)now);

    struct tm lt; localtime_r(&now, &lt);
    char local[32]; strftime(local, sizeof(local), "%Y-%m-%d %H:%M:%S", &lt);
    cJSON_AddStringToObject(root, "local", local);
    cJSON_AddStringToObject(root, "tz_name", cfg->tz_name);

    const char *net = rt->net_state == NET_STATE_STA_CONNECTED ? "sta_connected"
                    : rt->net_state == NET_STATE_AP_FALLBACK ? "ap_fallback"
                    : rt->net_state == NET_STATE_STA_CONNECTING ? "connecting" : "boot";
    cJSON_AddStringToObject(root, "net_state", net);
    cJSON_AddStringToObject(root, "ip", rt->ip_addr);
    cJSON_AddStringToObject(root, "ap_ssid", rt->ap_ssid);
    cJSON_AddStringToObject(root, "hostname", cfg->hostname);
    cJSON_AddStringToObject(root, "wifi_ssid", cfg->wifi_ssid);

    cJSON *relays = cJSON_AddArrayToObject(root, "relays");
    for (int r = 0; r < RELAY_COUNT; r++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", r + 1);
        cJSON_AddStringToObject(o, "mode", mode_to_str(cfg->relay_mode[r]));
        cJSON_AddStringToObject(o, "report", report_to_str(rt->relay_report[r]));
        cJSON_AddBoolToObject(o, "physical", rt->relay_physical[r]);
        cJSON_AddItemToArray(relays, o);
    }

    cJSON *mac = cJSON_AddObjectToObject(root, "macro");
    cJSON_AddBoolToObject(mac, "active", rt->macro_active);
    cJSON_AddNumberToObject(mac, "index", rt->macro_index);
    cJSON_AddStringToObject(mac, "name", rt->macro_name);
    cJSON_AddNumberToObject(mac, "step", rt->macro_step);
    cJSON_AddNumberToObject(mac, "steps", rt->macro_steps_total);
    cJSON_AddStringToObject(mac, "run",
                            rt->macro_run == MACRO_RUN_MANUAL ? "manual" : "auto");

    // Next scheduled event: seconds until, absolute time, and a short label.
    cJSON *ne = cJSON_AddObjectToObject(root, "next_event");
    time_t nev;
    char ndesc[40];
    if (rt->time_valid && schedule_next_event(now, &nev, ndesc, sizeof(ndesc))) {
        cJSON_AddBoolToObject(ne, "valid", true);
        cJSON_AddNumberToObject(ne, "in", (double)(nev - now));
        cJSON_AddNumberToObject(ne, "epoch", (double)nev);
        struct tm nlt; localtime_r(&nev, &nlt);
        char nbuf[32]; strftime(nbuf, sizeof(nbuf), "%Y-%m-%d %H:%M:%S", &nlt);
        cJSON_AddStringToObject(ne, "local", nbuf);
        cJSON_AddStringToObject(ne, "desc", ndesc);
    } else {
        cJSON_AddBoolToObject(ne, "valid", false);
    }

    // NTP health: reliable = last sync is recent enough to trust.
    bool nreach, nrel;
    int32_t nage;
    timekeeper_ntp_status(&nreach, &nrel, &nage);
    cJSON_AddBoolToObject(root, "ntp_reliable", nrel);
    cJSON_AddNumberToObject(root, "ntp_last_sync_age", nage);
    appstate_unlock();

    return send_json(req, root);
}

// ---------------------------------------------------------------------------
// GET /api/tzlist
// ---------------------------------------------------------------------------
static esp_err_t h_tzlist(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "zones");
    const char *name, *posix;
    for (size_t i = 0; timekeeper_tz_at(i, &name, &posix); i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(name));
    }
    return send_json(req, root);
}

// ---------------------------------------------------------------------------
// GET /api/config  (schedule editor payload)
// ---------------------------------------------------------------------------
static void event_to_json(const sched_event_t *e, cJSON *o)
{
    cJSON_AddBoolToObject(o, "enabled", e->enabled);
    cJSON_AddStringToObject(o, "type", type_to_str(e->type));
    cJSON_AddStringToObject(o, "action", e->action ? "on" : "off");
    cJSON *relays = cJSON_AddArrayToObject(o, "relays");
    for (int r = 0; r < RELAY_COUNT; r++)
        if (e->relay_mask & (1u << r)) cJSON_AddItemToArray(relays, cJSON_CreateNumber(r + 1));
    cJSON *dow = cJSON_AddArrayToObject(o, "dow");
    for (int d = 0; d < 7; d++)
        if (e->dow_mask & (1u << d)) cJSON_AddItemToArray(dow, cJSON_CreateNumber(d));
    cJSON_AddNumberToObject(o, "hour", e->hour);
    cJSON_AddNumberToObject(o, "minute", e->minute);
    cJSON_AddNumberToObject(o, "second", e->second);
    cJSON_AddNumberToObject(o, "day", e->day);
    cJSON_AddNumberToObject(o, "month", e->month);
    cJSON_AddNumberToObject(o, "year", e->year);
    cJSON_AddStringToObject(o, "target",
                            e->target == SCHED_TARGET_MACRO ? "macro" : "relays");
    cJSON_AddNumberToObject(o, "macro", e->macro_idx);
}

static esp_err_t h_get_config(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    appstate_lock();
    app_config_t *cfg = appstate_config();
    cJSON_AddStringToObject(root, "hostname", cfg->hostname);
    cJSON_AddStringToObject(root, "tz_name", cfg->tz_name);
    cJSON_AddStringToObject(root, "wifi_ssid", cfg->wifi_ssid);
    cJSON *evs = cJSON_AddArrayToObject(root, "events");
    for (uint16_t i = 0; i < cfg->event_count && i < MAX_SCHEDULE_EVENTS; i++) {
        cJSON *o = cJSON_CreateObject();
        event_to_json(&cfg->events[i], o);
        cJSON_AddItemToArray(evs, o);
    }

    cJSON *macs = cJSON_AddArrayToObject(root, "macros");
    for (int mi = 0; mi < MAX_MACROS; mi++) {
        const macro_t *m = &cfg->macros[mi];
        if (!m->used) continue;
        cJSON *mo = cJSON_CreateObject();
        cJSON_AddNumberToObject(mo, "index", mi);
        cJSON_AddStringToObject(mo, "name", m->name);
        cJSON_AddNumberToObject(mo, "loop", m->loop_count);
        cJSON *steps = cJSON_AddArrayToObject(mo, "steps");
        for (int s = 0; s < m->step_count; s++) {
            const macro_step_t *st = &m->steps[s];
            cJSON *so = cJSON_CreateObject();
            cJSON_AddNumberToObject(so, "delay", st->delay_s);
            cJSON_AddNumberToObject(so, "call", st->call_macro);
            cJSON_AddStringToObject(so, "action", mode_to_str(st->action));
            cJSON *sr = cJSON_AddArrayToObject(so, "relays");
            for (int r = 0; r < RELAY_COUNT; r++)
                if (st->relay_mask & (1u << r))
                    cJSON_AddItemToArray(sr, cJSON_CreateNumber(r + 1));
            cJSON_AddItemToArray(steps, so);
        }
        cJSON_AddItemToArray(macs, mo);
    }
    appstate_unlock();
    return send_json(req, root);
}

// ---------------------------------------------------------------------------
// POST /api/relay   {relay:1..6, mode:"on|auto|off"}
// ---------------------------------------------------------------------------
static esp_err_t h_set_relay(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    cJSON *jr = cJSON_GetObjectItem(j, "relay");
    cJSON *jm = cJSON_GetObjectItem(j, "mode");
    int relay = cJSON_IsNumber(jr) ? jr->valueint : 0;
    const char *ms = cJSON_IsString(jm) ? jm->valuestring : NULL;
    esp_err_t rc = ESP_FAIL;

    if (relay >= 1 && relay <= RELAY_COUNT && ms) {
        relay_mode_t mode = RELAY_MODE_AUTO;
        if (!strcmp(ms, "on")) mode = RELAY_MODE_ON;
        else if (!strcmp(ms, "off")) mode = RELAY_MODE_OFF;
        else if (!strcmp(ms, "auto")) mode = RELAY_MODE_AUTO;
        else { cJSON_Delete(j); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mode"); return ESP_FAIL; }

        appstate_lock();
        appstate_config()->relay_mode[relay - 1] = mode;
        appstate_unlock();
        appstate_save_config();
        controller_notify();
        rc = ESP_OK;
    }
    cJSON_Delete(j);

    if (rc != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "params"); return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/schedule   {events:[ ... ]}   (replaces the whole schedule)
// ---------------------------------------------------------------------------
static void json_to_event(cJSON *o, sched_event_t *e)
{
    memset(e, 0, sizeof(*e));
    cJSON *v;
    e->enabled = cJSON_IsTrue(cJSON_GetObjectItem(o, "enabled"));
    e->type = str_to_type(cJSON_GetStringValue(cJSON_GetObjectItem(o, "type")));
    const char *act = cJSON_GetStringValue(cJSON_GetObjectItem(o, "action"));
    e->action = (act && !strcmp(act, "on")) ? 1 : 0;

    cJSON *relays = cJSON_GetObjectItem(o, "relays");
    cJSON_ArrayForEach(v, relays) {
        int r = v->valueint;
        if (r >= 1 && r <= RELAY_COUNT) e->relay_mask |= (1u << (r - 1));
    }
    cJSON *dow = cJSON_GetObjectItem(o, "dow");
    cJSON_ArrayForEach(v, dow) {
        int d = v->valueint;
        if (d >= 0 && d <= 6) e->dow_mask |= (1u << d);
    }
    #define GETI(k, lo, hi, dst) do {                     \
        cJSON *x = cJSON_GetObjectItem(o, k);             \
        if (cJSON_IsNumber(x)) {                          \
            int _n = x->valueint;                         \
            if (_n < (lo)) { _n = (lo); }                 \
            if (_n > (hi)) { _n = (hi); }                 \
            dst = _n;                                     \
        }                                                 \
    } while (0)
    GETI("hour",   0, 23, e->hour);
    GETI("minute", 0, 59, e->minute);
    GETI("second", 0, 59, e->second);
    GETI("day",    1, 31, e->day);
    GETI("month",  1, 12, e->month);
    GETI("year",   1970, 2100, e->year);
    #undef GETI

    const char *tgt = cJSON_GetStringValue(cJSON_GetObjectItem(o, "target"));
    e->target = (tgt && !strcmp(tgt, "macro")) ? SCHED_TARGET_MACRO
                                               : SCHED_TARGET_RELAYS;
    cJSON *jmi = cJSON_GetObjectItem(o, "macro");
    if (cJSON_IsNumber(jmi)) {
        int mi = jmi->valueint;
        if (mi < 0) mi = 0;
        if (mi >= MAX_MACROS) mi = MAX_MACROS - 1;
        e->macro_idx = (uint8_t)mi;
    }
}

static esp_err_t h_set_schedule(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    cJSON *evs = cJSON_GetObjectItem(j, "events");
    if (!cJSON_IsArray(evs)) { cJSON_Delete(j); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "events"); return ESP_FAIL; }

    appstate_lock();
    app_config_t *cfg = appstate_config();
    uint16_t n = 0;
    cJSON *o;
    cJSON_ArrayForEach(o, evs) {
        if (n >= MAX_SCHEDULE_EVENTS) break;
        json_to_event(o, &cfg->events[n]);
        n++;
    }
    cfg->event_count = n;
    appstate_unlock();
    appstate_save_config();
    controller_notify();

    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/macros   {macros:[ {name, steps:[{delay, action, relays:[]}]} ]}
// Replaces all macro definitions. Slot index = array position (schedule events
// reference macros by that index).
// ---------------------------------------------------------------------------
static void json_to_macro(cJSON *mo, macro_t *m)
{
    memset(m, 0, sizeof(*m));
    m->used = true;
    const char *nm = cJSON_GetStringValue(cJSON_GetObjectItem(mo, "name"));
    strlcpy(m->name, nm ? nm : "", sizeof(m->name));

    cJSON *jl = cJSON_GetObjectItem(mo, "loop");
    int lc = cJSON_IsNumber(jl) ? jl->valueint : 1;   // default: run once
    if (lc < 0) lc = 0;                                // 0 = infinite
    if (lc > 65535) lc = 65535;
    m->loop_count = (uint16_t)lc;

    cJSON *steps = cJSON_GetObjectItem(mo, "steps");
    int sc = 0, v_i;
    cJSON *so;
    cJSON_ArrayForEach(so, steps) {
        if (sc >= MAX_MACRO_STEPS) break;
        macro_step_t *st = &m->steps[sc];
        st->call_macro = -1;
        cJSON *jd = cJSON_GetObjectItem(so, "delay");
        int d = cJSON_IsNumber(jd) ? jd->valueint : 0;
        if (d < 0) d = 0;
        if (d > 65535) d = 65535;
        st->delay_s = (uint16_t)d;

        cJSON *jc = cJSON_GetObjectItem(so, "call");
        if (cJSON_IsNumber(jc) && jc->valueint >= 0 && jc->valueint < MAX_MACROS) {
            st->call_macro = (int8_t)jc->valueint;   // call step
        } else {
            st->action = str_to_mode(cJSON_GetStringValue(cJSON_GetObjectItem(so, "action")));
            cJSON *sr = cJSON_GetObjectItem(so, "relays");
            cJSON *v;
            cJSON_ArrayForEach(v, sr) {
                v_i = v->valueint;
                if (v_i >= 1 && v_i <= RELAY_COUNT) st->relay_mask |= (1u << (v_i - 1));
            }
        }
        sc++;
    }
    m->step_count = (uint8_t)sc;
}

static esp_err_t h_set_macros(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    cJSON *macs = cJSON_GetObjectItem(j, "macros");
    if (!cJSON_IsArray(macs)) { cJSON_Delete(j); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "macros"); return ESP_FAIL; }

    appstate_lock();
    app_config_t *cfg = appstate_config();
    for (int i = 0; i < MAX_MACROS; i++) {
        cfg->macros[i].used = false;
        cfg->macros[i].step_count = 0;
    }
    int n = 0;
    cJSON *mo;
    cJSON_ArrayForEach(mo, macs) {
        if (n >= MAX_MACROS) break;
        json_to_macro(mo, &cfg->macros[n]);
        n++;
    }
    cfg->macro_count = (uint8_t)n;
    appstate_unlock();
    appstate_save_config();
    controller_notify();

    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/macro   {action:"start|stop|step", macro:<index|name>}
// ---------------------------------------------------------------------------
static esp_err_t h_macro_ctl(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    const char *act = cJSON_GetStringValue(cJSON_GetObjectItem(j, "action"));
    cJSON *jm = cJSON_GetObjectItem(j, "macro");
    int idx = -1;
    if (cJSON_IsNumber(jm)) idx = jm->valueint;
    else if (cJSON_IsString(jm)) idx = macro_index_by_name(jm->valuestring);

    bool ok = false;
    if (act && !strcmp(act, "start")) {
        ok = macros_start(idx, MACRO_RUN_AUTO);
        controller_notify();
    } else if (act && !strcmp(act, "stop")) {
        macros_stop();
        controller_notify();
        ok = true;
    } else if (act && !strcmp(act, "step")) {
        ok = macros_step(idx);   // idx < 0 steps the active macro
        controller_notify();
    }
    cJSON_Delete(j);

    if (!ok) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "action/macro"); return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/time   {epoch:<utc seconds>}
// ---------------------------------------------------------------------------
static esp_err_t h_set_time(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    cJSON *je = cJSON_GetObjectItem(j, "epoch");
    esp_err_t rc = ESP_FAIL;
    if (cJSON_IsNumber(je)) {
        timekeeper_set_manual((time_t)je->valuedouble);
        controller_notify();
        rc = ESP_OK;
    }
    cJSON_Delete(j);
    if (rc != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "epoch"); return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/timezone   {name:"America/New_York"}
// ---------------------------------------------------------------------------
static esp_err_t h_set_tz(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
    const char *posix = name ? timekeeper_tz_posix_for(name) : NULL;
    esp_err_t rc = ESP_FAIL;
    if (posix) {
        appstate_lock();
        app_config_t *cfg = appstate_config();
        strlcpy(cfg->tz_name, name, sizeof(cfg->tz_name));
        strlcpy(cfg->tz_posix, posix, sizeof(cfg->tz_posix));
        appstate_unlock();
        appstate_save_config();
        timekeeper_apply_tz(posix);
        controller_notify();
        rc = ESP_OK;
    }
    cJSON_Delete(j);
    if (rc != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name"); return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// POST /api/wifi   {ssid:"..", pass:".."}
// ---------------------------------------------------------------------------
static esp_err_t h_set_wifi(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(j, "ssid"));
    const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(j, "pass"));
    esp_err_t rc = ESP_FAIL;
    if (ssid && ssid[0]) {
        // Respond before the interface flips so the client gets an ack.
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":true}");
        netmgr_set_wifi(ssid, pass ? pass : "");
        rc = ESP_OK;
    }
    cJSON_Delete(j);
    if (rc != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid"); return ESP_FAIL; }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
static void reg(httpd_handle_t s, const char *uri, httpd_method_t m,
                esp_err_t (*fn)(httpd_req_t *))
{
    httpd_uri_t u = { .uri = uri, .method = m, .handler = fn };
    httpd_register_uri_handler(s, &u);
}

void webserver_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 32;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }

    reg(s_httpd, "/", HTTP_GET, h_index);
    reg(s_httpd, "/index.html", HTTP_GET, h_index);
    reg(s_httpd, "/style.css", HTTP_GET, h_css);
    reg(s_httpd, "/app.js", HTTP_GET, h_js);

    reg(s_httpd, "/api/status", HTTP_GET, h_status);
    reg(s_httpd, "/api/config", HTTP_GET, h_get_config);
    reg(s_httpd, "/api/tzlist", HTTP_GET, h_tzlist);
    reg(s_httpd, "/api/relay", HTTP_POST, h_set_relay);
    reg(s_httpd, "/api/schedule", HTTP_POST, h_set_schedule);
    reg(s_httpd, "/api/macros", HTTP_POST, h_set_macros);
    reg(s_httpd, "/api/macro", HTTP_POST, h_macro_ctl);
    reg(s_httpd, "/api/time", HTTP_POST, h_set_time);
    reg(s_httpd, "/api/timezone", HTTP_POST, h_set_tz);
    reg(s_httpd, "/api/wifi", HTTP_POST, h_set_wifi);

    // Common OS captive-portal probes -> redirect into the config UI.
    reg(s_httpd, "/generate_204", HTTP_GET, h_captive);
    reg(s_httpd, "/gen_204", HTTP_GET, h_captive);
    reg(s_httpd, "/hotspot-detect.html", HTTP_GET, h_captive);
    reg(s_httpd, "/ncsi.txt", HTTP_GET, h_captive);
    reg(s_httpd, "/connecttest.txt", HTTP_GET, h_captive);
    reg(s_httpd, "/*", HTTP_GET, h_captive);   // wildcard catch-all last

    ESP_LOGI(TAG, "HTTP server started on :80");
}

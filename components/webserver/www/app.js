// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
"use strict";

const $  = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
const DOW = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"];

async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) throw new Error(await r.text().catch(() => r.status));
  const t = await r.text();
  return t ? JSON.parse(t) : {};
}
function post(path, body) {
  return api(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
}
function flash(el, text, ok = true) {
  el.textContent = text; el.className = "msg " + (ok ? "ok" : "err");
  setTimeout(() => { el.textContent = ""; el.className = "msg"; }, 3000);
}

/* ---------------- Tabs ---------------- */
$$(".tab").forEach(t => t.addEventListener("click", () => {
  $$(".tab").forEach(x => x.classList.remove("active"));
  $$(".panel").forEach(x => x.classList.remove("active"));
  t.classList.add("active");
  $("#tab-" + t.dataset.tab).classList.add("active");
}));

/* ---------------- Status polling ---------------- */
const REPORT = {
  auto_on:   { txt: "Auto · On",     on: true,  manual: false },
  auto_off:  { txt: "Auto · Off",    on: false, manual: false },
  manual_on: { txt: "Manual · On",   on: true,  manual: true  },
  manual_off:{ txt: "Manual · Off",  on: false, manual: true  },
};
let relaysBuilt = false;

function buildRelays() {
  const grid = $("#relayGrid");
  grid.innerHTML = "";
  for (let i = 1; i <= 6; i++) {
    const el = document.createElement("div");
    el.className = "relay";
    el.innerHTML = `
      <div class="head">
        <span class="name">Relay ${i}</span>
        <span class="state" id="rstate${i}"><span class="dot"></span>—</span>
      </div>
      <div class="seg" id="rseg${i}">
        <button data-m="on">On</button>
        <button data-m="auto">Auto</button>
        <button data-m="off">Off</button>
      </div>`;
    grid.appendChild(el);
    $$("#rseg" + i + " button").forEach(b => b.addEventListener("click", () =>
      setRelay(i, b.dataset.m)));
  }
  relaysBuilt = true;
}

async function setRelay(id, mode) {
  try { await post("/api/relay", { relay: id, mode }); refreshStatus(); }
  catch (e) { console.error(e); }
}

function renderRelay(r) {
  const st = $("#rstate" + r.id);
  const info = REPORT[r.report] || REPORT.auto_off;
  st.innerHTML = `<span class="dot ${info.on ? "on" : ""}"></span>${info.txt}`;
  st.className = "state " + (info.on ? "on" : "off") + (info.manual ? " manual" : "");
  const seg = $("#rseg" + r.id);
  $$("button", seg).forEach(b => {
    b.className = "";
    if (b.dataset.m === r.mode) b.classList.add("sel-" + r.mode);
  });
}

function pill(el, text, cls) { el.textContent = text; el.className = "pill " + cls; }

async function refreshStatus() {
  let s;
  try { s = await api("/api/status"); } catch { pill($("#pill-net"), "Offline", "bad"); return; }
  if (!relaysBuilt) buildRelays();
  s.relays.forEach(renderRelay);

  // Pills
  const netMap = { sta_connected: ["Wi-Fi: connected", "ok"], ap_fallback: ["Setup AP", "warn"],
                   connecting: ["Connecting…", "warn"], boot: ["Booting", "warn"] };
  const [ntxt, ncls] = netMap[s.net_state] || ["Wi-Fi", "warn"];
  pill($("#pill-net"), ntxt, ncls);
  pill($("#pill-time"), s.time_valid ? s.local.slice(11) : "No time", s.time_valid ? "ok" : "bad");
  pill($("#pill-ntp"), s.ntp_reachable ? "NTP ok" : "NTP —", s.ntp_reachable ? "ok" : "warn");

  // Header + cards
  $("#devhost").textContent = s.hostname || "timerrelay";
  $("#curTime").textContent = s.time_valid ? s.local : "not set";
  $("#curSource").textContent = { ntp: "NTP", manual: "manual", none: "none" }[s.time_source];
  $("#curNtp").textContent = s.ntp_reachable ? "reachable" : "unreachable";
  $("#wifiState").textContent = ntxt;
  $("#wifiIp").textContent = s.ip || "—";
  if (s.ap_ssid) $("#apName").textContent = s.ap_ssid;
  if (!$("#ssid").matches(":focus") && s.wifi_ssid && !$("#ssid").value)
    $("#ssid").value = s.wifi_ssid;
  if ($("#tzSelect").dataset.current !== s.tz_name) selectTz(s.tz_name);
}

/* ---------------- Timezone ---------------- */
let wantedTz = null; // the device's current zone, applied once options exist

async function loadTz() {
  try {
    const d = await api("/api/tzlist");
    const sel = $("#tzSelect");
    sel.innerHTML = "";
    d.zones.forEach(z => { const o = document.createElement("option"); o.value = z; o.textContent = z; sel.appendChild(o); });
    // The list may arrive after the first /api/status: apply the pending zone now.
    if (wantedTz) applyTz(wantedTz);
  } catch (e) { console.error(e); }
}
// Only lock in as "current" once the matching option actually exists, so a
// status arriving before the option list doesn't leave the picker stuck on UTC.
function applyTz(name) {
  const sel = $("#tzSelect");
  if (!name) return;
  if (Array.from(sel.options).some(o => o.value === name)) {
    sel.value = name;
    sel.dataset.current = name;
  }
}
function selectTz(name) { wantedTz = name; applyTz(name); }
$("#saveTz").addEventListener("click", async () => {
  try { await post("/api/timezone", { name: $("#tzSelect").value }); flash($("#timeMsg"), "Timezone set."); refreshStatus(); }
  catch (e) { flash($("#timeMsg"), "Failed: " + e.message, false); }
});

/* ---------------- Manual time ---------------- */
$("#useNow").addEventListener("click", () => {
  const d = new Date(Date.now() - new Date().getTimezoneOffset() * 60000);
  $("#manualTime").value = d.toISOString().slice(0, 19);
});
$("#saveTime").addEventListener("click", async () => {
  const v = $("#manualTime").value;
  if (!v) { flash($("#timeMsg"), "Pick a date/time.", false); return; }
  const epoch = Math.floor(new Date(v).getTime() / 1000); // local -> UTC epoch
  try { await post("/api/time", { epoch }); flash($("#timeMsg"), "Time set."); refreshStatus(); }
  catch (e) { flash($("#timeMsg"), "Failed: " + e.message, false); }
});

/* ---------------- Wi-Fi ---------------- */
$("#saveWifi").addEventListener("click", async () => {
  const ssid = $("#ssid").value.trim();
  if (!ssid) { flash($("#wifiMsg"), "Enter an SSID.", false); return; }
  try { await post("/api/wifi", { ssid, pass: $("#wpass").value }); flash($("#wifiMsg"), "Saved. Reconnecting…"); }
  catch (e) { flash($("#wifiMsg"), "Failed: " + e.message, false); }
});

/* ---------------- Schedule ---------------- */
let events = [];

function hms(e) {
  const p = n => String(n).padStart(2, "0");
  return `${p(e.hour)}:${p(e.minute)}:${p(e.second)}`;
}
function eventCard(e, idx) {
  const div = document.createElement("div");
  div.className = "event";
  const relayChips = [1,2,3,4,5,6].map(r =>
    `<span class="chip ${e.relays.includes(r) ? "sel" : ""}" data-relay="${r}">${r}</span>`).join("");
  const dowChips = DOW.map((d, i) =>
    `<span class="chip ${e.dow.includes(i) ? "sel" : ""}" data-dow="${i}">${d}</span>`).join("");
  div.innerHTML = `
    <div class="erow">
      <label class="switch"><input type="checkbox" ${e.enabled ? "checked" : ""} data-en>
        <span class="track"></span></label>
      <div class="grp"><span>Relays</span><div class="chips" data-relays>${relayChips}</div></div>
      <div class="grp"><span>Days</span><div class="chips" data-dows>${dowChips}</div></div>
      <div class="grp"><span>Time</span><input class="timeinput" type="time" step="1" value="${hms(e)}" data-time></div>
      <div class="grp"><span>Action</span>
        <select class="actsel" data-act>
          <option value="on" ${e.action === "on" ? "selected" : ""}>Enable (NO)</option>
          <option value="off" ${e.action === "off" ? "selected" : ""}>Disable (NC)</option>
        </select></div>
      <button class="del" data-del>Delete</button>
    </div>`;

  $("[data-en]", div).addEventListener("change", ev => e.enabled = ev.target.checked);
  $$("[data-relays] .chip", div).forEach(c => c.addEventListener("click", () => {
    const r = +c.dataset.relay; c.classList.toggle("sel");
    e.relays = c.classList.contains("sel") ? [...e.relays, r] : e.relays.filter(x => x !== r);
  }));
  $$("[data-dows] .chip", div).forEach(c => c.addEventListener("click", () => {
    const d = +c.dataset.dow; c.classList.toggle("sel");
    e.dow = c.classList.contains("sel") ? [...e.dow, d] : e.dow.filter(x => x !== d);
  }));
  $("[data-time]", div).addEventListener("change", ev => {
    const p = (ev.target.value || "00:00:00").split(":").map(Number);
    e.hour = p[0] || 0; e.minute = p[1] || 0; e.second = p[2] || 0;
  });
  $("[data-act]", div).addEventListener("change", ev => e.action = ev.target.value);
  $("[data-del]", div).addEventListener("click", () => { events.splice(idx, 1); renderEvents(); });
  return div;
}
function renderEvents() {
  const list = $("#eventList");
  list.innerHTML = "";
  if (!events.length) { list.innerHTML = `<p class="hint">No events yet. Add one to get started.</p>`; return; }
  events.forEach((e, i) => list.appendChild(eventCard(e, i)));
}
$("#addEvent").addEventListener("click", () => {
  events.push({ enabled: true, type: "weekly", action: "on", relays: [], dow: [],
                hour: 8, minute: 0, second: 0, day: 1, month: 1, year: 2026 });
  renderEvents();
});
$("#saveSchedule").addEventListener("click", async () => {
  const payload = { events: events.map(e => ({ ...e, type: "weekly" })) };
  try { await post("/api/schedule", payload); flash($("#schedMsg"), "Schedule saved."); }
  catch (e) { flash($("#schedMsg"), "Failed: " + e.message, false); }
});
async function loadConfig() {
  try { const c = await api("/api/config"); events = (c.events || []).filter(e => e.type === "weekly"); renderEvents(); }
  catch (e) { console.error(e); }
}

/* ---------------- Boot ---------------- */
loadTz();
loadConfig();
refreshStatus();
setInterval(refreshStatus, 2000);

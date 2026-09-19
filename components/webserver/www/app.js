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
  auto_on:   { txt: "Auto · On",     on: true,  cls: ""       },
  auto_off:  { txt: "Auto · Off",    on: false, cls: ""       },
  macro_on:  { txt: "Macro · On",    on: true,  cls: "macro"  },
  macro_off: { txt: "Macro · Off",   on: false, cls: "macro"  },
  manual_on: { txt: "Manual · On",   on: true,  cls: "manual" },
  manual_off:{ txt: "Manual · Off",  on: false, cls: "manual" },
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
  st.className = "state " + (info.on ? "on" : "off") + (info.cls ? " " + info.cls : "");
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
  renderMacroStatus(s.macro);

  $("#fwVersion").textContent = s.fw_version || "—";
  $("#fwPartition").textContent = s.fw_partition || "—";
  $("#sysTime").textContent = s.time_valid ? s.local : "not set";
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
let macros = [];   // [{index, name, steps:[{delay, action, relays:[]}]}]

function hms(e) {
  const p = n => String(n).padStart(2, "0");
  return `${p(e.hour)}:${p(e.minute)}:${p(e.second)}`;
}
function eventCard(e, idx) {
  const div = document.createElement("div");
  div.className = "event";
  const isMacro = e.target === "macro";
  const relayChips = [1,2,3,4,5,6].map(r =>
    `<span class="chip ${e.relays.includes(r) ? "sel" : ""}" data-relay="${r}">${r}</span>`).join("");
  const dowChips = DOW.map((d, i) =>
    `<span class="chip ${e.dow.includes(i) ? "sel" : ""}" data-dow="${i}">${d}</span>`).join("");
  const macroOpts = macros.length
    ? macros.map(m => `<option value="${m.index}" ${e.macro === m.index ? "selected" : ""}>${m.name || ("Macro " + m.index)}</option>`).join("")
    : `<option value="">(no macros defined)</option>`;

  const targetBlock = `
    <div class="grp"><span>Do</span>
      <select class="actsel" data-target>
        <option value="relays" ${!isMacro ? "selected" : ""}>Set relays</option>
        <option value="macro" ${isMacro ? "selected" : ""}>Start macro</option>
      </select></div>`;
  const relayActionBlock = `
    <div class="grp"><span>Relays</span><div class="chips" data-relays>${relayChips}</div></div>
    <div class="grp"><span>Action</span>
      <select class="actsel" data-act>
        <option value="on" ${e.action === "on" ? "selected" : ""}>Enable (NO)</option>
        <option value="off" ${e.action === "off" ? "selected" : ""}>Disable (NC)</option>
      </select></div>`;
  const macroBlock = `
    <div class="grp"><span>Macro</span>
      <select class="actsel" data-macro>${macroOpts}</select></div>`;

  div.innerHTML = `
    <div class="erow">
      <label class="switch"><input type="checkbox" ${e.enabled ? "checked" : ""} data-en>
        <span class="track"></span></label>
      ${targetBlock}
      ${isMacro ? macroBlock : relayActionBlock}
      <div class="grp"><span>Days</span><div class="chips" data-dows>${dowChips}</div></div>
      <div class="grp"><span>Time</span><input class="timeinput" type="time" step="1" value="${hms(e)}" data-time></div>
      <button class="del" data-del>Delete</button>
    </div>`;

  $("[data-en]", div).addEventListener("change", ev => e.enabled = ev.target.checked);
  $("[data-target]", div).addEventListener("change", ev => { e.target = ev.target.value; renderEvents(); });
  if (isMacro) {
    const ms = $("[data-macro]", div);
    if (ms && macros.length) { e.macro = +ms.value; ms.addEventListener("change", ev => e.macro = +ev.target.value); }
  } else {
    $$("[data-relays] .chip", div).forEach(c => c.addEventListener("click", () => {
      const r = +c.dataset.relay; c.classList.toggle("sel");
      e.relays = c.classList.contains("sel") ? [...e.relays, r] : e.relays.filter(x => x !== r);
    }));
    $("[data-act]", div).addEventListener("change", ev => e.action = ev.target.value);
  }
  $$("[data-dows] .chip", div).forEach(c => c.addEventListener("click", () => {
    const d = +c.dataset.dow; c.classList.toggle("sel");
    e.dow = c.classList.contains("sel") ? [...e.dow, d] : e.dow.filter(x => x !== d);
  }));
  $("[data-time]", div).addEventListener("change", ev => {
    const p = (ev.target.value || "00:00:00").split(":").map(Number);
    e.hour = p[0] || 0; e.minute = p[1] || 0; e.second = p[2] || 0;
  });
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
  events.push({ enabled: true, type: "weekly", target: "relays", action: "on", macro: macros[0] ? macros[0].index : 0,
                relays: [], dow: [], hour: 8, minute: 0, second: 0, day: 1, month: 1, year: 2026 });
  renderEvents();
});
$("#saveSchedule").addEventListener("click", async () => {
  const payload = { events: events.map(e => ({ ...e, type: "weekly" })) };
  try { await post("/api/schedule", payload); flash($("#schedMsg"), "Schedule saved."); }
  catch (e) { flash($("#schedMsg"), "Failed: " + e.message, false); }
});
async function loadConfig() {
  try {
    const c = await api("/api/config");
    macros = (c.macros || []).map(m => ({ index: m.index, name: m.name || "", loop: m.loop != null ? m.loop : 1,
      steps: (m.steps || []).map(s => ({ delay: s.delay || 0, call: s.call != null ? s.call : -1,
        action: s.action || "on", relays: s.relays || [] })) }));
    events = (c.events || []).filter(e => e.type === "weekly");
    renderMacros();
    renderEvents();
  } catch (e) { console.error(e); }
}

/* ---------------- Macros ---------------- */
function macroStepChips(step) {
  return [1,2,3,4,5,6].map(r =>
    `<span class="chip ${step.relays.includes(r) ? "sel" : ""}" data-srelay="${r}">${r}</span>`).join("");
}
function stepRow(m, step, si) {
  const row = document.createElement("div");
  row.className = "mstep";
  const isCall = step.call != null && step.call >= 0;
  const callOpts = macros.length
    ? macros.map(x => `<option value="${x.index}" ${step.call === x.index ? "selected" : ""}>${x.name || ("Macro " + x.index)}</option>`).join("")
    : `<option value="">(no macros)</option>`;
  const typeBlock = `
    <div class="grp"><span>Do</span>
      <select class="actsel" data-stype>
        <option value="relays" ${!isCall ? "selected" : ""}>Set relays</option>
        <option value="call" ${isCall ? "selected" : ""}>Call macro</option>
      </select></div>`;
  const relayBlock = `
    <div class="grp"><span>Relays</span><div class="chips" data-srelays>${macroStepChips(step)}</div></div>
    <div class="grp"><span>Action</span>
      <select class="actsel" data-saction>
        <option value="on" ${step.action === "on" ? "selected" : ""}>On (NO)</option>
        <option value="off" ${step.action === "off" ? "selected" : ""}>Off (NC)</option>
        <option value="auto" ${step.action === "auto" ? "selected" : ""}>Auto</option>
      </select></div>`;
  const callBlock = `<div class="grp"><span>Macro</span><select class="actsel" data-scall>${callOpts}</select></div>`;

  row.innerHTML = `
    <div class="grp"><span>Wait (s)</span>
      <input type="number" min="0" class="delayinput" value="${step.delay}" data-delay></div>
    ${typeBlock}
    ${isCall ? callBlock : relayBlock}
    <button class="del" data-delstep>Delete</button>`;

  $("[data-delay]", row).addEventListener("change", ev => step.delay = Math.max(0, +ev.target.value || 0));
  $("[data-stype]", row).addEventListener("change", ev => {
    step.call = ev.target.value === "call" ? (macros.length ? macros[0].index : 0) : -1;
    renderMacros();
  });
  if (isCall) {
    const cs = $("[data-scall]", row);
    if (cs && macros.length) { step.call = +cs.value; cs.addEventListener("change", ev => step.call = +ev.target.value); }
  } else {
    $$("[data-srelays] .chip", row).forEach(c => c.addEventListener("click", () => {
      const r = +c.dataset.srelay; c.classList.toggle("sel");
      step.relays = c.classList.contains("sel") ? [...step.relays, r] : step.relays.filter(x => x !== r);
    }));
    $("[data-saction]", row).addEventListener("change", ev => step.action = ev.target.value);
  }
  $("[data-delstep]", row).addEventListener("click", () => { m.steps.splice(si, 1); renderMacros(); });
  return row;
}
function macroCard(m, mi) {
  const div = document.createElement("div");
  div.className = "macro";
  div.innerHTML = `
    <div class="mhead">
      <input class="mname" value="${m.name || ""}" placeholder="Macro name" data-name>
      <label class="mloop" title="Number of passes over the steps; 0 = forever">Loop
        <input type="number" min="0" class="loopinput" value="${m.loop != null ? m.loop : 1}" data-loop> ×</label>
      <div class="mbtns">
        <button class="btn" data-run>▶ Run</button>
        <button class="btn" data-step>⏭ Step</button>
        <button class="btn" data-stop>■ Stop</button>
        <button class="del" data-delmacro>Delete</button>
      </div>
    </div>
    <div class="steps" data-steps></div>
    <button class="btn addstep" data-addstep>+ Add step</button>`;
  $("[data-name]", div).addEventListener("change", ev => m.name = ev.target.value);
  $("[data-loop]", div).addEventListener("change", ev => m.loop = Math.max(0, +ev.target.value || 0));
  const steps = $("[data-steps]", div);
  if (!m.steps.length) steps.innerHTML = `<p class="hint">No steps yet.</p>`;
  m.steps.forEach((s, si) => steps.appendChild(stepRow(m, s, si)));
  $("[data-addstep]", div).addEventListener("click", () => { m.steps.push({ delay: 5, call: -1, action: "on", relays: [] }); renderMacros(); });
  $("[data-delmacro]", div).addEventListener("click", () => { macros.splice(mi, 1); renderMacros(); renderEvents(); });
  $("[data-run]", div).addEventListener("click", () => macroControl("start", mi));
  $("[data-step]", div).addEventListener("click", () => macroControl("step", mi));
  $("[data-stop]", div).addEventListener("click", () => macroControl("stop", mi));
  return div;
}
function renderMacros() {
  const list = $("#macroList");
  if (!list) return;
  list.innerHTML = "";
  if (!macros.length) { list.innerHTML = `<p class="hint">No macros yet. Add one to get started.</p>`; return; }
  macros.forEach((m, i) => list.appendChild(macroCard(m, i)));
}
function macrosPayload() {
  const toStep = s => (s.call != null && s.call >= 0)
    ? { delay: +s.delay || 0, call: s.call }
    : { delay: +s.delay || 0, action: s.action, relays: s.relays };
  return { macros: macros.map(m => ({ name: m.name, loop: m.loop != null ? m.loop : 1, steps: m.steps.map(toStep) })) };
}
async function saveMacros(silent) {
  await post("/api/macros", macrosPayload());
  if (!silent) flash($("#macroMsg"), "Macros saved.");
}
$("#addMacro").addEventListener("click", () => { macros.push({ index: macros.length, name: "", loop: 1, steps: [] }); renderMacros(); });
$("#saveMacros").addEventListener("click", async () => {
  try { await saveMacros(false); await loadConfig(); }
  catch (e) { flash($("#macroMsg"), "Failed: " + e.message, false); }
});
// Save current definitions first (so server slots match the editor), then send
// the control command referencing the macro by its list position.
async function macroControl(action, mi) {
  try {
    if (action !== "stop") await saveMacros(true);
    const body = action === "stop" ? { action } : { action, macro: mi };
    await post("/api/macro", body);
    refreshStatus();
  } catch (e) { flash($("#macroMsg"), "Failed: " + e.message, false); }
}
function renderMacroStatus(mac) {
  const el = $("#macroActive");
  if (el) {
    if (mac && mac.active) {
      el.innerHTML = `<span class="dot on"></span> Running <b>${mac.name || ("Macro " + mac.index)}</b>
        — step ${mac.step}/${mac.steps} (${mac.run})
        <button class="btn" id="macStopInline">■ Stop</button>`;
      const b = $("#macStopInline"); if (b) b.onclick = () => macroControl("stop");
    } else {
      el.innerHTML = `<span class="dot"></span> No macro running.`;
    }
    el.hidden = false;
  }
  // Header pill, visible from any tab while a macro runs.
  let mp = $("#pill-macro");
  if (mac && mac.active) {
    if (!mp) { mp = document.createElement("span"); mp.id = "pill-macro"; $("#pills").appendChild(mp); }
    mp.className = "pill ok"; mp.textContent = "▶ " + (mac.name || "Macro");
  } else if (mp) { mp.remove(); }
}

/* ---------------- System / OTA ---------------- */
$("#fwUpload").addEventListener("click", () => {
  const f = $("#fwFile").files[0];
  if (!f) { flash($("#otaMsg"), "Choose a .bin file first.", false); return; }

  const wrap = $("#fwProgWrap"), bar = $("#fwProg"), msg = $("#otaMsg");
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/ota");
  xhr.setRequestHeader("Content-Type", "application/octet-stream");
  wrap.hidden = false; bar.style.width = "0%";
  msg.className = "msg"; msg.textContent = "Uploading…";
  $("#fwUpload").disabled = true;

  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) bar.style.width = Math.round((e.loaded / e.total) * 100) + "%";
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      bar.style.width = "100%";
      flash($("#otaMsg"), "Installed — device is rebooting. Reload this page in ~15 s.");
    } else {
      $("#fwUpload").disabled = false;
      flash($("#otaMsg"), "Update failed: " + (xhr.responseText || xhr.status), false);
    }
  };
  xhr.onerror = () => {
    // The socket often drops as the device reboots right after accepting the image.
    $("#fwUpload").disabled = false;
    flash($("#otaMsg"), "Connection closed — if the upload reached 100%, the device is rebooting.", false);
  };
  xhr.send(f);
});

/* ---------------- Boot ---------------- */
loadTz();
loadConfig();
refreshStatus();
setInterval(refreshStatus, 2000);

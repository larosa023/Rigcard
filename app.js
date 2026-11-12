// Rigcard share page. Renders a card from:
//   #d=<base64url(raw DEFLATE(JSON))>   quick links (everything is in the URL; nothing is sent anywhere)
//   #g=<gist id>                         hosted cards: a secret gist with rigcard.json, live.json, photo.txt
// Everything is drawn with textContent (never innerHTML), so a card can't inject markup.
"use strict";

const app = document.getElementById("app");
let liveTimer = null, refreshTimer = null, current = null, liveUrl = null;

const FONTS = {
  "Segoe UI Variable Text": '"Segoe UI Variable Text", "Segoe UI", system-ui, -apple-system, Inter, Roboto, sans-serif',
  "Segoe UI Variable Display": '"Segoe UI Variable Display", "Segoe UI", system-ui, -apple-system, Inter, Roboto, sans-serif',
  "Bahnschrift": 'Bahnschrift, "DIN Alternate", "Barlow Semi Condensed", "Roboto Condensed", system-ui, sans-serif',
  "Cascadia Code": '"Cascadia Code", "SF Mono", Menlo, Consolas, monospace',
  "Consolas": 'Consolas, "SF Mono", Menlo, monospace',
  "Georgia": 'Georgia, "Times New Roman", serif',
};
const font = (f) => FONTS[f] || `"${String(f || "").replace(/["\\]/g, "")}", "Segoe UI", system-ui, sans-serif`;
const hex = (v, d) => (/^#[0-9a-fA-F]{6}$/.test(v || "") ? v : d);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);

function showError(msg, withHome) {
  app.replaceChildren();
  const p = el("p", "error", msg);
  if (withHome) { p.append(" "); const a = el("a", null, "What's Rigcard?"); a.href = location.pathname; p.append(a); }
  app.append(p);
}

function b64urlBytes(s) {
  s = s.replace(/-/g, "+").replace(/_/g, "/");
  while (s.length % 4) s += "=";
  const bin = atob(s);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

async function inflate(bytes) {
  const ds = new DecompressionStream("deflate-raw");
  const text = await new Response(new Blob([bytes]).stream().pipeThrough(ds)).text();
  return JSON.parse(text);
}

async function fileText(f) {
  if (!f) return null;
  if (!f.truncated && typeof f.content === "string") return f.content;
  const r = await fetch(f.raw_url);
  return r.ok ? r.text() : null;
}

async function loadGist(id) {
  const r = await fetch(`https://api.github.com/gists/${encodeURIComponent(id)}`, { headers: { Accept: "application/vnd.github+json" } });
  if (r.status === 404) throw new Error("This card isn't shared anymore (its owner deleted it).");
  if (r.status === 403 || r.status === 429) throw new Error("GitHub is rate-limiting this network right now. Try again in a few minutes.");
  if (!r.ok) throw new Error(`Couldn't load the card (GitHub said ${r.status}).`);
  const g = await r.json();
  const card = JSON.parse(await fileText(g.files["rigcard.json"]));
  const liveText = await fileText(g.files["live.json"]);
  if (liveText) { try { card.liveData = JSON.parse(liveText); } catch (_) {} }
  const photo = await fileText(g.files["photo.txt"]);
  if (photo && photo.startsWith("data:image/")) card.photoUrl = photo;
  const owner = g.owner && g.owner.login;
  if (owner && g.files["live.json"]) liveUrl = `https://gist.githubusercontent.com/${owner}/${id}/raw/live.json`;
  card.owner = owner;
  return card;
}

function fmt(v, unit, dec = 0) {
  const n = num(v);
  return n == null ? { v: "—", u: "", na: true } : { v: n.toFixed(dec), u: unit, na: false };
}

function uptime(s) {
  s = num(s) || 0;
  const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return d ? `${d}d ${h}h` : h ? `${h}h ${m}m` : `${m}m`;
}

function heat(t, acc) { return t == null ? acc : t >= 85 ? "#ff5d5d" : t >= 72 ? "#ffb347" : acc; }

function ago(t) {
  const s = Math.max(0, Math.round(Date.now() / 1000 - t));
  if (s < 60) return `${s} s ago`;
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86400) return `${Math.round(s / 3600)} h ago`;
  return new Date(t * 1000).toLocaleDateString();
}

function renderLive(card, root) {
  const L = card.liveData || card.live;
  const panel = root.querySelector("#live");
  if (!L) { panel.classList.add("hidden"); return; }
  const st = card.style || {};
  const acc = hex(st.acc, "#6c8cff"), acc2 = hex(st.acc2, "#b06cff");
  const tiles = root.querySelector("#tiles");
  tiles.replaceChildren();
  const lc = (load, mhz) => [num(load) != null ? `${Math.round(load)}% load` : "", num(mhz) ? `${(mhz / 1000).toFixed(2)} GHz` : ""].filter(Boolean).join(" · ");
  const cpuT = num(L.cpuT), gpuT = num(L.gpuT), cpuW = num(L.cpuW), gpuW = num(L.gpuW);
  const total = cpuW == null && gpuW == null ? null : (cpuW || 0) + (gpuW || 0);
  const list = [
    ["CPU", fmt(cpuT, "°C"), lc(L.cpuL, L.cpuMhz), cpuT != null ? cpuT / 100 : (num(L.cpuL) || 0) / 100, heat(cpuT, acc)],
    ["GPU", fmt(gpuT, "°C"), lc(L.gpuL, L.gpuMhz), gpuT != null ? gpuT / 100 : (num(L.gpuL) || 0) / 100, heat(gpuT, acc)],
    ["Power", fmt(total, "W"), [cpuW != null ? `CPU ${Math.round(cpuW)} W` : "", gpuW != null ? `GPU ${Math.round(gpuW)} W` : ""].filter(Boolean).join(" · "), total != null ? Math.min(1, total / 800) : 0, acc2],
    ["Uptime", { v: L.up ? uptime(L.up) : "—", u: "", na: !L.up }, num(L.mem) != null ? `RAM ${Math.round(L.mem)}% used` : "", (num(L.mem) || 0) / 100, acc2],
  ];
  for (const [label, val, sub, bar, color] of list) {
    const t = el("div", "tile");
    t.style.setProperty("--c", color);
    t.append(el("small", null, label.toUpperCase()));
    const b = el("b", val.na ? "na" : null, val.v);
    t.append(b);
    if (val.u) t.append(el("em", null, val.u));
    t.append(el("p", null, sub || "\u00a0"));
    const br = el("div", "bar"), i = el("i");
    i.style.width = `${Math.round(Math.max(0, Math.min(1, bar)) * 100)}%`;
    br.append(i);
    t.append(br);
    tiles.append(t);
  }
  const badge = root.querySelector("#livebadge"), seen = root.querySelector("#seen");
  const mode = (card.live && card.live.mode) || "snapshot";
  const at = num(L.at) || card.updated;
  const stale = Date.now() / 1000 - at > 15 * 60;
  if (mode === "live" && !stale) { badge.className = "badge live"; badge.textContent = "LIVE"; seen.textContent = `Updated ${ago(at)}`; }
  else if (mode === "live") { badge.className = "badge off"; badge.textContent = "OFFLINE"; seen.textContent = `Last seen ${ago(at)}`; }
  else { badge.className = "badge snap"; badge.textContent = "SNAPSHOT"; seen.textContent = at ? `Snapshot from ${new Date(at * 1000).toLocaleString()}` : ""; }
}

function render(card) {
  current = card;
  const st = card.style || {};
  const tpl = document.getElementById("card-tpl").content.cloneNode(true);
  const root = tpl.querySelector("#card");
  const s = root.style;
  s.setProperty("--bg1", hex(st.bg1, "#0b0d14"));
  s.setProperty("--bg2", st.bg === "solid" ? hex(st.bg1, "#0b0d14") : hex(st.bg2, "#1a1f33"));
  s.setProperty("--angle", `${num(st.angle) != null ? st.angle + 90 : 135}deg`);
  const panelHex = hex(st.panel, "#ffffff"), op = num(st.panelOp) != null ? st.panelOp : 0.05;
  s.setProperty("--panel", `color-mix(in srgb, ${panelHex} ${Math.round(op * 100)}%, transparent)`);
  s.setProperty("--text", hex(st.text, "#f4f6fb"));
  s.setProperty("--sub", hex(st.sub, "#9aa3b8"));
  s.setProperty("--acc", hex(st.acc, "#6c8cff"));
  s.setProperty("--acc2", hex(st.acc2, "#b06cff"));
  s.setProperty("--r", `${Math.max(0, Math.min(40, num(st.r) ?? 18)) * 0.9}px`);
  s.setProperty("--font", font(st.font));
  s.setProperty("--head", font(st.head));
  const textHex = hex(st.text, "#f4f6fb");
  const lum = parseInt(textHex.slice(1, 3), 16) * 0.2126 + parseInt(textHex.slice(3, 5), 16) * 0.7152 + parseInt(textHex.slice(5, 7), 16) * 0.0722;
  if (lum < 115) { root.classList.add("light"); s.setProperty("--border", "rgba(0,0,0,.08)"); }
  if (st.border === false) s.setProperty("--border", "transparent");
  if (st.glow) { root.classList.add("glow"); s.setProperty("--border", `color-mix(in srgb, ${hex(st.acc, "#6c8cff")} 35%, transparent)`); }
  if (st.layout === "showcase") root.classList.add("showcase");
  if (st.layout === "minimal") root.classList.add("minimal");
  document.body.style.background = hex(st.bg1, "#07080c");

  const show = card.show || {};
  const head = root.querySelector(".head");
  const photo = root.querySelector("#photo");
  if (card.photoUrl && show.photo !== false) {
    photo.style.backgroundImage = `url("${card.photoUrl}")`;
    const p = card.photo || {};
    const zoom = Math.max(1, Math.min(4, num(p.zoom) || 1));
    photo.style.backgroundSize = zoom > 1.001 ? `${zoom * 100}%` : "cover";
    photo.style.backgroundPosition = `${(num(p.x) ?? 0.5) * 100}% ${(num(p.y) ?? 0.5) * 100}%`;
    photo.setAttribute("aria-label", `Photo of ${card.name || "the build"}`);
    if (st.bg === "photo") {
      root.classList.add("photo-bg");
      const blur = el("div", "blur");
      blur.style.backgroundImage = `url("${card.photoUrl}")`;
      root.prepend(blur);
    }
  } else {
    head.classList.add("nophoto");
    root.classList.remove("showcase");
  }

  root.querySelector("#name").textContent = card.name || "My Build";
  root.querySelector("#sub").textContent = card.sub || "";
  document.title = `${card.name || "My Build"} · Rigcard`;
  const chips = root.querySelector("#chips");
  const parts = Array.isArray(card.parts) ? card.parts : [];
  const short = (p) => p.n.replace(/^(AMD |Intel\(R\) |Intel |NVIDIA |GeForce |Radeon\(TM\) )+/g, "");
  for (const k of ["cpu", "gpu", "ram"]) { const p = parts.find((x) => x.k === k); if (p) chips.append(el("span", "chip", short(p))); }

  const pl = root.querySelector("#partlist");
  if (show.parts === false || !parts.length) root.querySelector("#parts").classList.add("hidden");
  for (const p of parts) {
    pl.append(el("dt", null, p.l || p.k));
    const dd = el("dd");
    dd.append(el("b", null, p.n));
    if (p.d) dd.append(el("span", null, p.d));
    pl.append(dd);
  }

  if (show.live === false) root.querySelector("#live").classList.add("hidden");
  else renderLive(card, root);

  const bl = root.querySelector("#benchlist");
  const bench = Array.isArray(card.bench) ? card.bench : [];
  if (show.bench === false) root.querySelector("#bench").classList.add("hidden");
  if (!bench.length) bl.append(el("li", "empty", "No benchmarks yet"));
  for (const r of bench) {
    const li = el("li"), row = el("div", "row");
    row.append(el("span", "n", r.n), el("span", "score", r.f || String(r.s)));
    li.append(row, el("p", "src", r.src || ""));
    const bar = el("div", "bar"), i = el("i");
    i.style.width = "100%";
    bar.append(i);
    li.append(bar);
    bl.append(li);
  }
  // bars: relative to the best score with the same unit
  const byUnit = {};
  bench.forEach((r) => { const u = (r.f || "").replace(/[\d.,\s]/g, ""); byUnit[u] = Math.max(byUnit[u] || 0, num(r.s) || 0); });
  bl.querySelectorAll("li .bar i").forEach((i, k) => {
    const r = bench[k];
    if (!r) return;
    const u = (r.f || "").replace(/[\d.,\s]/g, "");
    i.style.width = `${Math.max(6, Math.round(((num(r.s) || 0) / (byUnit[u] || 1)) * 100))}%`;
  });

  // single visible section: use the full width
  const visible = [...root.querySelectorAll(".body .panel")].filter((p) => !p.classList.contains("hidden"));
  const body = root.querySelector("#body");
  if (!visible.length) body.remove();
  else if (visible.length < 3 && innerWidth > 960) body.style.gridTemplateColumns = visible.map((p) => (p.id === "parts" ? "43fr" : p.id === "live" ? "30fr" : "34fr")).join(" ");

  root.querySelector("#gen").textContent = card.gen ? `Made with ${card.gen}` : "";
  const copy = tpl.querySelector("#copy");
  copy.addEventListener("click", async () => {
    try { await navigator.clipboard.writeText(location.href); copy.textContent = "Link copied"; } catch (_) { copy.textContent = "Copy failed"; }
    setTimeout(() => (copy.textContent = "Copy link"), 1800);
  });
  app.replaceChildren(tpl);
}

async function pollLive() {
  if (!liveUrl || !current) return;
  try {
    const r = await fetch(`${liveUrl}?t=${Date.now()}`, { cache: "no-store" });
    if (!r.ok) return;
    current.liveData = await r.json();
    const root = document.getElementById("card");
    if (root && !(current.show && current.show.live === false)) renderLive(current, root);
  } catch (_) { /* offline: keep what we have */ }
}

async function start() {
  clearInterval(liveTimer);
  clearInterval(refreshTimer);
  liveUrl = null;
  const hash = new URLSearchParams(location.hash.slice(1));
  try {
    if (hash.get("d")) {
      if (typeof DecompressionStream === "undefined") return showError("This browser is too old to open quick links. Try a current Chrome, Edge, Firefox or Safari.");
      render(await inflate(b64urlBytes(hash.get("d"))));
    } else if (hash.get("g")) {
      const id = hash.get("g");
      if (!/^[0-9a-f]{20,40}$/i.test(id)) return showError("That share link looks incomplete.", true);
      render(await loadGist(id));
      if (current.live && current.live.mode === "live") liveTimer = setInterval(pollLive, 30000);
      refreshTimer = setInterval(async () => { try { render(await loadGist(id)); } catch (_) {} }, 5 * 60000);
    } else {
      document.title = "Rigcard";
      app.replaceChildren(document.getElementById("home-tpl").content.cloneNode(true));
    }
  } catch (e) {
    showError(e && e.message ? e.message : "Couldn't open this card.", true);
  }
}

addEventListener("hashchange", start);
start();

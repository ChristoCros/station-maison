const $ = id => document.getElementById(id);

const mainEl = $('main');
const solarEl = $('solar');
const mainOkEl = $('mainOk');
const solarOkEl = $('solarOk');
const sysInfo = $('sysInfo');

const history = [];
const HISTORY_MAX = 120;

function fmt(v) { return v == null || isNaN(v) ? '--' : Math.round(v); }

function setOk(el, ok) {
  el.classList.toggle('stale', !ok);
  el.title = ok ? 'OK' : 'Erreur de lecture';
}

function drawChart() {
  const c = $('chart');
  const ctx = c.getContext('2d');
  const w = c.clientWidth, h = c.clientHeight;
  if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
  ctx.clearRect(0, 0, w, h);
  if (history.length < 2) return;

  const all = history.flatMap(p => [p.main, p.solar]);
  let mn = Math.min(...all), mx = Math.max(...all);
  const pad = Math.max(50, (mx - mn) * 0.1);
  mn -= pad; mx += pad;
  if (mn > 0) mn = 0;
  if (mx < 0) mx = 0;
  const range = mx - mn || 1;

  const yZero = h - ((0 - mn) / range) * h;
  ctx.strokeStyle = '#2a323c';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(0, yZero); ctx.lineTo(w, yZero); ctx.stroke();

  ctx.fillStyle = '#888';
  ctx.font = '10px sans-serif';
  ctx.fillText('0W', 4, yZero - 2);

  const drawSerie = (key, color) => {
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    history.forEach((p, i) => {
      const x = (i / (HISTORY_MAX - 1)) * w;
      const y = h - ((p[key] - mn) / range) * h;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  };
  drawSerie('main', '#fdd835');
  drawSerie('solar', '#4caf50');
}

function pushPoint(main, solar) {
  history.push({ main, solar });
  if (history.length > HISTORY_MAX) history.shift();
  drawChart();
}

let ws;
function connectWs() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onmessage = e => {
    try {
      const m = JSON.parse(e.data);
      mainEl.textContent = fmt(m.powerMain);
      solarEl.textContent = fmt(m.powerSolar);
      setOk(mainOkEl, m.mainOk);
      setOk(solarOkEl, m.solarOk);
      pushPoint(m.powerMain || 0, m.powerSolar || 0);
    } catch (ex) {}
  };
  ws.onclose = (e) => {
    if (e.code === 4001) { location.href = '/login.html'; return; }
    setTimeout(connectWs, 2000);
  };
  ws.onerror = () => { try { ws.close(); } catch (e) {} };
}

async function loadStatus() {
  try {
    const r = await fetch('/api/status');
    if (r.status === 401) { location.href = '/login.html'; return; }
    const j = await r.json();
    if (!$('ssid').matches(':focus')) $('ssid').value = j.wifi_ssid || '';
    if (!$('urlMain').matches(':focus')) $('urlMain').value = j.shelly_main_url || '';
    if (!$('urlSolar').matches(':focus')) $('urlSolar').value = j.shelly_solar_url || '';
    $('otaPass').value = j.ota_password || '';
    if (j.first_boot) $('firstBootWarn').hidden = false;
    sysInfo.innerHTML = `
      <li>IP : <code>${j.ip}</code></li>
      <li>WiFi connecté : ${j.sta_connected ? 'oui' : 'non'}</li>
      <li>Mode AP : ${j.ap_mode ? 'oui' : 'non'}</li>
      <li>Heap libre : ${(j.heap_free / 1024).toFixed(1)} Ko</li>
      <li>Uptime : ${Math.floor(j.uptime_s / 60)} min</li>
    `;
  } catch (ex) {
    sysInfo.innerHTML = '<li class="err">Erreur de chargement</li>';
  }
}

async function postJson(url, body) {
  const r = await fetch(url, {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify(body)
  });
  const text = await r.text();
  let json = {};
  try { json = text ? JSON.parse(text) : {}; } catch (e) {}
  return { ok: r.ok, status: r.status, json };
}

$('wifiForm').addEventListener('submit', async e => {
  e.preventDefault();
  if (!confirm('Le module va redémarrer. Continuer ?')) return;
  const r = await postJson('/api/wifi', { ssid: $('ssid').value, pass: $('wpass').value });
  if (r.ok) alert('Redémarrage en cours, reconnecte-toi dans 30s.');
  else alert('Erreur : ' + (r.json.error || r.status));
});

$('shellyForm').addEventListener('submit', async e => {
  e.preventDefault();
  const r = await postJson('/api/shelly', { main: $('urlMain').value, solar: $('urlSolar').value });
  alert(r.ok ? 'Enregistré' : ('Erreur : ' + (r.json.error || r.status)));
});

$('pwdForm').addEventListener('submit', async e => {
  e.preventDefault();
  const r = await postJson('/api/password', { old: $('oldPass').value, new: $('newPass').value });
  if (r.ok) {
    alert('Mot de passe changé');
    $('oldPass').value = ''; $('newPass').value = '';
    $('firstBootWarn').hidden = true;
  } else {
    alert('Erreur : ' + (r.json.error || r.status));
  }
});

$('copyOta').addEventListener('click', async () => {
  try {
    await navigator.clipboard.writeText($('otaPass').value);
    $('copyOta').textContent = 'Copié';
    setTimeout(() => $('copyOta').textContent = 'Copier', 1500);
  } catch (e) {
    $('otaPass').select();
    document.execCommand('copy');
  }
});

$('regenOta').addEventListener('click', async () => {
  if (!confirm('Régénérer le mot de passe OTA va redémarrer l\'ESP32. Note bien le nouveau avant. Continuer ?')) return;
  const r = await postJson('/api/ota-password/regenerate', {});
  if (r.ok) {
    $('otaPass').value = r.json.ota_password;
    alert('Nouveau mot de passe OTA : ' + r.json.ota_password + '\n\nRedémarrage en cours.');
  } else {
    alert('Erreur : ' + (r.json.error || r.status));
  }
});

$('logout').addEventListener('click', async () => {
  await postJson('/api/logout', {});
  location.href = '/login.html';
});

window.addEventListener('resize', drawChart);
loadStatus();
connectWs();
setInterval(loadStatus, 30000);

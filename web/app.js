'use strict';

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];

const VALID_VIEWS = new Set([
  'overview',
  'channels',
  'guide',
  'viewers',
  'playlist',
  'settings',
  'diagnostics'
]);

const state = {
  status: null,
  activeView: 'overview',
  channelSearch: '',
  channelType: 'all',
  guideSearch: '',
  guideLcn: null,
  guidePayload: null,
  guideLoading: false,
  statusLoading: false,
  settingsDirty: false,
  lastError: '',
  toastTimer: null,
  authReady: false,
  authRequired: false,
  authenticated: false,
  loginVisible: false,
  dialogResolve: null,
  dialogCancelable: true,
  dialogPreviousFocus: null
};


const SATELLITE_PRESETS = Object.freeze({
  'hispasat-30w': {label: 'Hispasat 30.0°W', orbital: 300, west: true, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 43},
  'eutelsat-5w': {label: 'Eutelsat 5.0°W', orbital: 50, west: true, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 22},
  'amos-4w': {label: 'Amos 4.0°W', orbital: 40, west: true, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 6},
  'thor-0.8w': {label: 'Thor 0.8°W', orbital: 8, west: true, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 66},
  'bulgariasat-1.9e': {label: 'BulgariaSat 1.9°E', orbital: 19, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 14},
  'astra-4.8e': {label: 'Astra 4.8°E', orbital: 48, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 21},
  'eutelsat-7e': {label: 'Eutelsat 7.0°E', orbital: 70, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 166},
  'eutelsat-9e': {label: 'Eutelsat 9.0°E', orbital: 90, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 29},
  'hotbird-13e': {label: 'Hot Bird 13.0°E', orbital: 130, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 102},
  'eutelsat-16e': {label: 'Eutelsat 16.0°E', orbital: 160, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 79},
  'astra-19.2e': {label: 'Astra 19.2°E', orbital: 192, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 99, builtInSweep: true},
  'astra-23.5e': {label: 'Astra 23.5°E', orbital: 235, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 72},
  'badr-26e': {label: 'Badr 26.0°E', orbital: 260, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 76},
  'astra-28.2e': {label: 'Astra 28.2°E', orbital: 282, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 85},
  'eutelsat-33e': {label: 'Eutelsat 33.0°E', orbital: 330, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 25},
  'hellas-sat-39e': {label: 'Hellas Sat 39.0°E', orbital: 390, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 55},
  'turksat-42e': {label: 'Türksat 42.0°E', orbital: 420, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 124},
  'intelsat-45e': {label: 'Intelsat 45.0°E', orbital: 450, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 4},
  'monacosat-52e': {label: 'MonacoSat 52.0°E', orbital: 520, west: false, low: 9750, high: 10600, switchMHz: 11700, tone: 'auto', transponders: 20}
});

function matchingSatellitePreset(config = {}) {
  if (config.satellite_preset && SATELLITE_PRESETS[config.satellite_preset]) {
    return config.satellite_preset;
  }
  const orbital = Number(config.orbital_tenths ?? 192);
  const west = Boolean(config.west);
  const low = Number(config.lnb_low_mhz ?? 9750);
  const high = Number(config.lnb_high_mhz ?? 10600);
  const switchMHz = Number(config.lnb_switch_mhz ?? 11700);
  const tone = config.tone || 'auto';
  return Object.entries(SATELLITE_PRESETS).find(([, preset]) =>
    preset.orbital === orbital && preset.west === west && preset.low === low &&
    preset.high === high && preset.switchMHz === switchMHz && preset.tone === tone
  )?.[0] || 'custom';
}

function updateSatellitePresetSummary() {
  const key = $('#setting-satellite-preset')?.value || 'custom';
  const preset = SATELLITE_PRESETS[key];
  const summary = $('#satellite-preset-summary');
  if (!summary) return;
  if (preset) {
    text(summary, `${preset.label} · ${preset.transponders} configured transponders · Universal LNB ${preset.low}/${preset.high} MHz`);
  } else {
    const orbital = Number($('#setting-orbital-tenths')?.value || 0) / 10;
    const direction = $('#setting-orbital-direction')?.value === '1' ? 'W' : 'E';
    text(summary, `Custom profile · ${orbital.toFixed(1)}°${direction}`);
  }
}

function applySatellitePreset(key) {
  const preset = SATELLITE_PRESETS[key];
  if (!preset) {
    updateSatellitePresetSummary();
    return;
  }
  $('#setting-orbital-tenths').value = preset.orbital;
  $('#setting-orbital-direction').value = preset.west ? '1' : '0';
  $('#setting-lnb-low').value = preset.low;
  $('#setting-lnb-high').value = preset.high;
  $('#setting-lnb-switch').value = preset.switchMHz;
  $('#setting-tone').value = preset.tone;
  $('#setting-device-scan-mode').value = '110';
  updateSatellitePresetSummary();
  updateScanMethodHelp();
}

function updateScanMethodHelp() {
  const presetKey = $('#setting-satellite-preset')?.value || 'custom';
  const mode = Number($('#setting-device-scan-mode')?.value || 0);
  const help = $('#scan-mode-help');
  if (!help) return;
  const preset = SATELLITE_PRESETS[presetKey];
  const tablePath = state.status?.config?.transponder_table_path || 'transponders.conf';
  if (mode === 110) {
    const source = preset?.builtInSweep
      ? `the [${presetKey}] section in ${tablePath}, with the built-in Astra table as a fallback`
      : `the [${presetKey}] section in ${tablePath}`;
    text(help, `${preset?.label || 'Custom satellite'} full sweep uses ${source}. It tunes every detailed frequency entry one by one and never silently falls back to the quick Device scan. Configured list size: ${preset?.transponders ?? 'custom'} transponders.`);
  } else {
    text(help, `Quick Device scan uses only the transponder table stored inside the receiver for ${preset?.label || 'the selected satellite'}. It can finish much faster and may find fewer services than a full preset sweep.`);
  }
}

function text(element, value) {
  if (element) element.textContent = String(value ?? '');
}

function formatDuration(secondsValue) {
  const seconds = Math.max(0, Math.floor(Number(secondsValue) || 0));
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const remainder = seconds % 60;
  return [hours, minutes, remainder].map((part) => String(part).padStart(2, '0')).join(':');
}

function formatClock(epochSeconds) {
  if (!epochSeconds) return '—';
  return new Date(Number(epochSeconds) * 1000).toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit'
  });
}

function formatDateTime(epochSeconds) {
  if (!epochSeconds) return '—';
  return new Date(Number(epochSeconds) * 1000).toLocaleString([], {
    dateStyle: 'medium',
    timeStyle: 'medium'
  });
}

function formatBytes(bytesValue) {
  const bytes = Math.max(0, Number(bytesValue) || 0);
  if (bytes < 1024) return `${bytes.toFixed(0)} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let amount = bytes;
  let unit = 'B';
  for (const candidate of units) {
    amount /= 1024;
    unit = candidate;
    if (amount < 1024) break;
  }
  return `${amount.toFixed(amount >= 100 ? 0 : amount >= 10 ? 1 : 2)} ${unit}`;
}

function programmeProgress(programme) {
  if (!programme?.start || !programme?.stop) return 0;
  const now = Date.now() / 1000;
  const total = Math.max(1, Number(programme.stop) - Number(programme.start));
  const elapsed = now - Number(programme.start);
  return Math.max(0, Math.min(10, Math.round((elapsed / total) * 10)));
}

function setTimelineProgress(element, programme) {
  if (!element) return;
  element.dataset.progress = String(programmeProgress(programme));
}

function toast(message, error = false) {
  const element = $('#toast');
  if (!element) return;
  text(element, message);
  element.classList.toggle('error', Boolean(error));
  element.classList.add('show');
  clearTimeout(state.toastTimer);
  state.toastTimer = window.setTimeout(() => element.classList.remove('show'), 3200);
}

function setAdminLoginVisible(visible, message = '') {
  const modal = $('#admin-login-modal');
  const error = $('#admin-login-error');
  if (!modal) return;
  state.loginVisible = Boolean(visible);
  modal.hidden = !visible;
  document.body.classList.toggle('auth-locked', Boolean(visible));
  if (error) {
    text(error, message);
    error.hidden = !message;
  }
  if (visible) {
    window.setTimeout(() => $('#admin-login-username')?.focus(), 0);
  }
}

function requireAdminLogin(message = '') {
  if (state.dialogResolve) closeWebuiDialog(false);
  state.authReady = true;
  state.authRequired = true;
  state.authenticated = false;
  state.status = null;
  $('#admin-logout')?.setAttribute('hidden', '');
  setOnlineState(false, 'Administrator login required.');
  setAdminLoginVisible(true, message);
}

async function checkAuthentication() {
  try {
    const response = await fetch('/admin/api/auth', {
      cache: 'no-store',
      credentials: 'same-origin'
    });
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
    const auth = await response.json();
    state.authReady = true;
    state.authRequired = Boolean(auth.required);
    state.authenticated = Boolean(auth.authenticated);
    $('#admin-logout')?.toggleAttribute('hidden', !state.authRequired || !state.authenticated);
    if (state.authRequired && !state.authenticated) {
      requireAdminLogin();
      return;
    }
    setAdminLoginVisible(false);
    await fetchStatus();
  } catch (error) {
    state.authReady = true;
    state.lastError = error.message || 'Authentication check failed';
    setOnlineState(false, state.lastError);
  }
}

async function submitAdminLogin(event) {
  event.preventDefault();
  const username = $('#admin-login-username')?.value || '';
  const password = $('#admin-login-password')?.value || '';
  const submit = $('#admin-login-submit');
  const errorElement = $('#admin-login-error');
  if (submit) {
    submit.disabled = true;
    submit.textContent = 'Signing in…';
  }
  if (errorElement) errorElement.hidden = true;

  try {
    const body = new URLSearchParams({username, password});
    const response = await fetch('/admin/login', {
      method: 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
      body
    });
    let payload = null;
    try { payload = await response.json(); } catch { }
    if (!response.ok) {
      throw new Error(payload?.message || `${response.status} ${response.statusText}`);
    }
    state.authRequired = Boolean(payload?.authentication_required ?? true);
    state.authenticated = true;
    $('#admin-login-password').value = '';
    $('#admin-logout')?.toggleAttribute('hidden', !state.authRequired);
    setAdminLoginVisible(false);
    toast('Administrator signed in');
    await fetchStatus();
  } catch (error) {
    setAdminLoginVisible(true, error.message || 'Sign-in failed.');
    $('#admin-login-password')?.focus();
    $('#admin-login-password')?.select();
  } finally {
    if (submit) {
      submit.disabled = false;
      submit.textContent = 'Sign in';
    }
  }
}

async function logoutAdmin() {
  try {
    await fetch('/admin/logout', {
      method: 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
      body: new URLSearchParams()
    });
  } finally {
    state.authenticated = false;
    state.status = null;
    requireAdminLogin('You have signed out.');
  }
}

function closeWebuiDialog(result) {
  const modal = $('#webui-dialog');
  if (!modal || modal.hidden) return;
  modal.hidden = true;
  const resolve = state.dialogResolve;
  state.dialogResolve = null;
  const previous = state.dialogPreviousFocus;
  state.dialogPreviousFocus = null;
  if (previous instanceof HTMLElement) previous.focus();
  if (resolve) resolve(result);
}

function openWebuiDialog({
  title,
  message,
  label = 'Mission Control',
  symbol = '!',
  confirmText = 'Continue',
  cancelText = 'Cancel',
  cancelable = true,
  value = null,
  danger = false
}) {
  const modal = $('#webui-dialog');
  if (!modal) return Promise.resolve(false);
  if (state.dialogResolve) closeWebuiDialog(false);
  state.dialogPreviousFocus = document.activeElement;
  state.dialogCancelable = Boolean(cancelable);
  text($('#webui-dialog-title'), title);
  text($('#webui-dialog-message'), message);
  text($('#webui-dialog-label'), label);
  text($('#webui-dialog-symbol'), symbol);
  text($('#webui-dialog-confirm'), confirmText);
  text($('#webui-dialog-cancel'), cancelText);
  const cancel = $('#webui-dialog-cancel');
  const confirm = $('#webui-dialog-confirm');
  const valueWrap = $('#webui-dialog-value-wrap');
  const valueInput = $('#webui-dialog-value');
  cancel.hidden = !cancelable;
  confirm.classList.toggle('danger-button', Boolean(danger));
  confirm.classList.toggle('primary-button', !danger);
  valueWrap.hidden = value == null;
  valueInput.value = value == null ? '' : String(value);
  modal.hidden = false;

  return new Promise((resolve) => {
    state.dialogResolve = resolve;
    window.setTimeout(() => {
      if (value != null) {
        valueInput.focus();
        valueInput.select();
      } else {
        confirm.focus();
      }
    }, 0);
  });
}

function confirmPopup(message, title = 'Confirm action', confirmText = 'Continue', danger = false) {
  return openWebuiDialog({title, message, confirmText, danger, symbol: danger ? '!' : '◇'});
}

function alertPopup(message, title = 'Mission Control alert') {
  return openWebuiDialog({
    title,
    message,
    confirmText: 'Close',
    cancelable: false,
    symbol: '!'
  });
}

function copyFallbackPopup(value) {
  return openWebuiDialog({
    title: 'Copy value',
    message: 'Clipboard access is unavailable. The value is selected below; press Ctrl+C or use your device copy command.',
    confirmText: 'Close',
    cancelable: false,
    value,
    symbol: '⧉'
  });
}

async function copyText(value, successMessage = 'Copied to clipboard') {
  try {
    await navigator.clipboard.writeText(value);
    toast(successMessage);
  } catch {
    await copyFallbackPopup(value);
  }
}

function sameOriginUrl(path) {
  const origin = window.location.origin && window.location.origin !== 'null'
    ? window.location.origin
    : document.baseURI;
  try {
    return new URL(path, origin).href;
  } catch {
    return String(path);
  }
}

function channelByLcn(lcn) {
  return state.status?.channels?.find((channel) => Number(channel.lcn) === Number(lcn)) || null;
}

function currentChannel() {
  const current = state.status?.current_channel;
  if (!current) return null;
  return channelByLcn(current.lcn) || current;
}

function signalState() {
  if (!state.status) return 'CONNECTING';
  if (state.status.streaming && state.status.current_channel) return 'LOCKED';
  if (Number(state.status.clients || 0) > 0) return 'ACTIVE';
  return 'IDLE';
}

function setOnlineState(online, detail = '') {
  const beacon = $('#connection-beacon');
  const footerDot = $('.status-dot');
  beacon?.classList.toggle('online', online);
  beacon?.classList.toggle('offline', !online);
  footerDot?.classList.toggle('online', online);
  footerDot?.classList.toggle('offline', !online);
  text($('#header-online'), online ? 'Online' : 'Offline');
  text($('#footer-status'), online ? 'SORALink online' : 'SORALink offline');
  text($('#connection-state'), `${online ? 'SORALink online' : 'SORALink offline'}. ${detail}`);
}

function switchView(viewName, updateHash = true) {
  const target = VALID_VIEWS.has(viewName) ? viewName : 'overview';
  if (target === 'settings' && state.activeView !== 'settings') state.settingsDirty = false;
  state.activeView = target;

  $$('.app-view').forEach((view) => {
    const active = view.dataset.view === target;
    view.hidden = !active;
    view.classList.toggle('active', active);
  });

  $$('.dock-item').forEach((button) => {
    const active = button.dataset.viewTarget === target;
    button.classList.toggle('active', active);
    button.setAttribute('aria-current', active ? 'page' : 'false');
  });

  if (updateHash && window.location.hash.slice(1) !== target) {
    try {
      history.replaceState(null, '', `#${target}`);
    } catch {
    }
  }

  renderActiveView();
}

function viewFromHash() {
  const hash = window.location.hash.slice(1).toLowerCase();
  return VALID_VIEWS.has(hash) ? hash : 'overview';
}

async function fetchStatus() {
  if (state.statusLoading || (state.authRequired && !state.authenticated)) return;
  state.statusLoading = true;
  try {
    const response = await fetch('/admin/api/status', {
      cache: 'no-store',
      credentials: 'same-origin'
    });
    if (response.status === 401) {
      requireAdminLogin('Your administrator session has expired.');
      return;
    }
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
    state.status = await response.json();
    state.authenticated = true;
    state.lastError = '';
    setOnlineState(true, `${Number(state.status.clients || 0)} viewer(s) connected.`);
    renderGlobalStatus();
    renderActiveView();
  } catch (error) {
    state.lastError = error.message || 'Status request failed';
    setOnlineState(false, state.lastError);
    renderDiagnostics();
  } finally {
    state.statusLoading = false;
  }
}

async function postAction(path, values = {}, successMessage = 'Action completed') {
  const body = new URLSearchParams();
  Object.entries(values).forEach(([key, value]) => body.set(key, String(value)));

  try {
    const response = await fetch(path, {
      method: 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      redirect: 'follow',
      headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
      body
    });
    if (response.status === 401) {
      requireAdminLogin('Your administrator session has expired.');
      return false;
    }
    if (!response.ok) {
      const detail = (await response.text()).trim();
      throw new Error(detail || `${response.status} ${response.statusText}`);
    }
    toast(successMessage);
    await fetchStatus();
    return true;
  } catch (error) {
    await alertPopup(error.message || 'Action failed', 'Action failed');
    return false;
  }
}

function renderGlobalStatus() {
  const data = state.status;
  if (!data) return;

  const viewers = Number(data.clients || data.viewers?.length || 0);
  const maxClients = Number(data.max_clients || 0);
  const channels = Number(data.channels?.length || 0);
  const rate = Number(data.data_rate_mbps || 0);
  const active = currentChannel();

  text($('#header-uptime'), formatDuration(data.uptime_seconds));
  text($('#header-viewers'), `${viewers} / ${maxClients}`);
  text($('#header-channels'), channels.toLocaleString());
  text($('#footer-rate'), `${rate.toFixed(2)} Mbps`);
  text($('#footer-channel'), active?.name || 'Idle');
  text($('#footer-epg'), `${Number(data.epg?.programmes || 0).toLocaleString()} programmes`);

  document.title = viewers > 0
    ? `SORALink Mission Control · ${viewers} viewer${viewers === 1 ? '' : 's'}`
    : 'SORALink Mission Control';
}

function renderOverview() {
  const data = state.status;
  if (!data) return;

  const active = currentChannel();
  const viewers = Number(data.clients || 0);
  const maxClients = Number(data.max_clients || 0);
  const rate = Number(data.data_rate_mbps || 0);
  const device = data.device_updates || {};
  const status = device.scan_running ? `SCAN ${Number(device.scan_progress || 0)}%` : signalState();
  const epgCount = Number(data.epg?.programmes || 0);
  const channelSummary = active ? `${active.frequency_mhz || '—'} MHz · ${active.symbol_rate_ks || '—'} kSym/s · ${active.polarization || '—'}` : 'The tuner is waiting for a viewer.';

  text($('#hero-current-channel'), active?.name || 'No channel tuned');
  text($('#hero-current-detail'), channelSummary);
  text($('#hero-rate'), `${rate.toFixed(2)} Mbps`);
  text($('#telemetry-signal'), status);
  text($('#telemetry-protocol'), device.scan_running
    ? `${Number(device.scan_frequency_mhz || 0)} MHz · ${Number(device.scan_symbol_rate_ks || 0)} kSym/s · TV ${Number(device.scan_tv_count || 0)} · Radio ${Number(device.scan_radio_count || 0)}`
    : (data.signal_metrics_available ? 'Signal metrics available' : 'Protocol ready · RF metrics unavailable'));
  text($('#telemetry-viewers'), viewers);
  text($('#telemetry-capacity'), `Capacity ${maxClients}`);
  text($('#telemetry-blocks'), Number(data.blocks || 0).toLocaleString());
  text($('#telemetry-errors'), `${Number(data.timeouts || 0).toLocaleString()} timeouts`);
  text($('#telemetry-epg'), epgCount.toLocaleString());
  text($('#telemetry-epg-loaded'), data.epg?.loaded_utc ? `Loaded ${formatDateTime(data.epg.loaded_utc)}` : 'Not loaded');

  const meterCount = Math.max(0, Math.min(8, Math.round(rate / 2) + (active ? 2 : 0)));
  $$('#hero-meter i').forEach((bar, index) => bar.classList.toggle('active', index < meterCount));

  text($('#now-channel-name'), active?.name || 'No active channel');
  text($('#overview-now-title'), active?.now?.title || 'No programme data');
  text($('#overview-now-time'), active?.now ? `${formatClock(active.now.start)} – ${formatClock(active.now.stop)}` : '—');
  text($('#overview-next-title'), active?.next?.title || 'No programme data');
  text($('#overview-next-time'), active?.next ? `${formatClock(active.next.start)} – ${formatClock(active.next.stop)}` : '—');
  setTimelineProgress($('#overview-now-progress')?.parentElement, active?.now);
  $('#overview-open-guide').disabled = !active;
}

function channelMatches(channel, term) {
  if (!term) return true;
  const haystack = [
    channel.lcn,
    channel.name,
    channel.type,
    channel.service_id,
    channel.epg_id,
    channel.frequency_mhz,
    channel.polarization
  ].join(' ').toLowerCase();
  return haystack.includes(term.trim().toLowerCase());
}

function filteredChannels() {
  const channels = state.status?.channels || [];
  return channels.filter((channel) => {
    if (!channelMatches(channel, state.channelSearch)) return false;
    if (state.channelType === 'all') return true;
    return String(channel.type || '').toLowerCase() === state.channelType;
  });
}

function appendCell(row, value, className = '') {
  const cell = document.createElement('td');
  if (className) cell.className = className;
  cell.textContent = String(value ?? '—');
  row.append(cell);
  return cell;
}

function renderChannels() {
  const body = $('#channels-table-body');
  if (!body) return;
  const channels = filteredChannels();
  body.replaceChildren();
  text($('#channels-result-count'), `${channels.length.toLocaleString()} service${channels.length === 1 ? '' : 's'}`);

  if (!channels.length) {
    const row = document.createElement('tr');
    const cell = appendCell(row, 'No channels match the current search and filter.', 'empty-state');
    cell.colSpan = 7;
    body.append(row);
    return;
  }

  channels.forEach((channel) => {
    const row = document.createElement('tr');
    appendCell(row, String(channel.lcn ?? '').padStart(3, '0'));

    const nameCell = document.createElement('td');
    const name = document.createElement('strong');
    name.textContent = channel.name || 'Unnamed channel';
    const epg = document.createElement('small');
    epg.textContent = channel.epg_id || 'No EPG ID';
    nameCell.append(name, epg);
    row.append(nameCell);

    appendCell(row, String(channel.type || 'TV').toUpperCase());
    appendCell(row, channel.service_id ?? '—');
    appendCell(row, `${channel.frequency_mhz || '—'} MHz · ${channel.symbol_rate_ks || '—'} · ${channel.polarization || '—'}`);
    appendCell(row, channel.now?.title || 'No EPG data');

    const actionCell = document.createElement('td');
    actionCell.className = 'action-group';
    const epgButton = document.createElement('button');
    epgButton.type = 'button';
    epgButton.className = 'channel-action epg';
    epgButton.textContent = 'EPG';
    epgButton.addEventListener('click', () => {
      state.guideLcn = Number(channel.lcn);
      switchView('guide');
      loadGuide(channel.lcn);
    });

    const copyButton = document.createElement('button');
    copyButton.type = 'button';
    copyButton.className = 'channel-action copy';
    copyButton.textContent = 'Copy URL';
    copyButton.addEventListener('click', () => copyText(sameOriginUrl(channel.stream_url), `${channel.name} stream URL copied`));

    actionCell.append(epgButton, copyButton);
    row.append(actionCell);
    body.append(row);
  });
}

function renderGuideChannelList() {
  const container = $('#guide-channel-list');
  if (!container) return;
  const channels = (state.status?.channels || []).filter((channel) => channelMatches(channel, state.guideSearch));
  container.replaceChildren();

  if (!channels.length) {
    const empty = document.createElement('div');
    empty.className = 'schedule-empty';
    empty.textContent = 'No matching channels.';
    container.append(empty);
    return;
  }

  channels.forEach((channel) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'guide-channel-button';
    button.classList.toggle('active', Number(channel.lcn) === Number(state.guideLcn));

    const number = document.createElement('span');
    number.textContent = String(channel.lcn ?? '').padStart(3, '0');
    const label = document.createElement('div');
    const name = document.createElement('b');
    name.textContent = channel.name || 'Unnamed channel';
    const now = document.createElement('small');
    now.textContent = channel.now?.title || 'No EPG data';
    label.append(name, now);
    button.append(number, label);
    button.addEventListener('click', () => loadGuide(channel.lcn));
    container.append(button);
  });
}

async function loadGuide(lcn) {
  const channel = channelByLcn(lcn);
  if (!channel) {
    state.guidePayload = null;
    renderGuide();
    return;
  }
  state.guideLcn = Number(channel.lcn);
  state.guideLoading = true;
  renderGuideChannelList();
  renderGuideHeader(channel);
  renderGuideSchedule();

  try {
    const response = await fetch(`/admin/api/epg/${encodeURIComponent(channel.lcn)}`, {
      cache: 'no-store',
      credentials: 'same-origin'
    });
    if (response.status === 401) {
      requireAdminLogin('Your administrator session has expired.');
      return;
    }
    if (!response.ok) {
      const detail = (await response.text()).trim();
      throw new Error(detail || `${response.status} ${response.statusText}`);
    }
    state.guidePayload = await response.json();
  } catch (error) {
    state.guidePayload = {programmes: []};
    await alertPopup(`EPG: ${error.message}`, 'Programme guide error');
  } finally {
    state.guideLoading = false;
    renderGuide();
  }
}

function renderGuideHeader(channel) {
  text($('#guide-channel-name'), channel?.name || 'Choose a channel');
  text($('#guide-channel-meta'), channel
    ? `LCN ${String(channel.lcn).padStart(3, '0')} · Service ${channel.service_id || '—'} · XMLTV ${channel.epg_id || 'not mapped'}`
    : 'No XMLTV schedule loaded.');
}

function renderGuideSchedule() {
  const channel = channelByLcn(state.guideLcn);
  const payload = state.guidePayload;
  const programmes = payload?.programmes || [];
  const nowEpoch = Date.now() / 1000;
  const current = programmes.find((programme) => Number(programme.start) <= nowEpoch && nowEpoch < Number(programme.stop));
  const next = programmes.find((programme) => Number(programme.start) > nowEpoch);

  text($('#guide-now-title'), state.guideLoading ? 'Loading programme data…' : current?.title || channel?.now?.title || 'No current programme');
  text($('#guide-now-time'), current ? `${formatClock(current.start)} – ${formatClock(current.stop)}` : channel?.now ? `${formatClock(channel.now.start)} – ${formatClock(channel.now.stop)}` : '—');
  text($('#guide-now-detail'), current?.subtitle || current?.category || current?.description || '');
  text($('#guide-next-title'), next?.title || channel?.next?.title || 'No next programme');
  text($('#guide-next-time'), next ? `${formatClock(next.start)} – ${formatClock(next.stop)}` : channel?.next ? `${formatClock(channel.next.start)} – ${formatClock(channel.next.stop)}` : '—');
  text($('#guide-next-detail'), next?.subtitle || next?.category || next?.description || '');
  setTimelineProgress($('#guide-progress')?.parentElement, current || channel?.now);

  const schedule = $('#guide-schedule');
  if (!schedule) return;
  schedule.replaceChildren();

  if (state.guideLoading) {
    const loading = document.createElement('div');
    loading.className = 'schedule-empty';
    loading.textContent = 'Loading XMLTV schedule…';
    schedule.append(loading);
    return;
  }

  const upcoming = programmes.filter((programme) => Number(programme.stop) > nowEpoch).slice(0, 100);
  if (!upcoming.length) {
    const empty = document.createElement('div');
    empty.className = 'schedule-empty';
    empty.textContent = 'No XMLTV programmes found for this channel. Ensure epg_id or xmltv_id in channels.xml matches the XMLTV channel ID.';
    schedule.append(empty);
    return;
  }

  upcoming.forEach((programme) => {
    const item = document.createElement('article');
    item.className = 'schedule-item';
    if (Number(programme.start) <= nowEpoch && nowEpoch < Number(programme.stop)) item.classList.add('current');

    const time = document.createElement('time');
    time.textContent = `${formatClock(programme.start)} – ${formatClock(programme.stop)}`;
    const detail = document.createElement('div');
    const titleNode = document.createElement('strong');
    titleNode.textContent = programme.title || 'Programme';
    const small = document.createElement('small');
    small.textContent = programme.subtitle || programme.category || programme.description || 'Programme';
    detail.append(titleNode, small);
    item.append(time, detail);
    schedule.append(item);
  });
}

function renderGuide() {
  const channels = state.status?.channels || [];
  if (state.guideLcn == null && channels.length) {
    state.guideLcn = Number(state.status?.current_channel?.lcn || channels[0].lcn);
  }
  const channel = channelByLcn(state.guideLcn);
  renderGuideChannelList();
  renderGuideHeader(channel);
  renderGuideSchedule();
}

function renderViewers() {
  const data = state.status;
  if (!data) return;
  const viewers = data.viewers || [];
  const body = $('#viewers-table-body');
  if (!body) return;

  text($('#viewer-summary-connected'), viewers.length);
  text($('#viewer-summary-max'), Number(data.max_clients || 0));
  text($('#viewer-summary-channel'), currentChannel()?.name || 'Idle');
  text($('#viewer-summary-rate'), `${Number(data.data_rate_mbps || 0).toFixed(2)} Mbps`);

  body.replaceChildren();
  if (!viewers.length) {
    const row = document.createElement('tr');
    const cell = appendCell(row, 'No viewers are connected.', 'empty-state');
    cell.colSpan = 5;
    body.append(row);
    return;
  }

  viewers.forEach((viewer) => {
    const row = document.createElement('tr');
    appendCell(row, `conn_${String(viewer.id ?? '').padStart(4, '0')}`);
    appendCell(row, viewer.host || '—');
    appendCell(row, viewer.port || '—');
    appendCell(row, formatDuration(viewer.connected_seconds));

    const actionCell = document.createElement('td');
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'viewer-action';
    button.textContent = 'Disconnect';
    button.addEventListener('click', () => disconnectViewer(viewer));
    actionCell.append(button);
    row.append(actionCell);
    body.append(row);
  });
}

async function disconnectViewer(viewer) {
  const label = viewer.host || `viewer ${viewer.id}`;
  if (!await confirmPopup(`Disconnect ${label}?`, 'Disconnect viewer', 'Disconnect', true)) return;
  await postAction('/admin/kick', {id: viewer.id}, `${label} disconnected`);
}

function renderPlaylist() {
  text($('#playlist-all-url'), sameOriginUrl('/playlist.m3u'));
  text($('#playlist-tv-url'), sameOriginUrl('/tv.m3u'));
  text($('#playlist-radio-url'), sameOriginUrl('/radio.m3u'));

  const grid = $('#playlist-channel-grid');
  if (!grid) return;
  const channels = state.status?.channels || [];
  grid.replaceChildren();

  if (!channels.length) {
    const empty = document.createElement('div');
    empty.className = 'schedule-empty';
    empty.textContent = 'No channels are loaded.';
    grid.append(empty);
    return;
  }

  channels.forEach((channel) => {
    const card = document.createElement('article');
    card.className = 'channel-link-card';
    const name = document.createElement('strong');
    name.textContent = `${String(channel.lcn ?? '').padStart(3, '0')} · ${channel.name || 'Unnamed channel'}`;
    const meta = document.createElement('small');
    meta.textContent = `${String(channel.type || 'TV').toUpperCase()} · SID ${channel.service_id || '—'}`;
    const actions = document.createElement('div');
    const copy = document.createElement('button');
    copy.type = 'button';
    copy.textContent = 'Copy';
    copy.addEventListener('click', () => copyText(sameOriginUrl(channel.stream_url), `${channel.name} stream URL copied`));
    const epg = document.createElement('button');
    epg.type = 'button';
    epg.textContent = 'EPG';
    epg.addEventListener('click', () => {
      state.guideLcn = Number(channel.lcn);
      switchView('guide');
      loadGuide(channel.lcn);
    });
    actions.append(copy, epg);
    card.append(name, meta, actions);
    grid.append(card);
  });
}

function renderSettings() {
  const data = state.status;
  if (!data) return;
  const config = data.config || {};

  const connectedViewers = Number(data.clients || 0);
  $('#setting-max-clients').value = Number(data.max_clients || 1);
  text($('#max-clients-help'), connectedViewers > 0
    ? `${connectedViewers} viewer${connectedViewers === 1 ? '' : 's'} currently connected. Lowering the limit keeps those streams connected and blocks new viewers until usage drops below the new limit.`
    : 'Existing streams stay connected when this limit is lowered.');
  $('#setting-wait-ms').value = Number(config.wait_ms || 0);
  $('#setting-timeout-ms').value = Number(config.timeout_ms || 3000);
  $('#setting-missing-key').value = config.missing_key || 'pass';
  const device = data.device_updates || {};
  $('#setting-device-channels-update').checked = Boolean(device.channels_enabled);
  $('#setting-device-epg-update').checked = Boolean(device.epg_enabled);
  $('#setting-device-update-start').checked = Boolean(config.device_update_on_start ?? true);
  $('#setting-device-channels-minutes').value = Number(device.channels_refresh_minutes ?? 1440);
  $('#setting-device-epg-minutes').value = Number(device.epg_refresh_minutes ?? 240);
  $('#setting-device-scan-minutes').value = Number(device.scan_refresh_minutes ?? 0);
  $('#setting-device-scan-timeout').value = Number(config.device_scan_timeout_minutes ?? 45);
  $('#setting-device-scan-mode').value = String(Number(config.device_scan_mode ?? 110));
  $('#setting-device-scan-sort').value = String(Number(config.device_scan_sort_mode ?? 0));
  $('#setting-device-scan-range').value = Number(config.device_scan_search_range ?? 0);
  $('#setting-device-scan-order').value = Number(config.device_scan_order_by ?? 0);
  $('#setting-device-scan-network').checked = Boolean(config.device_scan_network ?? true);
  $('#setting-device-scan-epg').checked = Boolean(config.device_scan_epg_after ?? true);
  $('#setting-device-scan-apply-satellite').checked = Boolean(config.device_scan_apply_satellite ?? false);
  $('#setting-orbital-tenths').value = Number(config.orbital_tenths ?? 192);
  $('#setting-orbital-direction').value = config.west ? '1' : '0';
  $('#setting-lnb-low').value = Number(config.lnb_low_mhz ?? 9750);
  $('#setting-lnb-high').value = Number(config.lnb_high_mhz ?? 10600);
  $('#setting-lnb-switch').value = Number(config.lnb_switch_mhz ?? 11700);
  $('#setting-diseqc-port').value = String(Number(config.diseqc_port ?? 0));
  $('#setting-tone').value = config.tone || 'auto';
  $('#setting-satellite-preset').value = matchingSatellitePreset(config);
  updateSatellitePresetSummary();
  updateScanMethodHelp();
  $('#setting-persist').disabled = !config.config_active;
  $('#setting-persist').checked = Boolean(config.config_active);

  text($('#info-admin-auth'), config.admin_auth ? 'Enabled · popup session' : (config.viewer_auth ? 'Uses viewer login · popup session' : 'Disabled'));
  text($('#info-viewer-auth'), config.viewer_auth ? 'Enabled' : 'Disabled');
  text($('#info-config-path'), config.config_active ? config.config_path : 'CLI only');
  text($('#info-channels-path'), config.channels_path || '—');
  text($('#info-epg-path'), config.epg_path || 'Not configured');
  text($('#info-epg-update'), config.epg_update
    ? `Enabled · ${Number(config.epg_update_timeout_ms || 0)} ms timeout`
    : 'Disabled');
  text($('#info-epg-url'), config.epg_url || 'Not configured');
  text($('#info-web-root'), config.web_root || '—');
  text($('#info-device-channels-last'), device.last_channels_success_utc ? formatDateTime(device.last_channels_success_utc) : 'Never');
  text($('#info-device-epg-last'), device.last_epg_success_utc ? formatDateTime(device.last_epg_success_utc) : 'Never');
  text($('#info-device-scan-last'), device.last_scan_success_utc ? formatDateTime(device.last_scan_success_utc) : 'Never');
  text($('#info-device-message'), device.last_message || 'Idle');

  const progress = Math.max(0, Math.min(100, Number(device.scan_progress || 0)));
  text($('#receiver-scan-title'), device.scan_running ? `Scanning · ${progress}%` : (device.last_scan_success_utc ? 'Last scan completed' : 'Idle'));
  const scanMethod = Number(device.scan_mode || 0) === 110 ? 'full preset sweep' : 'quick Device scan';
  text($('#receiver-scan-detail'), device.scan_running
    ? `${Number(device.scan_frequency_mhz || 0)} MHz · ${Number(device.scan_symbol_rate_ks || 0)} kSym/s · ${scanMethod}`
    : (device.last_message || 'No scan is running.'));
  $('#receiver-scan-progress').style.width = `${progress}%`;
  text($('#receiver-scan-tv'), Number(device.scan_tv_count || 0));
  text($('#receiver-scan-radio'), Number(device.scan_radio_count || 0));
  $('#settings-start-scan').disabled = Boolean(device.busy || device.scan_running || Number(data.clients || 0));
  $('#settings-cancel-scan').disabled = !device.scan_running;
}

function collectSettingsValues() {
  const values = {
    max_clients: $('#setting-max-clients').value,
    wait_ms: $('#setting-wait-ms').value,
    timeout_ms: $('#setting-timeout-ms').value,
    missing_key: $('#setting-missing-key').value,
    device_channels_update: $('#setting-device-channels-update').checked ? 1 : 0,
    device_epg_update: $('#setting-device-epg-update').checked ? 1 : 0,
    device_update_on_start: $('#setting-device-update-start').checked ? 1 : 0,
    device_channels_refresh_minutes: $('#setting-device-channels-minutes').value,
    device_epg_refresh_minutes: $('#setting-device-epg-minutes').value,
    device_scan_refresh_minutes: $('#setting-device-scan-minutes').value,
    device_scan_timeout_minutes: $('#setting-device-scan-timeout').value,
    device_scan_mode: $('#setting-device-scan-mode').value,
    device_scan_sort_mode: $('#setting-device-scan-sort').value,
    device_scan_search_range: $('#setting-device-scan-range').value,
    device_scan_order_by: $('#setting-device-scan-order').value,
    device_scan_network: $('#setting-device-scan-network').checked ? 1 : 0,
    device_scan_epg_after: $('#setting-device-scan-epg').checked ? 1 : 0,
    device_scan_apply_satellite: $('#setting-device-scan-apply-satellite').checked ? 1 : 0,
    satellite_preset: $('#setting-satellite-preset').value,
    orbital_tenths: $('#setting-orbital-tenths').value,
    west: $('#setting-orbital-direction').value,
    tone: $('#setting-tone').value,
    lnb_low_mhz: $('#setting-lnb-low').value,
    lnb_high_mhz: $('#setting-lnb-high').value,
    lnb_switch_mhz: $('#setting-lnb-switch').value,
    diseqc_port: $('#setting-diseqc-port').value
  };
  if ($('#setting-persist').checked && !$('#setting-persist').disabled) values.persist = 1;
  return values;
}

async function submitSettings(event) {
  event.preventDefault();
  const values = collectSettingsValues();
  const requestedLimit = Number(values.max_clients || 0);
  const connectedViewers = Number(state.status?.clients || 0);
  const loweredBelowUsage = requestedLimit > 0 && requestedLimit < connectedViewers;
  const successMessage = loweredBelowUsage
    ? `Settings applied${values.persist ? ' and saved' : ''} · current viewers remain connected`
    : (values.persist ? 'Settings applied and saved' : 'Settings applied');
  const ok = await postAction('/admin/settings', values, successMessage);
  if (ok) state.settingsDirty = false;
}

function renderDiagnostics() {
  const data = state.status;
  text($('#diag-server-time'), data?.server_time ? formatDateTime(data.server_time) : '—');
  text($('#diag-total-bytes'), formatBytes(data?.total_bytes || 0));
  text($('#diag-blocks'), Number(data?.blocks || 0).toLocaleString());
  text($('#diag-timeouts'), Number(data?.timeouts || 0).toLocaleString());
  text($('#diag-bad-blocks'), Number(data?.bad_blocks || 0).toLocaleString());
  text($('#diag-epg-loaded'), data?.epg?.loaded_utc ? formatDateTime(data.epg.loaded_utc) : '—');
  text($('#diag-epg-updater'), data?.device_updates?.scan_running
    ? `Receiver scan ${Number(data.device_updates.scan_progress || 0)}%`
    : (data?.device_updates?.epg_enabled ? 'Receiver EPG enabled' : (data?.config?.epg_update ? 'URL update enabled' : 'Disabled')));
  text($('#diag-device-channels'), data?.device_updates?.last_channels_success_utc ? formatDateTime(data.device_updates.last_channels_success_utc) : 'Never');
  text($('#diag-device-epg'), data?.device_updates?.last_epg_success_utc ? formatDateTime(data.device_updates.last_epg_success_utc) : 'Never');
  const raw = $('#diagnostics-json');
  if (raw) raw.textContent = data ? JSON.stringify(data, null, 2) : `Status unavailable\n${state.lastError}`;
}

function renderActiveView() {
  if (!state.status && state.activeView !== 'diagnostics') return;
  switch (state.activeView) {
    case 'overview':
      renderOverview();
      break;
    case 'channels':
      renderChannels();
      break;
    case 'guide':
      renderGuide();
      if (state.guidePayload == null && state.guideLcn != null && !state.guideLoading) loadGuide(state.guideLcn);
      break;
    case 'viewers':
      renderViewers();
      break;
    case 'playlist':
      renderPlaylist();
      break;
    case 'settings':
      if (!state.settingsDirty) renderSettings();
      break;
    case 'diagnostics':
      renderDiagnostics();
      break;
    default:
      break;
  }
}

function bindEvents() {
  $('#admin-login-form')?.addEventListener('submit', submitAdminLogin);
  $('#admin-logout')?.addEventListener('click', async () => {
    if (await confirmPopup('End this administrator session?', 'Sign out administrator', 'Sign out')) {
      await logoutAdmin();
    }
  });
  $('#webui-dialog-confirm')?.addEventListener('click', () => closeWebuiDialog(true));
  $('#webui-dialog-cancel')?.addEventListener('click', () => closeWebuiDialog(false));
  $('#webui-dialog-backdrop')?.addEventListener('click', () => {
    if (state.dialogCancelable) closeWebuiDialog(false);
  });
  window.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && state.dialogResolve && state.dialogCancelable) {
      closeWebuiDialog(false);
    }
  });

  $$('[data-view-target]').forEach((button) => {
    button.addEventListener('click', (event) => {
      event.preventDefault();
      switchView(button.dataset.viewTarget);
    });
  });

  window.addEventListener('hashchange', () => switchView(viewFromHash(), false));

  $('#overview-open-guide').addEventListener('click', () => {
    const active = currentChannel();
    if (!active) return;
    state.guideLcn = Number(active.lcn);
    switchView('guide');
    loadGuide(active.lcn);
  });
  $('#overview-reload').addEventListener('click', () => postAction('/admin/reload', {}, 'Local channels and EPG reloaded'));
  $('#overview-device-sync').addEventListener('click', () => postAction('/admin/device/update-all', {}, 'Receiver channels and EPG updated'));
  $('#overview-device-scan').addEventListener('click', async () => {
    if (!await confirmPopup('This rebuilds the dongle lineup and can take several minutes. No viewers may be connected.', 'Start receiver scan', 'Start scan')) return;
    postAction('/admin/device/scan', {}, 'Receiver channel scan started');
  });
  $('#overview-playlist').addEventListener('click', () => window.open('/playlist.m3u', '_blank', 'noopener'));
  $('#overview-viewers').addEventListener('click', () => switchView('viewers'));
  $('#overview-settings').addEventListener('click', () => switchView('settings'));

  $('#channels-search').addEventListener('input', (event) => {
    state.channelSearch = event.target.value;
    renderChannels();
  });
  $('#channels-type-filter').addEventListener('change', (event) => {
    state.channelType = event.target.value;
    renderChannels();
  });
  $('#channels-reload').addEventListener('click', () => postAction('/admin/reload', {}, 'Local channels and EPG reloaded'));
  $('#channels-device-update').addEventListener('click', () => postAction('/admin/device/update-channels', {}, 'Channel list updated from receiver'));
  $('#channels-device-scan').addEventListener('click', async () => {
    if (!await confirmPopup('Start a receiver channel scan and rebuild the lineup?', 'Rebuild channel lineup', 'Start scan')) return;
    postAction('/admin/device/scan', {}, 'Receiver channel scan started');
  });
  $('#channels-open-playlist').addEventListener('click', () => window.open('/playlist.m3u', '_blank', 'noopener'));

  $('#guide-channel-search').addEventListener('input', (event) => {
    state.guideSearch = event.target.value;
    renderGuideChannelList();
  });
  $('#guide-device-update').addEventListener('click', async () => {
    const ok = await postAction('/admin/device/update-epg', {}, 'EPG updated from receiver');
    if (ok && state.guideLcn != null) await loadGuide(state.guideLcn);
  });
  $('#guide-reload').addEventListener('click', async () => {
    const ok = await postAction('/admin/reload-epg', {}, 'EPG reloaded');
    if (ok && state.guideLcn != null) await loadGuide(state.guideLcn);
  });
  $('#guide-copy-stream').addEventListener('click', async () => {
    const channel = channelByLcn(state.guideLcn);
    if (!channel) return alertPopup('Choose a channel first.', 'No channel selected');
    await copyText(sameOriginUrl(channel.stream_url), `${channel.name} stream URL copied`);
  });

  $('#viewers-disconnect-all').addEventListener('click', async () => {
    if (!await confirmPopup('Disconnect every active viewer?', 'Disconnect all viewers', 'Disconnect all', true)) return;
    postAction('/admin/kick-all', {}, 'All viewers disconnected');
  });

  $$('.playlist-copy').forEach((button) => {
    button.addEventListener('click', () => copyText(sameOriginUrl(button.dataset.playlistPath), 'Playlist URL copied'));
  });
  $$('.playlist-open').forEach((button) => {
    button.addEventListener('click', () => window.open(button.dataset.playlistPath, '_blank', 'noopener'));
  });

  $('#settings-update-channels').addEventListener('click', () => postAction('/admin/device/update-channels', {}, 'Channel list updated from receiver'));
  $('#settings-update-epg').addEventListener('click', () => postAction('/admin/device/update-epg', {}, 'EPG updated from receiver'));
  $('#settings-update-all').addEventListener('click', () => postAction('/admin/device/update-all', {}, 'Receiver data updated'));
  $('#settings-start-scan').addEventListener('click', async () => {
    const presetKey = $('#setting-satellite-preset').value;
    const preset = SATELLITE_PRESETS[presetKey];
    const fullSweep = $('#setting-device-scan-mode').value === '110';
    const prompt = fullSweep
      ? `Start the ${preset?.label || 'custom satellite'} full sweep? SORALink will read section [${presetKey}] from the configured transponder table${preset?.builtInSweep ? ' (or use the built-in Astra fallback)' : ''}, tune every listed transponder, and merge the results. Configured list size: ${preset?.transponders ?? 'custom'}. If that section is missing, the scan stops with an error instead of running the quick scan.`
      : `Start the quick Device internal scan for ${preset?.label || 'the selected satellite'}? This uses only the table stored in the receiver and may scan fewer transponders than the listed ${preset?.transponders ?? 'unknown'} entries.`;
    if (!await confirmPopup(prompt, 'Start satellite scan', 'Start scan')) return;
    const values = collectSettingsValues();
    const applied = await postAction('/admin/settings', values, values.persist ? 'Scan settings applied and saved' : 'Scan settings applied');
    if (!applied) return;
    state.settingsDirty = false;
    await postAction('/admin/device/scan', {}, 'Device channel scan started');
  });
  $('#settings-cancel-scan').addEventListener('click', () => postAction('/admin/device/scan-cancel', {}, 'Receiver scan cancelled'));
  $('#setting-satellite-preset').addEventListener('change', (event) => {
    applySatellitePreset(event.target.value);
    state.settingsDirty = true;
  });
  $('#setting-device-scan-mode').addEventListener('change', () => {
    updateScanMethodHelp();
    state.settingsDirty = true;
  });
  ['setting-orbital-tenths', 'setting-orbital-direction', 'setting-lnb-low', 'setting-lnb-high', 'setting-lnb-switch', 'setting-tone'].forEach((id) => {
    $(`#${id}`).addEventListener('input', () => {
      $('#setting-satellite-preset').value = matchingSatellitePreset({
        orbital_tenths: Number($('#setting-orbital-tenths').value || 0),
        west: $('#setting-orbital-direction').value === '1',
        lnb_low_mhz: Number($('#setting-lnb-low').value || 0),
        lnb_high_mhz: Number($('#setting-lnb-high').value || 0),
        lnb_switch_mhz: Number($('#setting-lnb-switch').value || 0),
        tone: $('#setting-tone').value
      });
      updateSatellitePresetSummary();
      updateScanMethodHelp();
    });
  });
  $('#settings-form').addEventListener('submit', submitSettings);
  $('#settings-form').addEventListener('input', () => { state.settingsDirty = true; });
  $('#settings-form').addEventListener('change', () => { state.settingsDirty = true; });
  $('#settings-reset').addEventListener('click', () => {
    state.settingsDirty = false;
    renderSettings();
  });

  $('#diagnostics-refresh').addEventListener('click', fetchStatus);
  $('#diagnostics-copy-json').addEventListener('click', async () => {
    if (!state.status) return alertPopup('No status JSON is available.', 'Diagnostics unavailable');
    await copyText(JSON.stringify(state.status, null, 2), 'Status JSON copied');
  });
}

bindEvents();
switchView(viewFromHash(), false);
checkAuthentication();
window.setInterval(() => {
  if (state.authReady && (!state.authRequired || state.authenticated)) fetchStatus();
}, 2000);

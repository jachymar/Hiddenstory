// ============================================================================
//  GENEROVANY SOUBOR - NEEDITOVAT RUCNE!
//  Zdroj:     Web_app/index.html
//  Generator: Web_app/build_dashboard.ps1   (spust po kazde zmene index.html)
//  Kodovani:  UTF-8 bez BOM (ESP stranku serviruje s charset=utf-8)
// ============================================================================
#pragma once
#include <stddef.h>

const char dashboard_html[] = R"rawliteral(<!DOCTYPE html>
<html lang="cs">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Dashboard - Hidden Story</title>
    <style>
        :root {
            --bg-color: #0f111a;
            --panel-bg: #1e2130;
            --card-bg: #181b2a;
            --text-main: #e2e8f0;
            --text-muted: #94a3b8;
            --accent-blue: #3b82f6;
            --accent-blue-hover: #2563eb;
            --accent-orange: #f59e0b;
            --accent-orange-hover: #d97706;
            --status-red: #ef4444;
            --status-green: #10b981;
            --border-color: #334155;
            --border-light: rgba(255, 255, 255, 0.08);
        }

        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            padding: 1.5rem 1rem;
            font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            display: flex;
            flex-direction: column;
            align-items: center;
            min-height: 100vh;
        }

        .dashboard-container {
            width: 100%;
            max-width: 950px;
            display: flex;
            flex-direction: column;
            gap: 1.25rem;
        }

        /* HEADER */
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 1rem;
            border-bottom: 1px solid var(--border-color);
            flex-wrap: wrap;
            gap: 1rem;
        }

        .header-title h1 {
            margin: 0;
            font-size: 1.5rem;
            font-weight: 400;
            letter-spacing: 1px;
            text-transform: uppercase;
        }

        .header-title h1 strong {
            font-weight: 700;
            color: var(--accent-orange);
        }

        .header-actions {
            display: flex;
            align-items: center;
            gap: 0.75rem;
        }

        /* STATUS BADGE */
        .status-indicator {
            display: flex;
            align-items: center;
            gap: 0.6rem;
            padding: 0.5rem 1rem;
            border-radius: 9999px;
            font-size: 0.85rem;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            background-color: rgba(239, 68, 68, 0.12);
            color: var(--status-red);
            border: 1px solid rgba(239, 68, 68, 0.25);
            transition: all 0.3s ease;
        }

        .status-indicator.connected {
            background-color: rgba(16, 185, 129, 0.12);
            color: var(--status-green);
            border: 1px solid rgba(16, 185, 129, 0.25);
        }

        .status-indicator.connecting {
            background-color: rgba(245, 158, 11, 0.12);
            color: var(--accent-orange);
            border: 1px solid rgba(245, 158, 11, 0.25);
        }

        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: currentColor;
            box-shadow: 0 0 6px currentColor;
        }

        /* GRID LAYOUT */
        .grid-layout {
            display: grid;
            grid-template-columns: 1fr;
            gap: 1.25rem;
        }

        @media (min-width: 768px) {
            .grid-layout {
                grid-template-columns: repeat(2, 1fr);
            }
            .span-full {
                grid-column: 1 / -1;
            }
        }

        /* CARDS */
        .card {
            background-color: var(--panel-bg);
            border-radius: 12px;
            padding: 1.25rem;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.3), 0 2px 4px -2px rgba(0, 0, 0, 0.3);
            border: 1px solid var(--border-light);
        }

        .card h2 {
            margin-top: 0;
            margin-bottom: 1rem;
            font-size: 0.95rem;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 1px;
            font-weight: 600;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        /* BUTTONS */
        button {
            width: 100%;
            padding: 0.85rem 1.25rem;
            border-radius: 8px;
            border: none;
            font-size: 0.95rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
            color: #fff;
            margin-bottom: 0.65rem;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 0.5rem;
            user-select: none;
        }

        button:last-child {
            margin-bottom: 0;
        }

        button:active {
            transform: scale(0.98);
        }

        .btn-primary {
            background-color: var(--accent-blue);
        }

        .btn-primary:hover {
            background-color: var(--accent-blue-hover);
        }

        .btn-outline {
            background-color: transparent;
            border: 1px solid var(--border-color);
            color: var(--text-main);
        }

        .btn-outline:hover {
            background-color: rgba(255, 255, 255, 0.06);
            border-color: var(--text-muted);
        }

        .btn-outline.active-mode {
            background-color: rgba(59, 130, 246, 0.2);
            border-color: var(--accent-blue);
            color: #93c5fd;
            box-shadow: 0 0 10px rgba(59, 130, 246, 0.25);
        }

        .btn-warning {
            background-color: rgba(245, 158, 11, 0.12);
            color: var(--accent-orange);
            border: 1px solid rgba(245, 158, 11, 0.3);
        }

        .btn-warning:hover {
            background-color: rgba(245, 158, 11, 0.25);
        }

        .btn-sm {
            font-size: 0.75rem;
            padding: 0.35rem 0.75rem;
            width: auto;
            margin-bottom: 0;
        }

        /* SUB-GRID FOR BUTTONS */
        .btn-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 0.65rem;
        }

        .btn-grid button {
            margin-bottom: 0;
        }

        /* MODULE STATUS GRID */
        .module-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(130px, 1fr));
            gap: 0.6rem;
        }

        .module-badge {
            background: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 0.6rem 0.75rem;
            display: flex;
            align-items: center;
            justify-content: space-between;
            font-size: 0.8rem;
            font-weight: 500;
        }

        .module-badge .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: var(--status-red);
            box-shadow: 0 0 4px var(--status-red);
            transition: all 0.3s ease;
        }

        .module-badge.online {
            border-color: rgba(16, 185, 129, 0.3);
            background: rgba(16, 185, 129, 0.05);
        }

        .module-badge.online .dot {
            background-color: var(--status-green);
            box-shadow: 0 0 6px var(--status-green);
        }

        /* TERMINAL / LOGS */
        .terminal-container {
            background-color: #070913;
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 0.75rem;
            height: 140px;
            overflow-y: auto;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 0.8rem;
            color: #a7f3d0;
            line-height: 1.4;
        }

        .terminal-container .log-entry {
            margin: 0;
            white-space: pre-wrap;
            word-wrap: break-word;
        }

        .terminal-container .log-entry.log-err {
            color: #fca5a5;
        }

        .terminal-container .log-entry.log-cmd {
            color: #93c5fd;
        }

        /* CONFIG ROW */
        .config-row {
            display: flex;
            gap: 0.75rem;
            align-items: center;
            justify-content: space-between;
            flex-wrap: wrap;
            font-size: 0.85rem;
            color: var(--text-muted);
        }

        .config-row input {
            background: #0f111a;
            border: 1px solid var(--border-color);
            color: #fff;
            padding: 0.4rem 0.75rem;
            border-radius: 6px;
            font-size: 0.85rem;
            width: 150px;
            text-align: center;
        }

        .config-row input:focus {
            outline: none;
            border-color: var(--accent-blue);
        }

        .mode-info {
            margin-top: 0.75rem;
            font-size: 0.8rem;
            color: var(--text-muted);
        }

        .mode-info.alarm {
            color: var(--status-red);
            font-weight: 600;
        }

        .pulse-btn {
            animation: pulse-border 0.4s ease;
        }

        @keyframes pulse-border {
            0% { transform: scale(0.96); }
            50% { transform: scale(1.02); }
            100% { transform: scale(1); }
        }
    </style>
</head>
<body>

    <div class="dashboard-container">
        
        <!-- HEADER -->
        <header class="header">
            <div class="header-title">
                <h1>Hidden Story <strong>Operator</strong></h1>
            </div>
            <div class="header-actions">
                <button id="btnConnectUsb" class="btn-outline btn-sm" onclick="connectUsb()">Připojit USB</button>
                <div id="statusBadge" class="status-indicator">
                    <div class="status-dot"></div>
                    <span id="statusText">Odpojeno</span>
                </div>
            </div>
        </header>

        <div class="grid-layout">
            
            <!-- GAME MODES MODULE -->
            <div class="card">
                <h2>Herní Režim</h2>
                <button id="btnMode0" class="btn-outline" onclick="sendCommand('0', this)">Standardní Hra (0)</button>
                <button id="btnMode3" class="btn-outline" onclick="sendCommand('3', this)">Pracovní Mód (3)</button>
                <div class="mode-info" id="modeInfo">Aktuální režim: neznámý</div>
            </div>

            <!-- LASER DETECTION MODULE -->
            <div class="card">
                <h2>Detekce Laseru (Herní Mód)</h2>
                <div class="btn-grid">
                    <button id="btnLaserDetectOn" class="btn-outline active-mode" onclick="sendCommand('Z', this)">Zapnuto (Výchozí)</button>
                    <button id="btnLaserDetectOff" class="btn-outline" onclick="sendCommand('Y', this)">Vypnuto (Ignorovat)</button>
                </div>
                <div class="mode-info" id="laserDetectInfo" style="margin-top: 0.75rem;">Stav snímání v herním módu: Zapnuto</div>
            </div>

            <!-- LOCKS MODULE -->
            <div class="card">
                <h2>Manuální Ovládání Schránek</h2>
                <div class="mode-info" id="lockStatusInfo">Stav Tlačítkové schránky: ČEKÁM NA DATA...</div>
                <div class="btn-grid">
                    <button class="btn-warning" onclick="sendCommand('A', this)">Lebka (A)</button>
                    <button class="btn-warning" onclick="sendCommand('B', this)">Tlačítka (B)</button>
                    <button class="btn-warning" onclick="sendCommand('C', this)">Kola (C)</button>
                    <button class="btn-warning" onclick="sendCommand('E', this)">Oltář / Ťukání (E)</button>
                    <button class="btn-warning" style="grid-column: 1 / -1;" onclick="sendCommand('F', this)">Vynucené vysypání kuličky (F)</button>
                    <button class="btn-warning" style="grid-column: 1 / -1;" onclick="sendCommand('L', this)">Spustit světelnou kaskádu (L)</button>
                    <button class="btn-warning" style="grid-column: 1 / -1;" onclick="sendCommand('U', this)">Odblokovat schránku Tlačítka (U)</button>
                </div>
            </div>

            <!-- HARDWARE STATUS -->
            <div class="card span-full">
                <h2>Stav Komponent & Sběrnice</h2>
                <div class="module-grid">
                    <div class="module-badge" id="mod-eth"><span>LAN (ENC28J60)</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-laser"><span>Lasery</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-tlacitka"><span>Tlačítka</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-kola"><span>Kola</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-lebka"><span>Lebka</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-svetla"><span>Světla</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-audio"><span>Audio 1 & 2</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-m3"><span>ESP Místnost 3</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-tukani"><span>Ťukání (M3)</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-audio3"><span>Audio 3 (M3)</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-wled1"><span>WLED 1</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-wled2"><span>WLED 2</span><div class="dot"></div></div>
                    <div class="module-badge" id="mod-wled3"><span>WLED 3</span><div class="dot"></div></div>
                </div>
            </div>

            <!-- LOGS / DIAGNOSTICS -->
            <div class="card span-full">
                <h2>
                    <span>Systémový Log</span>
                    <span id="lastUpdateText" style="font-size: 0.75rem; text-transform: none; color: var(--text-muted); font-weight: normal;"></span>
                </h2>
                <div class="terminal-container" id="terminalLog">
                    <div class="log-entry">Aplikace spuštěna. Inicializuji spojení...</div>
                </div>
            </div>

            <!-- CONFIGURATION -->
            <div class="card span-full" style="background: transparent; border-color: rgba(255,255,255,0.05);">
                <div class="config-row">
                    <div>
                        <span>IP adresa ESP32 Master: </span>
                        <input type="text" id="ipAddress" value="192.168.0.100" placeholder="192.168.0.100">
                    </div>
                    <span style="font-size: 0.75rem;">Podpora: HTTP REST API & Web Serial API</span>
                </div>
            </div>

        </div>

    </div>

    <script>
        const DEFAULT_IP = '192.168.0.100';
        let isConnected = false;

        // --- PROMĚNNÉ PRO USB SERIAL ---
        let serialPort = null;
        let serialWriter = null;
        let serialReader = null;

        const statusBadge = document.getElementById('statusBadge');
        const statusText = document.getElementById('statusText');
        const ipInput = document.getElementById('ipAddress');
        const terminalLog = document.getElementById('terminalLog');
        const lastUpdateText = document.getElementById('lastUpdateText');
        const btnConnectUsb = document.getElementById('btnConnectUsb');
        const btnMode0 = document.getElementById('btnMode0');
        const btnMode3 = document.getElementById('btnMode3');
        const modeInfo = document.getElementById('modeInfo');
        const btnLaserDetectOn = document.getElementById('btnLaserDetectOn');
        const btnLaserDetectOff = document.getElementById('btnLaserDetectOff');
        const laserDetectInfo = document.getElementById('laserDetectInfo');

        const MODE_NAMES = { 0: 'Herní (0)', 1: 'ALARM laseru (1)', 2: 'Dohráno (2)', 3: 'Pracovní (3)' };
        const isOn = (v) => v === 1 || v === true || v === '1';

        function addLog(msg, type = 'info') {
            const entry = document.createElement('div');
            entry.className = 'log-entry ' + (type === 'err' ? 'log-err' : (type === 'cmd' ? 'log-cmd' : ''));
            const time = new Date().toLocaleTimeString();
            entry.textContent = `[${time}] ${msg}`;
            terminalLog.appendChild(entry);
            if (terminalLog.children.length > 50) {
                terminalLog.removeChild(terminalLog.children[0]);
            }
            terminalLog.scrollTop = terminalLog.scrollHeight;
        }

        // Automatické určení IP adresy
        let savedIp = localStorage.getItem('esp_master_ip');
        if (window.location.hostname && window.location.hostname !== '' && window.location.hostname !== 'localhost' && window.location.hostname !== '127.0.0.1') {
            savedIp = window.location.hostname;
        } else if (!savedIp || savedIp.includes('192.168.1.')) {
            savedIp = DEFAULT_IP;
        }
        ipInput.value = savedIp;
        localStorage.setItem('esp_master_ip', savedIp);

        ipInput.addEventListener('change', () => {
            const cleanIp = ipInput.value.trim().replace(/^https?:\/\//, '').replace(/\/.*$/, '');
            ipInput.value = cleanIp || DEFAULT_IP;
            localStorage.setItem('esp_master_ip', ipInput.value);
            addLog(`IP adresa změněna na: ${ipInput.value}`);
            scheduleNextPoll(100);
        });

        function getBaseUrl() {
            let ip = (ipInput.value.trim() || DEFAULT_IP);
            if (!ip.startsWith('http://') && !ip.startsWith('https://')) {
                ip = 'http://' + ip;
            }
            return ip;
        }

        function setBadgeStatus(badgeId, isOnline) {
            const el = document.getElementById(badgeId);
            if (!el) return;
            if (isOnline) {
                el.classList.add('online');
            } else {
                el.classList.remove('online');
            }
        }

        function setConnectionState(state, modeText = '') {
            if (state === 'connected') {
                isConnected = true;
                statusText.textContent = modeText || (serialPort ? 'Připojeno (USB)' : 'Připojeno (LAN)');
                statusBadge.className = 'status-indicator connected';
            } else if (state === 'connecting') {
                statusText.textContent = 'Připojování...';
                statusBadge.className = 'status-indicator connecting';
            } else {
                isConnected = false;
                statusText.textContent = 'Odpojeno';
                statusBadge.className = 'status-indicator';
            }
        }

        function handleTelemetryData(data) {
            setConnectionState('connected', serialPort ? 'Připojeno (USB)' : 'Připojeno (LAN)');
            lastUpdateText.textContent = 'Aktualizováno: ' + new Date().toLocaleTimeString();

            // Aktualizace aktivního módu (1 = alarm laseru je stále "hra", zvýrazní se ale textově)
            if (data.mode !== undefined) {
                const m = Number(data.mode);
                btnMode3.classList.toggle('active-mode', m === 3);
                btnMode0.classList.toggle('active-mode', m !== 3);
                modeInfo.textContent = 'Aktuální režim: ' + (MODE_NAMES[m] || m);
                modeInfo.classList.toggle('alarm', m === 1);
            }

            // Aktualizace stavu ignorování laseru
            if (data.laser && data.laser.ign !== undefined) {
                const isIgn = isOn(data.laser.ign);
                btnLaserDetectOff.classList.toggle('active-mode', isIgn);
                btnLaserDetectOn.classList.toggle('active-mode', !isIgn);
                if (laserDetectInfo) {
                    laserDetectInfo.textContent = isIgn
                        ? 'Stav snímání v herním módu: Vypnuto (Ignorovat)'
                        : 'Stav snímání v herním módu: Zapnuto (Výchozí)';
                }
            }

            // Aktualizace hardwarových modulů.
            // ESP posílá online příznaky vnořené v objektu "online" (viz buildTelemetryState v ESP_Master.ino).
            const o = data.online || {};
            setBadgeStatus('mod-eth', isOn(o.eth));
            setBadgeStatus('mod-laser', isOn(o.laser));
            setBadgeStatus('mod-tlacitka', isOn(o.tlacitka));
            setBadgeStatus('mod-kola', isOn(o.kola));
            setBadgeStatus('mod-lebka', isOn(o.lebka));
            setBadgeStatus('mod-svetla', isOn(o.svetla));
            setBadgeStatus('mod-audio', isOn(o.audio));
            setBadgeStatus('mod-m3', isOn(o.m3));
            setBadgeStatus('mod-tukani', isOn(o.m3_tukani));
            setBadgeStatus('mod-audio3', isOn(o.m3_audio3));
            setBadgeStatus('mod-wled1', isOn(o.wled1));
            setBadgeStatus('mod-wled2', isOn(o.wled2));
            setBadgeStatus('mod-wled3', isOn(o.wled3));

            // Aktualizace stavu Tlačítkové schránky (zda neblokuje kuličku a světla)
            const lockStatusInfo = document.getElementById('lockStatusInfo');
            if (lockStatusInfo && data.tlacitka) {
                const isLocked = isOn(data.tlacitka.lock);
                if (isLocked) {
                    lockStatusInfo.textContent = 'Tlačítková schránka: OTEVŘENA (Blokuje kuličku a světla)';
                    lockStatusInfo.classList.add('alarm');
                } else {
                    lockStatusInfo.textContent = 'Tlačítková schránka: ZAVŘENA';
                    lockStatusInfo.classList.remove('alarm');
                }
            }
        }

        // --- SÍŤOVÝ SPRÁVCE PRO ENC28J60 ---
        let isNetworkBusy = false;
        let pollTimer = null;
        let activeAbortController = null;
        let failedCount = 0;
        const MAX_FAILS_BEFORE_OFFLINE = 3;
        const NORMAL_POLL_INTERVAL = 2000;
        const RETRY_POLL_INTERVAL = 3000;

        function scheduleNextPoll(delayMs = NORMAL_POLL_INTERVAL) {
            if (pollTimer) {
                clearTimeout(pollTimer);
                pollTimer = null;
            }
            if (!serialPort) {
                pollTimer = setTimeout(fetchState, delayMs);
            }
        }

        async function fetchState() {
            if (serialPort) return;
            if (isNetworkBusy) {
                scheduleNextPoll(300);
                return;
            }

            const baseUrl = getBaseUrl();
            if (!baseUrl) {
                scheduleNextPoll(RETRY_POLL_INTERVAL);
                return;
            }

            isNetworkBusy = true;
            activeAbortController = new AbortController();
            const timeoutId = setTimeout(() => {
                if (activeAbortController) activeAbortController.abort();
            }, 3500);

            try {
                const response = await fetch(`${baseUrl}/api/state`, {
                    method: 'GET',
                    headers: { 'Accept': 'application/json' },
                    cache: 'no-store',
                    signal: activeAbortController.signal
                });
                clearTimeout(timeoutId);

                if (response.ok) {
                    const data = await response.json();
                    if (failedCount >= MAX_FAILS_BEFORE_OFFLINE || !isConnected) {
                        addLog('LAN připojení navázáno.', 'info');
                    }
                    failedCount = 0;
                    handleTelemetryData(data);
                    scheduleNextPoll(NORMAL_POLL_INTERVAL);
                } else {
                    throw new Error(`HTTP ${response.status}`);
                }
            } catch (error) {
                if (error.name !== 'AbortError') {
                    failedCount++;
                    if (failedCount >= MAX_FAILS_BEFORE_OFFLINE) {
                        if (isConnected) {
                            addLog(`LAN spojení ztraceno: ${error.message || 'Nedostupné'}`, 'err');
                        }
                        setConnectionState('disconnected');
                    } else if (isConnected) {
                        setConnectionState('connecting');
                    }
                    scheduleNextPoll(RETRY_POLL_INTERVAL);
                }
            } finally {
                isNetworkBusy = false;
                activeAbortController = null;
            }
        }

        async function sendCommand(cmd, btnElement = null) {
            if (btnElement) {
                btnElement.classList.add('pulse-btn');
                setTimeout(() => btnElement.classList.remove('pulse-btn'), 400);
            }

            addLog(`Odesílám příkaz: '${cmd}'`, 'cmd');

            if (serialPort && serialWriter) {
                try {
                    const encoder = new TextEncoder();
                    await serialWriter.write(encoder.encode(cmd + '\n'));
                    addLog(`USB příkaz '${cmd}' odeslán.`, 'info');
                } catch (err) {
                    addLog(`Chyba odeslání přes USB: ${err}`, 'err');
                }
                return;
            }

            const baseUrl = getBaseUrl();
            if (!baseUrl) {
                addLog('Chybí IP adresa ESP32!', 'err');
                return;
            }

            if (pollTimer) {
                clearTimeout(pollTimer);
                pollTimer = null;
            }
            if (activeAbortController) {
                activeAbortController.abort();
                activeAbortController = null;
            }

            let waitAttempts = 0;
            while (isNetworkBusy && waitAttempts < 2) {
                await new Promise(r => setTimeout(r, 50));
                waitAttempts++;
            }

            isNetworkBusy = true;
            const controller = new AbortController();
            const timeoutId = setTimeout(() => controller.abort(), 3500);

            try {
                const response = await fetch(`${baseUrl}/api/command`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'text/plain' },
                    body: cmd,
                    cache: 'no-store',
                    signal: controller.signal
                });
                clearTimeout(timeoutId);
                
                if (response.ok) {
                    failedCount = 0;
                    setConnectionState('connected', 'Připojeno (LAN)');
                    addLog(`Příkaz '${cmd}' úspěšně proveden.`, 'info');
                    scheduleNextPoll(100);
                } else {
                    addLog(`Chyba příkazu: HTTP ${response.status}`, 'err');
                    scheduleNextPoll(500);
                }
            } catch (error) {
                addLog(`Chyba při odesílání: ${error.message}`, 'err');
                scheduleNextPoll(1000);
            } finally {
                isNetworkBusy = false;
            }
        }

        // --- OBSLUHA WEB SERIAL (USB) ---
        async function connectUsb() {
            if (!('serial' in navigator)) {
                alert('Váš prohlížeč nepodporuje Web Serial API. Použijte Chrome, Edge nebo Opera.');
                return;
            }
            
            try {
                serialPort = await navigator.serial.requestPort();
                await serialPort.open({ baudRate: 115200 });
                
                serialWriter = serialPort.writable.getWriter();
                const textDecoder = new TextDecoderStream();
                serialPort.readable.pipeTo(textDecoder.writable);
                serialReader = textDecoder.readable.getReader();
                
                btnConnectUsb.textContent = 'USB Připojeno';
                btnConnectUsb.style.color = 'var(--status-green)';
                btnConnectUsb.style.borderColor = 'var(--status-green)';
                
                if (pollTimer) {
                    clearTimeout(pollTimer);
                    pollTimer = null;
                }

                addLog('USB Serial port otevřen (115200 bd).', 'info');
                readSerialLoop();
                
                const encoder = new TextEncoder();
                await serialWriter.write(encoder.encode('GET_STATE\n'));
            } catch (err) {
                addLog(`Chyba USB připojení: ${err.message || err}`, 'err');
            }
        }

        async function readSerialLoop() {
            let buffer = "";
            try {
                while (true) {
                    const { value, done } = await serialReader.read();
                    if (done) break;
                    if (value) {
                        buffer += value;
                        let lines = buffer.split('\n');
                        buffer = lines.pop();
                        
                        for (let line of lines) {
                            line = line.trim();
                            if (line.startsWith("TELEMETRY:")) {
                                try {
                                    const json = JSON.parse(line.substring(10));
                                    handleTelemetryData(json);
                                } catch(e) {}
                            } else if (line.length > 0) {
                                addLog(`ESP: ${line}`);
                            }
                        }
                    }
                }
            } catch (e) {
                addLog('USB odpojeno.', 'err');
                serialPort = null;
                setConnectionState('disconnected');
                btnConnectUsb.textContent = 'Připojit USB';
                btnConnectUsb.style.color = '';
                btnConnectUsb.style.borderColor = '';
                scheduleNextPoll(500);
            }
        }

        // Spuštění po načtení
        scheduleNextPoll(200);
    </script>
</body>
</html>
)rawliteral";

// Delka HTML bez ukoncovaci nuly - pouzito pro hlavicku Content-Length
const size_t DASHBOARD_HTML_LEN = sizeof(dashboard_html) - 1;

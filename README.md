# SORALink (ソラリンク)

![SORALink](soralink.jpg)

SORALink is a compact C bridge between network satellite receivers and [VLC media player](https://www.videolan.org/vlc/).

It supports two receiver backends:

- **WISI/S2D legacy receivers** through the original proprietary TCP/UDP protocol.
- **SAT>IP servers**, including the **TELESTAR DIGIBIT Twin**, through RTSP/RTP.

The recommended operating mode is the built-in HTTP/HTTPS server. It provides VLC-compatible M3U playlists, shared MPEG-TS streaming, an administration dashboard, channel scanning, XMLTV programme-guide support, and receiver-maintenance controls.

> [!IMPORTANT]
> Scans and native EPG updates require an idle upstream tuner. Close the official receiver application and stop active SORALink streams before maintenance. Legacy WISI/S2D devices also require an exclusive control session.

## What's new

The current release adds a complete SAT>IP path while preserving the existing WISI/S2D implementation:

- TELESTAR DIGIBIT Twin and generic SAT>IP RTSP/RTP support
- Full DVB-S/DVB-S2 tuning parameters: frequency, polarization, delivery system, modulation, roll-off, pilot, symbol rate, and FEC
- DIGIBIT firmware compatibility handling derived from the working Android SAT>IP library
  - Adds PID `21` when a request contains only low DVB-SI PIDs
  - Requests PMT PIDs explicitly instead of relying on problematic `pids=all` behavior
  - Splits large PID sets into DIGIBIT-compatible batches
- Local SAT>IP channel discovery by parsing PAT, PMT, and SDT tables
- Astra 19.2°E software scan using a bundled 55-transponder full-parameter table
- Automatic generation of `channels.xml`, `/playlist.m3u`, `/tv.m3u`, and `/radio.m3u`
- Native SAT>IP EPG collection from DVB EIT PID `18`, written as XMLTV
- Windows-specific SAT>IP fixes for scan memory usage, RTP reception, and tuner lock timing
- SAT>IP scan and EPG controls in the administration dashboard

## Features

### Streaming and server

- TV, radio, and combined M3U playlists
- Channel selection directly from VLC
- Up to 64 viewers sharing one tuned channel; default limit: 8
- Browser administration dashboard with live JSON APIs
- Viewer list, throughput, current channel, and now/next programme information
- Viewer disconnection, channel/EPG reload, and live settings controls
- UDP MPEG-TS output and optional `.ts` recording
- Automatic control reconnection and channel retuning
- MPEG-TS alignment and validation
- Viewer and administrator authentication
- Optional TLS 1.2+ through OpenSSL
- DNS, IPv4, and IPv6 support
- Windows and POSIX socket support
- `key=value` configuration files with command-line overrides
- Built-in protocol, parser, scan, EPG, and stream-processing self-tests

### WISI/S2D legacy backend

- Proprietary permission, tuning, heartbeat, lineup, EPG, and scan commands
- Tuning by saved programme index or DVB-S parameters
- Universal-LNB, 22 kHz tone, orbital-position, and DiSEqC configuration
- AES-128 even/odd packet processing and PID `0x1FFE` removal
- Native receiver lineup and EPG-cache refresh

### SAT>IP / DIGIBIT Twin backend

- RTSP `OPTIONS`, `SETUP`, `PLAY`, keepalive, and `TEARDOWN`
- Unicast RTP MPEG-TS reception with dynamic RTP/RTCP ports
- Exact full-parameter DVB-S and DVB-S2 tuning
- Software channel scan with PAT, PMT, and SDT parsing
- Native DVB EIT programme-guide collection
- DIGIBIT PID-list compatibility workarounds
- Generated channel PID filtering for lower network bandwidth

## Supported backends

| Backend | Select with | Control | Stream transport | Channel discovery | Native EPG |
|---|---|---|---|---|---|
| WISI/S2D legacy | `device_type=legacy` | Proprietary TCP, normally `8802` | Proprietary UDP, normally `8800` | Receiver database or receiver/software scan | Proprietary receiver EPG cache |
| SAT>IP / DIGIBIT Twin | `device_type=satip` or `digibit` | RTSP TCP, normally `554` | Dynamic RTP/RTCP UDP ports | SORALink software transponder scan | DVB EIT on PID `18` |

The default backend remains `legacy` for backward compatibility.

## Architecture

```text
WISI/S2D legacy receiver
  ├─ TCP 8802: permission, tuning, heartbeat, and maintenance
  └─ UDP 8800: MPEG-TS block requests and replies

SAT>IP server / DIGIBIT Twin
  ├─ TCP 554: RTSP control
  └─ Dynamic UDP ports: RTP MPEG-TS and RTCP
                    │
                    ▼
                 soralink
                  ├─ HTTP/HTTPS playlists and MPEG-TS → VLC clients
                  ├─ Dashboard and JSON APIs          → Web browser
                  ├─ XMLTV/native DVB EPG             → Programme guide
                  └─ UDP MPEG-TS forwarding           → VLC
```

The DIGIBIT Twin has two physical tuners. The current SORALink HTTP server uses one upstream tuner session at a time; many viewers can share that same tuned channel.

## Requirements

- A supported WISI/S2D receiver or SAT>IP server
- Network access between SORALink and the receiver
- VLC media player or another MPEG-TS client
- A C11 compiler
- The `web/` assets when the administration dashboard is enabled
- `transponders.conf` for custom software-scan presets
- OpenSSL development files only when native TLS or HTTPS XMLTV downloads are required

`channels.xml` may be absent at first startup. SORALink creates an empty channel document so a SAT>IP scan can populate it.

### Default ports

| Purpose | Protocol | Default |
|---|---:|---:|
| WISI/S2D control | TCP | `8802` |
| WISI/S2D MPEG-TS transport | UDP | `8800` |
| SAT>IP RTSP control | TCP | `554` |
| SAT>IP RTP/RTCP | UDP | Dynamic negotiated ports |
| Local HTTP server | TCP | `8080` |
| UDP-mode VLC output | UDP | `1234` |

## Repository layout

```text
.
├── soralink.c
├── README.md
├── README-SATIP.md            # additional DIGIBIT/SAT>IP notes
├── channels.xml               # generated or supplied channel list
├── epg.xml                    # optional/generated XMLTV data
├── transponders.conf          # software-scan presets
├── soralink.conf              # optional configuration file
├── soralink.jpg               # optional README image
├── Makefile
└── web/                       # required when the dashboard is enabled
    ├── index.html
    ├── styles.css
    ├── app.js
    ├── soralink-mascot.png
    ├── soralink-logo.png
    ├── soralink-avatar.png
    └── soralink-satellite.png
```

`channels.xml` is resolved from the current working directory by default. `web/`, `epg.xml`, and `transponders.conf` default to paths beside the executable. Override them with `--channels`, `--web-root`, `--epg`, and `--transponder-table`.

## Build

### Makefile

```bash
make clean
make
```

### Linux, macOS, and other POSIX systems

```bash
cc -std=c11 -O2 -Wall -Wextra -pedantic soralink.c -o soralink
```

### Windows with MinGW-w64 or w64devkit

```powershell
gcc -std=c11 -O2 -Wall -Wextra soralink.c -o soralink.exe -lws2_32
```

With w64devkit:

```bat
cd /d C:\w64devkit\home\SORALink\content
make clean
make
```

### Windows with Microsoft Visual C++

Run this from a Developer Command Prompt:

```powershell
cl /O2 /W4 soralink.c ws2_32.lib
```

The normal build uses built-in AES, HTTP, XML, XMLTV, DVB table, and socket implementations. It does not require a third-party runtime library.

### Optional OpenSSL build

Define `SORALINK_USE_OPENSSL` when either of these features is needed:

- HTTPS for the SORALink server
- HTTPS XMLTV downloads through `--epg-url`

POSIX with `pkg-config`:

```bash
cc -std=c11 -O2 -Wall -Wextra -pedantic \
  -DSORALINK_USE_OPENSSL \
  soralink.c -o soralink \
  $(pkg-config --cflags --libs openssl)
```

MinGW-w64:

```powershell
gcc -std=c11 -O2 -Wall -Wextra -DSORALINK_USE_OPENSSL `
  soralink.c -o soralink.exe -lws2_32 -lssl -lcrypto
```

Microsoft Visual C++:

```powershell
cl /O2 /W4 /DSORALINK_USE_OPENSSL soralink.c ws2_32.lib libssl.lib libcrypto.lib
```

OpenSSL library names and include/library paths vary by platform.

## Verify the build

The self-test does not require a receiver:

```bash
./soralink --self-test
```

The suite checks, among other things:

- AES-128 and MPEG-TS processing for the legacy backend
- Channel XML and XMLTV parsing
- DVB MJD/BCD time conversion
- SAT>IP tuning-query generation
- DIGIBIT PID `21` compatibility handling
- PAT, PMT, SDT, and EIT parsing
- SAT>IP scan service/PID generation
- Native SAT>IP EPG XMLTV generation
- HTTP authentication and administration APIs
- Astra 19.2°E transponder data

A successful run ends with:

```text
Self-test result: PASS
```

## Quick start: DIGIBIT Twin / SAT>IP

Create `soralink.conf` beside the executable:

```ini
[receiver]
device_type=satip
device=192.168.0.130
control_port=554
satip_source=1
satip_msys=auto

[server]
server=true
channels=channels.xml
http_bind=0.0.0.0
http_port=8088
max_clients=8
webui=true
web_root=web
admin_user=admin
admin_password=CHANGE_THIS_PASSWORD

[epg]
epg=epg.xml
epg_update=false
epg_update_timeout_ms=30000

[receiver_updates]
# Proprietary channel downloads are not used in SAT>IP mode.
device_channels_update=false

# Native DVB EIT collection through the DIGIBIT.
device_epg_update=true
device_epg_refresh_minutes=240

# Keep startup quick during initial setup; use the dashboard manually first.
device_update_on_start=false

# Manual scan from /admin/ when this is 0.
device_scan_refresh_minutes=0
device_scan_timeout_minutes=45
device_scan_mode=110
device_scan_epg_after=true
satellite_preset=astra-19.2e
transponder_table=transponders.conf

[network]
# Channel-table dwell per transponder; EPG uses a minimum of 5000 ms.
wait_ms=5000
timeout_ms=8000
verbose=true
```

Start SORALink:

```bat
soralink.exe
```

or:

```bash
./soralink --config soralink.conf
```

Open the dashboard:

```text
http://127.0.0.1:8088/admin/
```

From the dashboard:

1. Start **Scan channels**.
2. Wait for the new `channels.xml` to be installed.
3. Start **Update EPG now** if `device_scan_epg_after` is disabled or the first EPG collection was skipped.
4. Open a playlist in VLC.

```text
http://127.0.0.1:8088/playlist.m3u
http://127.0.0.1:8088/tv.m3u
http://127.0.0.1:8088/radio.m3u
```

Probe only the RTSP endpoint:

```bash
./soralink --device-type satip --device 192.168.0.130 --probe
```

> [!NOTE]
> `satip_source=1` selects the SAT>IP satellite source/DiSEqC position. It is not the physical tuner number; the server selects an available tuner.

## Quick start: WISI/S2D legacy receiver

```ini
[receiver]
device_type=legacy
device=soralink.local
control_port=8802
stream_port=8800

[server]
server=true
channels=channels.xml
http_bind=127.0.0.1
http_port=8080
max_clients=8
webui=true
web_root=web

[receiver_updates]
device_channels_update=false
device_epg_update=false
device_update_on_start=true

[network]
wait_ms=800
timeout_ms=3000
verbose=false
```

Probe the receiver:

```bash
./soralink --device-type legacy --device soralink.local --probe
```

Start the server:

```bash
./soralink --device-type legacy --device soralink.local --server --channels channels.xml
```

## Server behaviour

The first viewer tunes the requested channel. Additional viewers can join that same channel until `--max-clients` is reached.

While viewers are connected, a request for a different channel receives HTTP `409 Conflict`. Once all viewers disconnect, the next request can retune the receiver.

Receiver updates, scans, and native EPG collection require an idle tuner. New stream requests receive HTTP `503 Service Unavailable` while maintenance is active.

In SAT>IP mode, SORALink sends `TEARDOWN` when a stream or maintenance session ends. The server currently manages one active upstream channel even when the SAT>IP hardware contains multiple tuners.

## HTTP endpoints

### Media endpoints

| Endpoint | Methods | Description |
|---|---|---|
| `/` | `GET`, `HEAD`, `OPTIONS` | Alias for the combined playlist |
| `/playlist.m3u` | `GET`, `HEAD`, `OPTIONS` | TV and radio playlist |
| `/tv.m3u` | `GET`, `HEAD`, `OPTIONS` | TV-only playlist |
| `/radio.m3u` | `GET`, `HEAD`, `OPTIONS` | Radio-only playlist |
| `/status.json` | `GET`, `HEAD`, `OPTIONS` | Compact stream, viewer, channel, and backend status |
| `/channel/<lcn>` | `GET`, `HEAD`, `OPTIONS` | MPEG-TS stream for a logical channel number |

Viewer credentials, when configured, protect all media endpoints.

### Administration endpoints

| Endpoint | Method | Description |
|---|---:|---|
| `/admin/` | `GET` | Administration dashboard |
| `/admin/assets/*` | `GET` | Dashboard CSS, JavaScript, and images |
| `/admin/api/status` | `GET` | Full server, backend, channel, viewer, EPG, configuration, and maintenance state |
| `/admin/api/epg/<lcn>` | `GET` | Up to 100 relevant programmes for one logical channel number |
| `/admin/kick` | `POST` | Disconnect one viewer |
| `/admin/kick-all` | `POST` | Disconnect every viewer |
| `/admin/reload` | `POST` | Reload channels and EPG |
| `/admin/reload-epg` | `POST` | Reload only EPG data |
| `/admin/settings` | `POST` | Apply supported live settings and optionally persist them |
| `/admin/device/update-channels` | `POST` | Refresh legacy lineup; unavailable for SAT>IP |
| `/admin/device/update-epg` | `POST` | Refresh legacy EPG cache or collect SAT>IP DVB EIT |
| `/admin/device/update-all` | `POST` | Run the supported channel/EPG maintenance actions |
| `/admin/device/scan` | `POST` | Start a receiver scan or SAT>IP transponder sweep |
| `/admin/device/scan-cancel` | `POST` | Cancel an active scan |

Administration resources require administrator authorization. Mutating actions accept form-encoded `POST` requests and reject a mismatched `Origin` header.

## SAT>IP tuning and DIGIBIT compatibility

The DIGIBIT may return RTSP `200 OK` even when an incomplete satellite query will not produce an RTP stream. SORALink therefore sends the complete tuning parameter set whenever it is available:

```text
src, freq, pol, ro, msys, mtype, plts, sr, fec, pids
```

Example:

```text
?src=1&freq=11361.8&pol=h&ro=0.35&msys=dvbs2&mtype=8psk&plts=on&sr=22000&fec=23&pids=0,17,21
```

### DIGIBIT PID compatibility

The native library used by the working Android application applies a DIGIBIT workaround: a PID request containing only values up to `20` is extended with PID `21`.

SORALink follows the same rule:

```text
pids=0,17  →  pids=0,17,21
pids=18    →  pids=18,21
```

The scanner also avoids `pids=all` because affected DIGIBIT firmware may return only PID `0`. It requests PAT/SDT first, reads PMT PIDs from the PAT, and requests those PMTs explicitly in bounded batches.

## SAT>IP channel scan

The SAT>IP scan is performed locally by SORALink; it is not a proprietary DIGIBIT scan command and it is not a blind RF scan.

For each transponder, SORALink:

1. Opens an RTSP/RTP session with the full DVB tuning parameters.
2. Requests PAT and SDT using `pids=0,17,21`.
3. Reads service IDs and PMT PIDs from the PAT.
4. Requests PMTs explicitly in DIGIBIT-compatible batches.
5. Parses PMT stream/PCR/CA PIDs and SDT service names/types.
6. Merges duplicate services.
7. Atomically replaces `channels.xml`.
8. Reloads the M3U playlists immediately.

### Astra 19.2°E preset

The bundled fallback contains 55 unique full-parameter transponders. Select it with:

```ini
satellite_preset=astra-19.2e
transponder_table=transponders.conf
device_scan_mode=110
```

When an older external Astra section contains only frequency, polarization, and symbol rate, SORALink uses the bundled full-parameter table instead.

### Custom transponder table

```ini
[astra-19.2e]
11347.0,V,22000,dvbs2,8psk,0.35,on,23
11361.8,H,22000,dvbs2,8psk,0.35,on,23
11739.0,V,27500,dvbs,qpsk,0.35,on,34
```

Fields:

```text
frequency,polarization,symbol_rate,msys,mtype,rolloff,pilot,fec
```

The old three-field format remains accepted for custom presets, but complete entries are strongly recommended.

`timeout_ms` controls the initial RTP/MPEG-TS acquisition time and is clamped to 2–15 seconds in the SAT>IP lock path. `wait_ms` controls DVB table collection after lock and is clamped to 1800–8000 ms for channel scans.

## Channel XML

The server reads `<ch ...>` elements. Elements may span lines, and attributes may use single or double quotes.

SAT>IP-generated example:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ch_List>
  <ch type="TV" pol="H" sym="22000"
      s_id="11110" ts_id="1079" freq="11361.8"
      lcn="1" s_name="ZDF HD" fta="1"
      epg_id="1.1079.11110"
      msys="dvbs2" mtype="8psk" ro="0.35"
      plts="on" fec="23"
      pids="0,1,17,18,20,6100,6110,6120" />
</ch_List>
```

Legacy example:

```xml
<ch type="TV" pol="H" sym="22000" s_id="10301"
    freq="11494" lcn="1" fta="1"
    prog_idx="1085734912" ts_id="1011"
    epg_id="example.tv" s_name="Example TV" />
```

### Attributes

| Attribute | Required | Description |
|---|---:|---|
| `type` | Yes | `TV` or `RADIO`; normalized to uppercase |
| `pol` | Yes | `H`, `V`, `F`, or `O`; `F` and `O` are legacy receiver-specific modes |
| `sym` | Yes | Symbol rate in kSym/s |
| `s_id` | Yes | DVB service ID |
| `freq` | Yes | Transponder frequency in MHz; decimal SAT>IP values are supported |
| `lcn` | Yes | Unique logical channel number used in playlists and URLs |
| `s_name` | Yes | Display name shown in VLC and the dashboard |
| `fta` | No | Free-to-air flag; `0` is false and non-zero is true |
| `epg_id` | No | XMLTV channel ID; SAT>IP scans use `onid.tsid.sid` |
| `xmltv_id` | No | Alias for `epg_id` |
| `prog_idx` | Legacy only | Saved receiver programme index used by legacy tuning/EPG retrieval |
| `ts_id` | No | DVB transport-stream ID |
| `msys` | SAT>IP | `dvbs`, `dvbs2`, or `auto` |
| `mtype` | SAT>IP | `qpsk`, `8psk`, `16apsk`, or `32apsk` |
| `ro` | SAT>IP | Roll-off, normally `0.20`, `0.25`, or `0.35` |
| `plts` | SAT>IP | Pilot mode: `on`, `off`, or `auto` |
| `fec` | SAT>IP | SAT>IP FEC code such as `23`, `34`, `56`, `78`, or `910` |
| `pids` | SAT>IP | Comma-separated MPEG-TS PID filter or `all` |

Duplicate `lcn` values are skipped with a warning.

The parser decodes standard XML entities and decimal/hexadecimal numeric entities. Malformed entries are skipped, a single tag is limited to 64 KiB, and the complete file is limited to 64 MiB.

## Programme guide and XMLTV

SORALink uses XMLTV for dashboard now/next information and per-channel schedules. The default path is `epg.xml` beside the executable. A missing EPG file is non-fatal.

There are three EPG sources:

1. A local XMLTV file.
2. An optional HTTP/HTTPS XMLTV download.
3. Native receiver EPG:
   - WISI/S2D proprietary EPG cache
   - SAT>IP DVB EIT collection

### Native SAT>IP EPG

SORALink groups scanned channels by transponder, tunes each unique multiplex once, and requests:

```text
pids=18,21
```

PID `18` carries DVB EIT. PID `21` is the DIGIBIT compatibility PID used by the working Android library.

The collector parses:

- `0x4E–0x4F`: present/following events
- `0x50–0x5F`: schedule for the current transport stream
- `0x60–0x6F`: schedule for other transport streams
- Short-event descriptors for title and summary
- Extended-event descriptors for longer descriptions
- Content descriptors for categories

Events are matched through the generated channel identity:

```text
original_network_id.transport_stream_id.service_id
```

Enable native SAT>IP EPG:

```ini
device_epg_update=true
device_epg_refresh_minutes=240
device_scan_epg_after=true
```

Use **Update EPG now** in `/admin/` for a manual refresh. `wait_ms` is clamped to 5000–30000 ms per transponder for EPG collection. Larger values can capture more schedule sections but increase the total refresh time.

Existing future XMLTV entries are retained, and matching broadcast events are updated.

### External XMLTV download

```ini
epg=epg.xml
epg_update=true
epg_url=https://example.test/guide.xml
epg_update_timeout_ms=30000
```

Important behavior:

- `epg_update=true` requires `epg_url`.
- Plain HTTP works in the normal build.
- HTTPS requires an OpenSSL build.
- HTTPS verifies the certificate and hostname.
- TLS 1.2 or newer is required.
- Normal and chunked HTTP response bodies are supported.
- Up to five redirects are followed.
- Compressed response bodies are rejected.
- A failed startup download leaves the existing local XMLTV file available.

External XMLTV channel IDs must match each channel's `epg_id` or `xmltv_id`.

## Administration dashboard

The dashboard serves static files from `web/` and provides:

- Current backend, channel, and stream throughput
- Connected viewers and connection durations
- Individual or all-viewer disconnection
- TV/radio channel browsing and stream links
- Now/next information and channel schedules
- Channel-list and EPG reload controls
- Backend-aware lineup and EPG maintenance controls
- SAT>IP scan start, cancellation, progress, and result counts
- Live server, scan, and satellite settings
- Optional persistence of supported settings to the active config file

### Dashboard credentials

```ini
admin_user=admin
admin_password=replace-this-admin-password
```

Both administrator options must be present together. Without a dedicated administrator pair, the dashboard uses viewer credentials when configured.

SORALink refuses to expose an enabled dashboard on a non-loopback listener unless administrator or viewer credentials are configured.

## Receiver maintenance by backend

| Action | WISI/S2D legacy | SAT>IP / DIGIBIT |
|---|---|---|
| `device_channels_update` | Downloads proprietary receiver lineup | Disabled; use software scan |
| `device_epg_update` | Downloads proprietary receiver EPG cache | Collects DVB EIT and writes XMLTV |
| `device_scan_mode=110` | Full software preset sweep when available | Full software preset sweep |
| `device_scan_mode=0` | Receiver automatic/compatibility scan | SAT>IP still requires a transponder preset |
| `device_scan_epg_after` | Refreshes legacy EPG after scan | Collects DVB EIT after scan |

All maintenance requires no connected viewers.

## Configuration file

SORALink automatically loads `soralink.conf` beside the executable when present. An explicit file can be selected with:

```bash
./soralink --config soralink.conf
```

The configuration file is loaded first. Command-line options are then applied and take precedence.

Configuration rules:

- Empty lines are ignored.
- Lines beginning with `#` or `;` are comments.
- Lines beginning with `[` are ignored, allowing INI-style section headers.
- Keys are case-normalized.
- Hyphens and underscores in keys are treated the same.
- Values may be wrapped in matching single or double quotes.
- Unknown or invalid settings stop startup.
- Credentials are stored as plaintext; protect the file with filesystem permissions.
- Dashboard persistence is available only when a configuration file is active.

## Single-channel UDP mode

UDP mode forwards MPEG-TS to `127.0.0.1:1234` by default.

Open this URL in VLC first:

```text
udp://@:1234
```

### WISI/S2D saved programme

```bash
./soralink --device-type legacy --device soralink.local --progidx 0
```

### Direct DVB-S tuning

```bash
./soralink \
  --device-type legacy \
  --device soralink.local \
  --freq 11494 \
  --sr 22000 \
  --pol H \
  --sid 10301 \
  --orbital 192
```

SAT>IP does not support `--progidx`. Use direct DVB parameters or, preferably, server mode with scanned channel metadata so full modulation/FEC/roll-off/pilot values are available.

### Forward to another host

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --vlc-ip media-pc.local \
  --vlc-port 1234
```

### Record the processed stream

```bash
./soralink --device soralink.local --progidx 0 --dump recording.ts
```

## Stream keys and encrypted packets

This section applies to the WISI/S2D legacy backend. SAT>IP mode disables the legacy AES key path.

SORALink enables a legacy built-in even AES-128 key by default. It can be replaced, supplemented with an odd key, or disabled.

```bash
./soralink \
  --device-type legacy \
  --device soralink.local \
  --progidx 0 \
  --even-key 00112233445566778899aabbccddeeff \
  --odd-key 00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff
```

Missing-key policy:

| Mode | Behaviour |
|---|---|
| `pass` | Forward the encrypted packet unchanged and preserve its scrambling marker; default |
| `drop` | Discard the encrypted packet |

Use keys only with hardware, services, and content that you are authorized to access.

## Satellite and LNB settings

The `orbital`, `tone`, `lnb_*`, `diseqc`, and `sat_setup` options configure the proprietary WISI/S2D satellite setup sequence.

For SAT>IP, the SAT>IP server controls the LNB. Use `satip_source` to select the configured satellite source/DiSEqC position.

Legacy example:

```bash
./soralink \
  --device-type legacy \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --sat-setup \
  --lnb-low 9750 \
  --lnb-high 10600 \
  --lnb-switch 11700 \
  --diseqc 1
```

## Network access and security

The server listens on `127.0.0.1` by default.

To expose it on a trusted local network:

```bash
./soralink \
  --device-type satip \
  --device 192.168.0.130 \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0 \
  --http-user vlc \
  --http-password 'change-this-viewer-password' \
  --admin-user admin \
  --admin-password 'change-this-admin-password'
```

> [!WARNING]
> Plain HTTP does not encrypt credentials, configuration data, viewer addresses, channel activity, or stream content. Use TLS, a trusted TLS reverse proxy, and firewall rules when traffic crosses an untrusted network.

### HTTPS

TLS requires an OpenSSL build and both PEM files:

```bash
./soralink \
  --device-type satip \
  --device 192.168.0.130 \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0 \
  --admin-user admin \
  --admin-password 'change-this-admin-password' \
  --tls-cert server-chain.pem \
  --tls-key server-key.pem
```

## Status APIs

### `/status.json`

Idle response:

```json
{
  "streaming": false,
  "clients": 0
}
```

While streaming, it includes the active channel and client count. Backend-specific data includes SAT>IP settings or legacy AES counters as appropriate.

### `/admin/api/status`

The authenticated dashboard API includes:

- Server time and uptime
- Selected backend and control settings
- Current transfer rate and total bytes
- Current channel
- Configured channels with stream URLs and now/next data
- Viewer IDs, addresses, ports, and connection durations
- XMLTV load state
- Native maintenance state
- Scan state, preset, progress, frequency, mode, and result counts

### `/admin/api/epg/<lcn>`

Returns up to 100 programmes around the current time for one channel.

## Command-line reference

### Backend and receiver

| Option | Description | Default |
|---|---|---|
| `--config FILE` | Load settings before command-line overrides | Auto-load `soralink.conf` beside executable |
| `--device HOST` | Receiver DNS name, IPv4 address, or IPv6 address | Required except for `--self-test` |
| `--device-type legacy\|satip` | Select WISI/S2D or SAT>IP backend; `digibit` is accepted | `legacy` |
| `--control-port N` | Legacy control or SAT>IP RTSP port | `8802`; becomes `554` for SAT>IP unless explicitly set |
| `--stream-port N` | Legacy UDP stream port | `8800` |
| `--satip-source N` | SAT>IP source/DiSEqC position, `1..255` | `1` |
| `--satip-msys auto\|dvbs\|dvbs2` | Fallback delivery system when channel metadata is incomplete | `auto` |

### HTTP, dashboard, and XMLTV

| Option | Description | Default |
|---|---|---|
| `--server`, `--http-server` | Run the HTTP/HTTPS playlist and stream server | Off |
| `--channels FILE` | Channel XML file | `channels.xml` |
| `--http-bind HOST` | Listen address or hostname | `127.0.0.1` |
| `--http-port N` | HTTP listen port | `8080` |
| `--max-clients N` | Simultaneous viewers of one channel; `1..64` | `8` |
| `--http-user USER` | Viewer username; requires password | Off |
| `--http-password PASS` | Viewer password; requires username | Off |
| `--admin-user USER` | Dashboard administrator username | Viewer credentials or off |
| `--admin-password PASS` | Dashboard administrator password | Viewer credentials or off |
| `--webui` / `--no-webui` | Enable or disable dashboard | Enabled |
| `--web-root DIR` | Dashboard assets directory | `web` beside executable |
| `--epg FILE` | XMLTV file; empty config value disables it | `epg.xml` beside executable |
| `--epg-update` / `--no-epg-update` | Download external XMLTV at startup | Off |
| `--epg-url URL` | HTTP/HTTPS source for external XMLTV | — |
| `--epg-update-timeout-ms N` | External XMLTV timeout, `1000..120000` ms | `30000` |
| `--tls-cert FILE` | TLS certificate chain PEM | Off |
| `--tls-key FILE` | TLS private-key PEM | Off |

### Native maintenance and scans

| Option | Description | Default |
|---|---|---|
| `--device-channels-update` | Enable proprietary legacy lineup refresh; disabled in SAT>IP mode | Off |
| `--device-epg-update` | Enable legacy EPG-cache refresh or SAT>IP DVB EIT collection | Off |
| `--device-channels-refresh-minutes N` | Lineup refresh interval; `0` disables | `1440` |
| `--device-epg-refresh-minutes N` | EPG refresh interval; `0` disables | `240` |
| `--device-scan-refresh-minutes N` | Scan interval; `0` disables | `0` |
| `--device-scan-timeout-minutes N` | Overall scan timeout, `1..120` minutes | `45` |
| `--device-scan-mode N` | `110` full preset sweep, `0` compatibility; `126` alias for `110` | `110` |
| `--satellite-preset KEY` | Preset section used by software scan | `custom` |
| `--transponder-table FILE` | Preset file | `transponders.conf` beside executable |
| `--device-scan-search-range N` | Legacy receiver scan range code, `0..7` | `0` |
| `--device-scan-order-by N` | Legacy receiver ordering code, `0..3` | `0` |
| `--device-scan-sort-mode N` | Legacy receiver sorting code, `0..3` | `0` |
| `--device-scan-network` | Enable legacy network scan flag | Enabled |
| `--device-scan-epg-after` | Refresh/collect native EPG after scan | Enabled |
| `--device-scan-apply-satellite` | Apply configured legacy LNB/DiSEqC profile before scan | Off |
| `--device-update-on-start` | Run enabled native updates during startup | Enabled |

Each boolean option also has a corresponding `--no-...` form where shown in `--help`.

### Tuning and satellite

| Option | Description | Default |
|---|---|---|
| `--progidx N` | Tune legacy saved programme index | — |
| `--freq MHz` | Transponder frequency in MHz | — |
| `--sr N` | Symbol rate in kSym/s | — |
| `--pol H\|V\|F\|O` | Polarization or legacy voltage mode | — |
| `--sid N` | DVB service ID | — |
| `--orbital N` | Legacy orbital position in tenths of a degree | `192` |
| `--east` / `--west` | Legacy orbital direction | East |
| `--tone auto\|on\|off` | Legacy 22 kHz tone mode | `auto` |
| `--lnb-low N` | Legacy low-band LNB LO | `9750` |
| `--lnb-high N` | Legacy high-band LNB LO | `10600` |
| `--lnb-switch N` | Legacy LNB switch frequency | `11700` |
| `--diseqc N` | Legacy DiSEqC input | `0` |
| `--sat-setup` / `--no-sat-setup` | Enable/disable extended legacy setup | Disabled |

### Stream processing and UDP output

| Option | Description | Default |
|---|---|---|
| `--even-key HEX` | Set legacy 16-byte even AES key | Built-in legacy key |
| `--odd-key HEX` | Set legacy 16-byte odd AES key | Unset |
| `--no-default-key` | Disable built-in legacy even key | Off |
| `--missing-key pass\|drop` | Forward or discard unmatched encrypted packets | `pass` |
| `--vlc-ip HOST` | UDP destination | `127.0.0.1` |
| `--vlc-port N` | UDP destination port | `1234` |
| `--dump FILE.ts` | Save processed UDP-mode stream | Off |
| `--wait-ms N` | Post-tune/table collection duration | `800` |
| `--timeout-ms N` | Network/initial SAT>IP lock timeout | `3000` |

### Diagnostics

| Option | Description |
|---|---|
| `--probe` | Test receiver reachability without streaming |
| `--self-test` | Run internal tests without a receiver |
| `--verbose` | Print protocol frames and diagnostics |
| `--help`, `-h` | Show built-in help |

## Recovery behaviour

### WISI/S2D

SORALink sends a control heartbeat approximately every five seconds. On failure it attempts to reconnect, reacquire permission, reapply satellite setup when enabled, and retune the active channel.

### SAT>IP

SORALink opens a new RTSP/RTP session for tuning, applies the complete saved channel parameters, waits for a valid MPEG-TS packet, and tears the session down when finished. The HTTP path retries after stream stalls. Maintenance sessions are also closed before the next transponder is attempted.

## Troubleshooting

### SAT>IP server is reachable, but no channels are found

Run with:

```ini
wait_ms=5000
timeout_ms=8000
verbose=true
```

Confirm the `SETUP` query contains the full parameter set and the scan PID workaround:

```text
ro=...&msys=...&mtype=...&plts=...&sr=...&fec=...&pids=0,17,21
```

Also verify:

- `satip_source` selects the source used by the working SAT>IP application.
- No other client occupies the DIGIBIT tuners.
- Windows Firewall allows `soralink.exe` to receive UDP traffic.
- The correct full-parameter transponder table is installed.
- The dish/LNB path can tune the same service in another SAT>IP application.

### Only a few SAT>IP transponders lock

- Confirm the log shows the expected DVB-S/DVB-S2 mode, modulation, roll-off, pilot, and FEC.
- Replace old three-column Astra data with the bundled `transponders.conf`.
- Increase `timeout_ms` up to `8000` or `12000` on slow tuner changes.
- Power-cycle the DIGIBIT after interrupted tests if old sessions appear stuck.

### SAT>IP EPG is empty

- Complete a channel scan first; native EPG requires generated `epg_id=onid.tsid.sid` identities.
- Enable `device_epg_update=true`.
- Disconnect viewers before updating.
- Confirm the log contains a request with `pids=18,21`.
- Increase `wait_ms`; EPG collection uses at least 5000 ms per transponder.
- Some broadcasters transmit only limited present/following data or incomplete schedules.

### WISI/S2D control port is unreachable

- Confirm the receiver hostname or IP address.
- Confirm TCP port `8802` or the configured alternative.
- Fully close the official receiver application and other clients.
- Verify network and firewall rules.

### Receiver is occupied

Legacy devices require an exclusive permission session. SAT>IP devices have a finite tuner/session count. Close other applications and power-cycle the device if abandoned sessions remain.

### VLC opens the playlist but cannot play a channel

- Open `/channel/<lcn>` directly in VLC.
- Check `/status.json` and `/admin/api/status`.
- Confirm no scan or EPG update is active.
- For SAT>IP, verify the channel contains `msys`, `mtype`, `ro`, `plts`, `fec`, and `pids`.
- When authentication is enabled, ensure VLC sends credentials for both playlist and stream URLs.

### EPG does not match channels

- For SAT>IP native EPG, retain the generated `epg_id` values.
- For external XMLTV, map the provider's channel IDs to `epg_id`/`xmltv_id`.
- Reload EPG after replacing the XMLTV file.

### Dashboard asset is missing

- Confirm all files exist in the configured `web/` directory.
- Use `--web-root DIR` when assets are elsewhere.
- Use `--no-webui` when the dashboard is not required.

### HTTP bind fails

Another process may already use the address or port:

```bash
./soralink --device 192.168.0.130 --device-type satip --server --http-port 8090
```

For IPv6 URLs, use brackets:

```text
http://[::1]:8080/playlist.m3u
```

### TLS support is unavailable

Rebuild with `-DSORALINK_USE_OPENSSL`, link `libssl` and `libcrypto`, and supply both `--tls-cert` and `--tls-key`.

## Limitations

- The HTTP server currently tunes one upstream channel at a time, even on multi-tuner SAT>IP hardware.
- Multiple viewers can share only the currently tuned channel.
- SAT>IP scanning is a preset/transponder-table scan, not a blind RF scan.
- SAT>IP channel downloads are not available through the proprietary legacy database command.
- Native EIT completeness depends on what each broadcaster transmits and the collection dwell time.
- The dashboard assets are external and are not embedded in the executable.
- The HTTP service is purpose-built for SORALink and is not a general-purpose web server.
- Native TLS and HTTPS XMLTV downloads require an OpenSSL build.
- Channel XML and XMLTV parsing intentionally support only the documented subset.
- The XMLTV downloader expects uncompressed content and does not support arbitrary relative redirects.
- Legacy lineup, EPG, and scan commands depend on receiver implementation and firmware.
- The bundled software scan is specifically defined for Astra `19.2°E`; other satellites require a suitable preset.
- Hardware compatibility depends on the target receiver and firmware.

## Stopping SORALink

Press `Ctrl+C`. SORALink handles `SIGINT`/`SIGTERM` on POSIX and the equivalent console events on Windows, closes sockets, sends SAT>IP teardown when applicable, releases TLS resources, and exits.

## Disclaimer

Use SORALink only with hardware, services, keys, and content that you are authorized to access. This is an independent interoperability project and is not presented as an official application from TELESTAR, WISI, S2D, the SAT>IP Alliance, or VLC.

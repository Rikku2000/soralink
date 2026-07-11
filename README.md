# SORALink (ソラリンク)

![SORALink](soralink.jpg)

SORALink is a compact C bridge between a compatible network satellite tuner/dongle and [VLC media player](https://www.videolan.org/vlc/).

It acquires the receiver's exclusive control session, tunes saved programmes or DVB-S services, requests MPEG transport-stream blocks, validates and processes the packets, and forwards the resulting stream to VLC.

The recommended operating mode is the built-in HTTP/HTTPS server. It provides VLC-compatible M3U playlists, shared MPEG-TS streaming, an administration dashboard, XMLTV programme-guide support, and receiver-maintenance controls.

> [!IMPORTANT]
> SORALink uses an exclusive receiver control session. Fully close the official receiver application and every other client before starting SORALink.

## Features

- TV, radio, and combined M3U playlists
- Channel selection directly from VLC
- Up to 64 viewers sharing one tuned channel; default limit: 8
- Browser administration dashboard with live JSON APIs
- Viewer list, throughput, current channel, and now/next programme information
- Viewer disconnection, channel/EPG reload, and live settings controls
- Local XMLTV loading and optional HTTP/HTTPS XMLTV download
- Native receiver lineup and EPG refresh
- Manual and scheduled receiver scans with progress reporting
- Astra 19.2°E software transponder sweep in the default full-scan mode
- Direct DVB-S tuning or tuning by saved programme index
- UDP MPEG-TS output and optional `.ts` recording
- Universal-LNB, 22 kHz tone, orbital-position, and DiSEqC configuration
- Automatic control reconnection and channel retuning
- MPEG-TS alignment and validation
- AES-128 even/odd packet processing and PID `0x1FFE` removal
- Viewer and administrator HTTP Basic authentication
- Optional TLS 1.2+ through OpenSSL
- DNS, IPv4, and IPv6 support
- Windows and POSIX socket support
- `key=value` configuration files with command-line overrides
- Built-in protocol, parser, scan, and stream-processing self-tests

## Architecture

```text
Compatible receiver
  ├─ TCP 8802: permission, tuning, heartbeat, and maintenance
  └─ UDP 8800: MPEG-TS block requests and replies
                    │
                    ▼
                 soralink
                  ├─ HTTP/HTTPS playlists and MPEG-TS → VLC clients
                  ├─ Dashboard and JSON APIs          → Web browser
                  ├─ XMLTV/native receiver EPG        → Programme guide
                  └─ UDP MPEG-TS forwarding           → VLC
```

The receiver ports can be changed with `--control-port` and `--stream-port`.

## Requirements

- A compatible network tuner/dongle
- Network access to its TCP control and UDP stream ports
- VLC media player or another MPEG-TS client
- A C11 compiler
- A channel XML file for normal server startup, unless startup receiver refresh can create it
- The `web/` assets when the administration dashboard is enabled
- OpenSSL development files only when native TLS or HTTPS XMLTV downloads are required

Default ports:

| Purpose | Protocol | Port |
|---|---:|---:|
| Receiver control | TCP | `8802` |
| Receiver MPEG-TS transport | UDP | `8800` |
| Local HTTP server | TCP | `8080` |
| UDP-mode VLC output | UDP | `1234` |

## Repository layout

```text
.
├── soralink.c
├── README.md
├── channels.xml               # required for normal server startup
├── epg.xml                    # optional XMLTV data
├── soralink.conf              # optional configuration file
├── soralink.jpg               # optional README image
└── web/                       # required when the dashboard is enabled
    ├── index.html
    ├── styles.css
    ├── app.js
    ├── soralink-mascot.png
    ├── soralink-logo.png
    ├── soralink-avatar.png
    └── soralink-satellite.png
```

`channels.xml` is resolved from the current working directory by default. `web/` and `epg.xml` default to paths beside the executable. Override them with `--channels`, `--web-root`, and `--epg`.

## Build

### Linux, macOS, and other POSIX systems

```bash
cc -std=c11 -O2 -Wall -Wextra -pedantic soralink.c -o soralink
```

### Windows with MinGW-w64

```powershell
gcc -std=c11 -O2 -Wall -Wextra soralink.c -o soralink.exe -lws2_32
```

### Windows with Microsoft Visual C++

Run this from a Developer Command Prompt:

```powershell
cl /O2 /W4 soralink.c ws2_32.lib
```

The normal build uses the built-in AES, HTTP, XML, XMLTV, and socket implementations. It does not require a third-party runtime library.

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

The current suite checks:

- AES-128 known-answer decryption and separated-hex key parsing
- Even/odd MPEG-TS packet processing
- Missing-key pass/drop behaviour
- Multiline channel XML, EPG IDs, programme indexes, transport-stream IDs, and entities
- XMLTV timestamps, UTC offsets, programme text, and entity decoding
- Native receiver EPG XML and MJD conversion
- HTTP Basic-auth encoding
- HTTP/HTTPS EPG URL parsing and chunked-body decoding
- Receiver scan command framing and status requests
- Receiver scan flag layout
- Astra 19.2°E transponder sweep data
- Protocol response validation
- Automatic LNB tone selection

A successful run ends with:

```text
Self-test result: PASS
```

## Quick start

### 1. Probe the receiver

```bash
./soralink --device soralink.local --probe
```

A successful probe confirms that the TCP control port is reachable and that SORALink can claim the receiver session.

For non-default receiver ports:

```bash
./soralink \
  --device soralink.local \
  --control-port 8802 \
  --stream-port 8800 \
  --probe
```

### 2. Start the channel server

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml
```

Open the combined playlist in VLC:

```text
http://127.0.0.1:8080/playlist.m3u
```

### 3. Open the dashboard

The dashboard is enabled by default in server mode:

```text
http://127.0.0.1:8080/admin/
```

Disable it when only the media endpoints are needed:

```bash
./soralink --device soralink.local --server --no-webui
```

## Server behaviour

The first viewer tunes the requested channel. Additional viewers can join that same channel until `--max-clients` is reached.

The receiver exposes one active tuner path. While viewers are connected, a request for a different channel receives HTTP `409 Conflict`. Once all viewers disconnect, the next request can retune the receiver.

Receiver updates and scans require an idle tuner. New stream requests receive HTTP `503 Service Unavailable` while maintenance is active.

## HTTP endpoints

### Media endpoints

| Endpoint | Methods | Description |
|---|---|---|
| `/` | `GET`, `HEAD`, `OPTIONS` | Alias for the combined playlist |
| `/playlist.m3u` | `GET`, `HEAD`, `OPTIONS` | TV and radio playlist |
| `/tv.m3u` | `GET`, `HEAD`, `OPTIONS` | TV-only playlist |
| `/radio.m3u` | `GET`, `HEAD`, `OPTIONS` | Radio-only playlist |
| `/status.json` | `GET`, `HEAD`, `OPTIONS` | Compact stream, viewer, channel, and AES status |
| `/channel/<lcn>` | `GET`, `HEAD`, `OPTIONS` | MPEG-TS stream for a logical channel number |

Viewer credentials, when configured, protect all media endpoints.

### Administration endpoints

| Endpoint | Method | Description |
|---|---:|---|
| `/admin/` | `GET` | Administration dashboard |
| `/admin/assets/*` | `GET` | Dashboard CSS, JavaScript, and images |
| `/admin/api/status` | `GET` | Full server, channel, viewer, EPG, configuration, and maintenance state |
| `/admin/api/epg/<lcn>` | `GET` | Up to 100 relevant programmes for one logical channel number |
| `/admin/kick` | `POST` | Disconnect one viewer |
| `/admin/kick-all` | `POST` | Disconnect every viewer |
| `/admin/reload` | `POST` | Reload channels and EPG |
| `/admin/reload-epg` | `POST` | Reload only EPG data |
| `/admin/settings` | `POST` | Apply supported live settings and optionally persist them |
| `/admin/device/update-channels` | `POST` | Refresh the lineup from the receiver |
| `/admin/device/update-epg` | `POST` | Refresh XMLTV from the receiver EPG cache |
| `/admin/device/update-all` | `POST` | Refresh lineup and EPG |
| `/admin/device/scan` | `POST` | Start a receiver scan |
| `/admin/device/scan-cancel` | `POST` | Cancel an active scan |

Administration resources require administrator authorization. Mutating actions accept form-encoded `POST` requests and reject a mismatched `Origin` header.

## Channel XML

The server reads `<ch ...>` elements. Elements may span lines, and attributes may use single or double quotes.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<channels>
  <ch
    type="TV"
    pol="H"
    sym="22000"
    s_id="10301"
    freq="11494"
    lcn="1"
    fta="1"
    prog_idx="1085734912"
    ts_id="1011"
    epg_id="example.tv"
    s_name="Example TV" />

  <ch type='radio' pol='V' sym='27500' s_id='10402'
      freq='11538' lcn='101' fta='1'
      xmltv_id='example.radio'
      s_name='Example Radio &#9733;' />
</channels>
```

### Attributes

| Attribute | Required | Description |
|---|---:|---|
| `type` | Yes | `TV` or `RADIO`; normalized to uppercase |
| `pol` | Yes | `H`, `V`, `F`, or `O`; `F` and `O` are receiver-specific voltage modes |
| `sym` | Yes | Symbol rate in kSym/s |
| `s_id` | Yes | DVB service ID |
| `freq` | Yes | Transponder frequency in MHz |
| `lcn` | Yes | Unique logical channel number used in playlists and URLs |
| `s_name` | Yes | Display name shown in VLC and the dashboard |
| `fta` | No | Free-to-air flag; `0` is false and any non-zero value is true |
| `epg_id` | No | XMLTV channel ID; defaults to decimal `lcn` |
| `xmltv_id` | No | Alias for `epg_id` |
| `prog_idx` | No | Saved receiver programme index used by native EPG retrieval |
| `ts_id` | No | DVB transport-stream ID |

Duplicate `lcn` values are skipped with a warning.

The parser decodes `&amp;`, `&apos;`, `&quot;`, `&lt;`, `&gt;`, decimal numeric entities, and hexadecimal numeric entities.

The channel parser is intentionally lightweight:

- Attribute names must match the documented names.
- Malformed entries are skipped.
- A single tag is limited to 64 KiB.
- The complete XML file is limited to 64 MiB.
- Frequency and symbol-rate values must fit the receiver protocol fields.

## Programme guide and XMLTV

SORALink uses XMLTV for dashboard now/next information and per-channel schedules.

The default path is `epg.xml` beside the executable. A missing EPG file is non-fatal; the server starts with an empty guide.

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --epg guide.xml
```

Each channel's `epg_id` or `xmltv_id` must match the XMLTV `<channel id="...">` and `<programme channel="...">` values. Without either attribute, the decimal `lcn` is used.

The parser reads programme start/stop timestamps and these common elements:

- `title`
- `sub-title`
- `desc`
- `category`

### Download XMLTV at startup

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --epg epg.xml \
  --epg-update \
  --epg-url https://example.test/guide.xml
```

Important behaviour:

- `--epg-update` requires `--epg-url`.
- Plain HTTP works in the normal build.
- HTTPS requires an OpenSSL build.
- HTTPS verifies the server certificate and hostname using the system/OpenSSL trust store.
- TLS 1.2 or newer is required.
- Normal and chunked HTTP response bodies are supported.
- Up to five redirects are followed.
- Absolute redirects are supported; arbitrary relative redirect paths are not.
- Compressed response bodies are rejected; use an uncompressed XML URL.
- A failed startup download leaves the existing local file available for loading.
- `--epg-update-timeout-ms` accepts `1000` to `120000`; default: `30000`.

## Administration dashboard

The dashboard serves static files from `web/` by default and provides:

- Current channel and stream throughput
- Connected viewers and connection durations
- Individual or all-viewer disconnection
- TV/radio channel browsing and stream links
- Now/next information and channel schedules
- Channel-list and EPG reload controls
- Native lineup and EPG refresh
- Scan start, cancellation, progress, and result counts
- Live server, scan, and satellite settings
- Optional persistence of supported settings to the active config file

### Dashboard credentials

Configure a dedicated administrator pair:

```bash
./soralink \
  --device soralink.local \
  --server \
  --admin-user admin \
  --admin-password 'change-this-password'
```

Both administrator options must be present together. When they are omitted, the dashboard falls back to the viewer credentials.

For safety, SORALink refuses to expose an enabled dashboard on a non-loopback listener unless either administrator or viewer credentials are configured.

## Receiver lineup, EPG, and scans

Native maintenance commands are receiver- and firmware-specific. Run them only while no viewers are connected.

### Startup refresh

```bash
./soralink \
  --device soralink.local \
  --server \
  --device-channels-update \
  --device-epg-update
```

`--device-update-on-start` is enabled by default.

Channel startup behaviour:

- When the local channel file loads, a failed native refresh produces a warning and preserves the local lineup.
- When the local channel file is missing, enabled startup channel refresh can create the lineup.
- If neither source succeeds, server startup fails.

EPG startup behaviour:

- A failed native EPG refresh produces a warning.
- The already loaded local XMLTV data remains available.

Disable startup maintenance while retaining manual and scheduled controls:

```bash
./soralink --config soralink.conf --no-device-update-on-start
```

### Periodic maintenance

```bash
./soralink \
  --device soralink.local \
  --server \
  --device-channels-update \
  --device-epg-update \
  --device-channels-refresh-minutes 1440 \
  --device-epg-refresh-minutes 240
```

An interval of `0` disables that periodic task. Channel and EPG refreshes also require their corresponding update feature to be enabled.

Default intervals:

| Task | Default |
|---|---:|
| Channel refresh | `1440` minutes |
| EPG refresh | `240` minutes |
| Scheduled scan | Disabled (`0`) |

### Receiver scans

Manual scans are available from the dashboard. Scheduled scans use a non-zero interval:

```bash
./soralink \
  --device soralink.local \
  --server \
  --device-scan-refresh-minutes 10080 \
  --device-scan-timeout-minutes 45 \
  --device-scan-mode 110 \
  --device-scan-search-range 0 \
  --device-scan-order-by 0 \
  --device-scan-sort-mode 0 \
  --device-scan-network \
  --device-scan-epg-after
```

Scan modes:

| Value | Behaviour |
|---:|---|
| `110` | Default full mode. At `19.2°E`, runs the built-in 88-transponder Astra software sweep; elsewhere starts the receiver automatic scan. |
| `0` | Compatibility mode using the receiver automatic scan path without the Astra software sweep. |
| `126` | Accepted as a legacy alias and normalized to `110`. |

By default, scans use the satellite/LNB profile already stored in the receiver. Add `--device-scan-apply-satellite` to apply SORALink's configured orbital, LNB, tone, and DiSEqC values before scanning.

After a successful scan, SORALink downloads and installs the refreshed lineup. Native EPG refresh after scanning is enabled by default.

## Configuration file

Load settings from a `key=value` file:

```bash
./soralink --config soralink.conf
```

The configuration file is loaded first. Command-line options are then applied and take precedence.

```ini
# Receiver and server
device=soralink.local
control_port=8802
stream_port=8800
server=true
channels=channels.xml
http_bind=127.0.0.1
http_port=8080
max_clients=8

# Dashboard and authentication
webui=true
web_root=web
http_user=vlc
http_password=replace-this-viewer-password
admin_user=admin
admin_password=replace-this-admin-password

# XMLTV
epg=epg.xml
epg_update=false
epg_url=https://example.test/guide.xml
epg_update_timeout_ms=30000

# Native receiver maintenance
device_channels_update=false
device_epg_update=false
device_update_on_start=true
device_channels_refresh_minutes=1440
device_epg_refresh_minutes=240

# Receiver scanning
device_scan_refresh_minutes=0
device_scan_timeout_minutes=45
device_scan_mode=110
device_scan_search_range=0
device_scan_order_by=0
device_scan_sort_mode=0
device_scan_network=true
device_scan_epg_after=true
device_scan_apply_satellite=false

# Satellite/LNB profile
orbital=192
west=false
tone=auto
lnb_low=9750
lnb_high=10600
lnb_switch=11700
diseqc=0
sat_setup=false

# Stream processing
wait_ms=800
timeout_ms=3000
missing_key=pass
```

Configuration rules:

- Empty lines are ignored.
- Lines beginning with `#` or `;` are comments.
- Lines beginning with `[` are ignored, allowing visual INI-style sections.
- Keys are case-normalized.
- Hyphens in keys are treated as underscores.
- Values may be wrapped in matching single or double quotes.
- Unknown or invalid settings stop startup.
- Credentials are stored as plaintext; protect the file with filesystem permissions.
- Dashboard persistence is available only when SORALink was started with `--config`.

The dashboard can persist these live settings:

- Viewer limit and stream timeouts
- Missing-key policy
- Native update enables and intervals
- Scan interval, timeout, mode, range, ordering, sorting, network, EPG-after, and satellite-profile controls
- Orbital direction and position
- Tone, LNB, and DiSEqC values
- Startup maintenance enable

## Single-channel UDP mode

UDP mode forwards MPEG-TS to `127.0.0.1:1234` by default.

Open this network URL in VLC first:

```text
udp://@:1234
```

### Tune a saved programme

```bash
./soralink \
  --device soralink.local \
  --progidx 0
```

### Tune a DVB-S service directly

```bash
./soralink \
  --device soralink.local \
  --freq 11494 \
  --sr 22000 \
  --pol H \
  --sid 10301 \
  --orbital 192
```

Direct tuning requires all four of `--freq`, `--sr`, `--pol`, and `--sid`.

`--orbital 192` means `19.2°`. East is the default; add `--west` for a western orbital position.

### Forward to another host

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --vlc-ip media-pc.local \
  --vlc-port 1234
```

The destination can be a DNS name, IPv4 address, or IPv6 address and must accept incoming UDP traffic.

### Record the processed stream

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --dump recording.ts
```

The dump contains the same processed MPEG-TS packets sent to VLC.

## Stream keys and encrypted packets

SORALink enables a legacy built-in even AES-128 key by default. It can be replaced, supplemented with an odd key, or disabled.

Keys must contain exactly 16 bytes. Plain hexadecimal and separators such as colons, hyphens, spaces, and tabs are accepted.

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --even-key 00112233445566778899aabbccddeeff \
  --odd-key 00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff
```

Disable the built-in even key:

```bash
./soralink --device soralink.local --progidx 0 --no-default-key
```

Missing-key policy:

| Mode | Behaviour |
|---|---|
| `pass` | Forward the encrypted packet unchanged and preserve its scrambling marker; default |
| `drop` | Discard the encrypted packet |

Use keys only with hardware, services, and content that you are authorized to access.

## Satellite and LNB settings

Normal tuning skips the extended LNB/DiSEqC setup sequence by default. Enable it when the receiver must be configured explicitly:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --sat-setup \
  --lnb-low 9750 \
  --lnb-high 10600 \
  --lnb-switch 11700 \
  --diseqc 1
```

Defaults:

| Setting | Default |
|---|---:|
| Orbital position | `19.2°E` (`192`) |
| Low LNB oscillator | `9750 MHz` |
| High LNB oscillator | `10600 MHz` |
| LNB switch frequency | `11700 MHz` |
| DiSEqC input | `0` (none) |
| 22 kHz tone | `auto` |
| Extended satellite setup | Disabled |

With `--tone auto`, the tone is selected from the tuned frequency and configured switch frequency.

## Network access and security

The server listens on `127.0.0.1` by default.

To expose it on a trusted local network:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0 \
  --http-user vlc \
  --http-password 'change-this-viewer-password' \
  --admin-user admin \
  --admin-password 'change-this-admin-password'
```

Open the playlist using the SORALink host's LAN address:

```text
http://192.168.1.20:8080/playlist.m3u
```

The listener can also bind to a hostname or IPv6 address. `*` requests a wildcard listener through the platform resolver.

### Viewer authentication

Both options are required together:

```bash
--http-user vlc --http-password 'change-this-viewer-password'
```

Viewer authentication protects playlists, status, and channel streams.

### Administrator authentication

Both options are required together:

```bash
--admin-user admin --admin-password 'change-this-admin-password'
```

Without a dedicated administrator pair, the dashboard uses the viewer credentials.

### HTTPS

TLS requires an OpenSSL build and both PEM files:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0 \
  --http-user vlc \
  --http-password 'change-this-viewer-password' \
  --admin-user admin \
  --admin-password 'change-this-admin-password' \
  --tls-cert server-chain.pem \
  --tls-key server-key.pem
```

The native server enforces TLS 1.2 or newer, disables TLS compression, loads a certificate chain, and verifies that the private key matches.

> [!WARNING]
> HTTP Basic authentication does not encrypt credentials. Exposing the server beyond loopback can reveal credentials, viewer addresses, configuration details, channel activity, and stream content. Use TLS, a trusted TLS reverse proxy, and firewall rules.

## Status APIs

### `/status.json`

Idle response:

```json
{
  "streaming": false,
  "clients": 0
}
```

While streaming, it also includes:

- Logical channel number and name
- Frequency, symbol rate, polarization, and service ID
- Even/odd decrypted packet counters
- Missing even/odd key counters

### `/admin/api/status`

The authenticated dashboard API includes:

- Server time and uptime
- Current transfer rate and total bytes
- Current channel
- Every configured channel with stream URL and now/next data
- Viewer IDs, addresses, ports, and connection durations
- Active paths and selected settings
- XMLTV load state
- Native update state
- Scan state, progress, frequency, mode, and result counts

### `/admin/api/epg/<lcn>`

Returns up to 100 programmes around the current time for one channel.

## Command-line reference

### Configuration and receiver

| Option | Description | Default |
|---|---|---|
| `--config FILE` | Load `key=value` settings before command-line overrides | Off |
| `--device HOST` | Receiver DNS name, IPv4 address, or IPv6 address; required except for `--self-test` | — |
| `--control-port N` | Receiver TCP control port | `8802` |
| `--stream-port N` | Receiver UDP stream port | `8800` |

### HTTP, dashboard, and XMLTV

| Option | Description | Default |
|---|---|---|
| `--server`, `--http-server` | Run the HTTP/HTTPS playlist and stream server | Off |
| `--channels FILE` | Channel XML file | `channels.xml` |
| `--http-bind HOST` | Listen address or hostname | `127.0.0.1` |
| `--http-port N` | HTTP listen port | `8080` |
| `--max-clients N` | Simultaneous viewers of one channel; `1` to `64` | `8` |
| `--http-user USER` | Viewer HTTP Basic username; requires password | Off |
| `--http-password PASS` | Viewer HTTP Basic password; requires username | Off |
| `--admin-user USER` | Administrator username; requires password | Viewer credentials |
| `--admin-password PASS` | Administrator password; requires username | Viewer credentials |
| `--webui` | Enable the administration dashboard | Enabled |
| `--no-webui` | Disable the administration dashboard | Off |
| `--web-root DIR` | Dashboard assets directory | `web` beside executable |
| `--epg FILE` | XMLTV file; an empty config value disables it | `epg.xml` beside executable |
| `--epg-update` | Download XMLTV before server startup and full/EPG reloads | Off |
| `--no-epg-update` | Disable XMLTV download | Enabled |
| `--epg-url URL` | HTTP/HTTPS source for `--epg-update` | — |
| `--epg-update-timeout-ms N` | XMLTV download timeout; `1000` to `120000` ms | `30000` |
| `--tls-cert FILE` | TLS certificate chain PEM; requires `--tls-key` and OpenSSL | Off |
| `--tls-key FILE` | TLS private-key PEM; requires `--tls-cert` and OpenSSL | Off |

### Native maintenance and scans

| Option | Description | Default |
|---|---|---|
| `--device-channels-update` | Enable native lineup refresh | Off |
| `--no-device-channels-update` | Disable native lineup refresh | Enabled |
| `--device-epg-update` | Enable native EPG refresh | Off |
| `--no-device-epg-update` | Disable native EPG refresh | Enabled |
| `--device-channels-refresh-minutes N` | Lineup refresh interval; `0` disables; maximum `10080` | `1440` |
| `--device-epg-refresh-minutes N` | EPG refresh interval; `0` disables; maximum `10080` | `240` |
| `--device-scan-refresh-minutes N` | Scan interval; `0` disables; maximum `10080` | `0` |
| `--device-scan-timeout-minutes N` | Scan timeout; `1` to `120` minutes | `45` |
| `--device-scan-mode N` | `110` full/default, `0` compatibility; `126` legacy alias for `110` | `110` |
| `--device-scan-search-range N` | Receiver-specific scan range code; `0` to `7` | `0` |
| `--device-scan-order-by N` | Receiver-specific ordering code; `0` to `3` | `0` |
| `--device-scan-sort-mode N` | Receiver-specific sorting code; `0` to `3` | `0` |
| `--device-scan-network` | Enable network scanning | Enabled |
| `--no-device-scan-network` | Disable network scanning | Off |
| `--device-scan-epg-after` | Refresh native EPG after a completed scan | Enabled |
| `--no-device-scan-epg-after` | Skip EPG refresh after a scan | Off |
| `--device-scan-apply-satellite` | Apply configured satellite/LNB/DiSEqC profile before scanning | Off |
| `--no-device-scan-apply-satellite` | Use the receiver's stored satellite profile | Enabled |
| `--device-update-on-start` | Run enabled native updates during startup | Enabled |
| `--no-device-update-on-start` | Skip enabled native updates during startup | Off |

### Tuning and satellite

| Option | Description | Default |
|---|---|---|
| `--progidx N` | Tune saved programme index `N` | — |
| `--freq MHz` | Transponder frequency in MHz | — |
| `--sr N` | Symbol rate in kSym/s | — |
| `--pol H\|V\|F\|O` | Polarization or receiver-specific voltage mode | — |
| `--sid N` | DVB service ID | — |
| `--orbital N` | Orbital position in tenths of a degree | `192` |
| `--east` | Use an eastern orbital position | Enabled |
| `--west` | Use a western orbital position | Off |
| `--tone auto\|on\|off` | 22 kHz tone mode | `auto` |
| `--lnb-low N` | Low-band LNB local oscillator in MHz; maximum `65535` | `9750` |
| `--lnb-high N` | High-band LNB local oscillator in MHz; maximum `65535` | `10600` |
| `--lnb-switch N` | LNB switch frequency in MHz; maximum `65535` | `11700` |
| `--diseqc N` | DiSEqC input value; `0` to `255` | `0` |
| `--sat-setup` | Send the extended LNB/DiSEqC setup sequence | Off |
| `--no-sat-setup` | Skip the extended setup sequence | Enabled |

### Stream processing and UDP output

| Option | Description | Default |
|---|---|---|
| `--even-key HEX` | Set the 16-byte even AES key | Legacy built-in key |
| `--odd-key HEX` | Set the 16-byte odd AES key | Unset |
| `--no-default-key` | Disable the legacy built-in even key | Off |
| `--missing-key pass\|drop` | Forward or discard packets without a matching key | `pass` |
| `--vlc-ip HOST` | UDP destination DNS name, IPv4 address, or IPv6 address | `127.0.0.1` |
| `--vlc-port N` | UDP destination port | `1234` |
| `--dump FILE.ts` | Also write the processed stream to a file in UDP mode | Off |
| `--wait-ms N` | Delay after tuning; `0` to `60000` ms | `800` |
| `--timeout-ms N` | Network timeout; `100` to `60000` ms | `3000` |

### Diagnostics

| Option | Description | Default |
|---|---|---|
| `--probe` | Test only the receiver permission handshake | Off |
| `--self-test` | Run internal tests without connecting to a receiver | Off |
| `--verbose` | Print protocol frames and diagnostics | Off |
| `--help`, `-h` | Show built-in help | — |

## Recovery behaviour

SORALink sends a control heartbeat approximately every five seconds.

If the control connection fails or the MPEG-TS transport repeatedly times out, SORALink attempts to:

1. Reconnect to the receiver.
2. Reacquire the exclusive permission session.
3. Reapply satellite setup when enabled.
4. Retune the active channel.

Native EPG retrieval and scan polling also attempt to restore control sessions when required. A failed recovery ends the affected run or disconnects affected viewers rather than continuing with an unknown receiver state.

## Troubleshooting

### Control port is unreachable

- Confirm the receiver hostname or IP address.
- Confirm `--control-port` when it does not use TCP `8802`.
- Verify network reachability and firewall rules.
- Fully close the official receiver application and other clients.
- When a hostname resolves to several addresses, test a literal working address.

### Receiver is occupied or refuses permission

SORALink needs an exclusive control session. Force-stop or completely exit the official application on every phone, tablet, and computer that may still be connected.

### No signal lock

- Verify frequency, symbol rate, polarization, and service ID.
- Confirm orbital position and east/west direction.
- Check dish signal and cabling.
- Verify LNB oscillator and switch values.
- Select the correct DiSEqC input.
- Try `--sat-setup` when explicit setup is required.

### Repeated stream timeouts or recovery attempts

- Allow UDP traffic to and from the receiver stream port.
- Confirm tuning completed with signal lock.
- Check host and network firewalls.
- Confirm `--stream-port` when it does not use UDP `8800`.
- Increase `--timeout-ms` on slow or unstable networks.
- Run with `--verbose` to inspect protocol activity.

### VLC opens the playlist but cannot play a channel

- Check the tuning response with `--verbose`.
- Verify `lcn`, `freq`, `sym`, `pol`, and `s_id` in the channel entry.
- Open `/channel/<lcn>` directly in VLC.
- Check `/status.json`.
- Confirm no update or scan is active.
- When authentication is enabled, ensure VLC sends credentials for both playlist and stream URLs.
- With TLS, ensure VLC trusts the server certificate.

### Dashboard asset is missing

- Confirm all documented files exist in the configured `web/` directory.
- Use `--web-root DIR` when assets are elsewhere.
- Use `--no-webui` when the dashboard is not required.

### Dashboard cannot bind to a LAN address

An enabled dashboard cannot be exposed beyond loopback without credentials. Configure either a dedicated administrator pair or the viewer pair.

### EPG is empty or does not match channels

- Confirm `--epg FILE` points to readable XMLTV data.
- Match `epg_id`/`xmltv_id` to the XMLTV channel ID.
- Reload EPG after replacing the file.
- For native EPG, confirm relevant channels contain a valid `prog_idx`.

### XMLTV download fails

- Supply both `--epg-update` and `--epg-url`.
- Use an uncompressed XML endpoint.
- Increase `--epg-update-timeout-ms` for a slow server.
- Build with OpenSSL for HTTPS.
- Confirm the operating system/OpenSSL installation has a usable CA trust store.
- Replace unsupported relative redirects with a direct or absolute URL.

### Native refresh fails

- Disconnect all viewers.
- Confirm the receiver firmware supports the implemented commands.
- Use `--verbose` to inspect protocol traffic.
- Keep known-good local `channels.xml` and `epg.xml` files as fallbacks.

### Scan does not finish

- Increase `--device-scan-timeout-minutes` within `1` to `120`.
- Check progress in the dashboard.
- Cancel before retrying with different receiver-specific range/order/sort values.
- Use compatibility mode `--device-scan-mode 0` when the default mode is unsuitable.
- Confirm the official receiver application has not reclaimed the session.

### Different channel returns HTTP 409

Another viewer is using the currently tuned channel. Close all clients on that stream or disconnect them through the dashboard before selecting a new channel.

### Client limit is reached

Increase `--max-clients` up to `64`, or disconnect inactive viewers.

### HTTP bind fails

Another process may already use the address or port:

```bash
./soralink --device soralink.local --server --http-port 8090
```

For IPv6 URLs, use brackets, for example:

```text
http://[::1]:8080/playlist.m3u
```

### TLS support is unavailable

Rebuild with `-DSORALINK_USE_OPENSSL` and link `libssl` and `libcrypto`. Supply both `--tls-cert` and `--tls-key`.

### Encrypted channels remain scrambled

- Determine whether the packets use the even or odd scrambling state.
- Supply the corresponding key.
- Check AES counters in `/status.json`, `/admin/api/status`, or console output.
- Remember that `--missing-key pass` deliberately preserves packets that cannot be decrypted.

## Limitations

- The receiver can tune one HTTP channel at a time, although many viewers may share it.
- The dashboard assets are external and are not embedded in the executable.
- The HTTP service is purpose-built for SORALink and is not a general-purpose web server.
- Authentication is HTTP Basic with separate viewer/administrator credentials, not a role-management system.
- Native TLS and HTTPS XMLTV downloads require an OpenSSL build.
- Channel XML and XMLTV parsing intentionally support only the documented subset.
- The XMLTV downloader expects uncompressed content and does not support arbitrary relative redirects.
- Native lineup, EPG, and scan commands depend on receiver implementation and firmware.
- The built-in Astra sweep is specifically defined for `19.2°E`.
- `F` and `O` polarization/voltage modes are receiver-specific.
- The legacy built-in key and protocol framing are implementation-specific.
- Hardware compatibility depends on the target receiver and firmware.

## Stopping SORALink

Press `Ctrl+C`. SORALink handles `SIGINT`/`SIGTERM` on POSIX and the equivalent console events on Windows, closes sockets, releases TLS resources when enabled, and exits.

## Disclaimer

Use SORALink only with hardware, services, keys, and content that you are authorized to access. This is an independent interoperability project and is not presented as an official application from the receiver manufacturer or VLC.

# SORALink (ソラリンク)

![SORALink](soralink.jpg)

SORALink is a compact C bridge that connects a compatible network satellite tuner/dongle to [VLC media player](https://www.videolan.org/vlc/).

It acquires the device's exclusive control session, tunes a saved programme or DVB service, receives MPEG transport-stream data, validates and processes the packets, and forwards the resulting stream to VLC.

The recommended mode runs a local HTTP or HTTPS server. It provides VLC-compatible M3U playlists, shared MPEG-TS streaming, an optional browser administration dashboard, XMLTV programme-guide support, and device-maintenance controls.

The streaming core builds as one executable with no required third-party runtime library. OpenSSL is optional for TLS and HTTPS EPG downloads. The browser dashboard uses static files from a separate `web/` directory.

## Highlights in this update

- Browser administration dashboard at `/admin/`
- Live channel, stream, throughput, viewer, and now/next EPG information
- Viewer disconnection, channel/EPG reload, and live settings controls
- Separate viewer and administrator HTTP Basic credentials
- Local XMLTV loading and optional HTTP/HTTPS EPG download at startup
- Native device channel-list and EPG refresh
- Manual or scheduled receiver channel scans with progress reporting
- `key=value` configuration files with command-line overrides
- Optional persistence of supported live dashboard settings to the active config file
- Channel XML support for `epg_id`, `xmltv_id`, `prog_idx`, and `ts_id`
- Expanded self-tests for XMLTV, native EPG, EPG downloads, receiver scanning, and LNB tone selection

## Features

- HTTP playlists for TV, radio, or all channels
- Channel selection directly from VLC
- Up to 64 viewers sharing the same tuned HTTP channel; default limit: 8
- Browser administration dashboard with live JSON APIs
- XMLTV programme-guide loading, now/next display, and per-channel schedules
- Optional XMLTV download over HTTP or HTTPS
- Native device lineup and EPG refresh
- Manual and scheduled receiver scanning
- Direct tuning by frequency, symbol rate, polarization, and service ID
- Tuning by the device's saved programme index
- UDP MPEG-TS output for VLC
- Optional `.ts` recording in UDP mode
- Universal-LNB and DiSEqC configuration
- Device permission handshake and periodic control heartbeat
- Automatic control reconnection and channel retuning
- MPEG-TS alignment, validation, AES-128 processing, and removal of PID `0x1FFE`
- Configurable even and odd AES-128 keys
- Configurable pass/drop policy for encrypted packets without a matching key
- Viewer and administrator HTTP Basic authentication
- Optional TLS 1.2+ with OpenSSL
- DNS, IPv4, and IPv6 support for the device, listener, UDP destination, and EPG source
- Cross-platform socket support for Windows and POSIX systems
- Built-in protocol and data-processing self-tests

## How it works

```text
SORALink device
  ├─ TCP 8802: control, permission, tuning, heartbeat, maintenance
  └─ UDP 8800: MPEG-TS block requests and replies
                    │
                    ▼
                 soralink
                  ├─ HTTP/HTTPS playlists and MPEG-TS streams → VLC clients
                  ├─ Administration dashboard and JSON APIs   → Web browser
                  ├─ XMLTV / native receiver EPG              → Programme guide
                  └─ UDP MPEG-TS forwarding                   → VLC
```

The device ports are configurable with `--control-port` and `--stream-port`.

SORALink uses an exclusive device control session. Fully close any official device application or other client before starting it.

## Requirements

- A compatible network tuner/dongle reachable by DNS name, IPv4, or IPv6
- Network access to the device's TCP control port and UDP stream port
- VLC media player
- A C11 compiler
- A `channels.xml` file for normal playlist-server startup, unless native channel refresh is enabled for startup recovery
- The `web/` assets directory when the administration dashboard is enabled
- An optional XMLTV file for programme-guide data
- OpenSSL development files only for native TLS or HTTPS EPG downloads

Default device ports:

- TCP control: `8802`
- UDP stream: `8800`

## Suggested repository layout

```text
.
├── soralink.c
├── README.md
├── channels.xml
├── epg.xml                 # optional XMLTV file
├── soralink.conf           # optional configuration file
├── soralink.jpg
└── web/                    # required by the enabled-by-default dashboard
    ├── index.html
    ├── styles.css
    ├── app.js
    └── ...
```

Use `--web-root DIR` and `--epg FILE` when these files live elsewhere.

## Build

Rename the source file to `soralink.c`, then compile it with one of the following commands.

### Linux, macOS, or another POSIX system

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

The default build uses the built-in AES, HTTP, XML, XMLTV, and socket implementations.

### Optional OpenSSL/TLS build

Define `SORALINK_USE_OPENSSL` and link OpenSSL when either of these is required:

- HTTPS for the SORALink server
- HTTPS XMLTV downloads through `--epg-url`

On a POSIX system with `pkg-config`:

```bash
cc -std=c11 -O2 -Wall -Wextra -pedantic \
  -DSORALINK_USE_OPENSSL \
  soralink.c -o soralink \
  $(pkg-config --cflags --libs openssl)
```

With MinGW-w64, after configuring the OpenSSL include and library paths:

```powershell
gcc -std=c11 -O2 -Wall -Wextra -DSORALINK_USE_OPENSSL `
  soralink.c -o soralink.exe -lws2_32 -lssl -lcrypto
```

With Microsoft Visual C++, after configuring the OpenSSL include and library paths:

```powershell
cl /O2 /W4 /DSORALINK_USE_OPENSSL soralink.c ws2_32.lib libssl.lib libcrypto.lib
```

OpenSSL library filenames and setup can vary between distributions.

## Run the self-tests

The self-test does not require a tuner:

```bash
./soralink --self-test
```

The current test suite covers:

- AES-128 known-answer decryption and key parsing
- Even/odd MPEG-TS decryption and missing-key policies
- Multiline channel XML, EPG IDs, programme indexes, transport-stream IDs, and entities
- XMLTV timestamp and programme parsing
- Native receiver EPG XML and MJD conversion
- HTTP Basic-auth encoding
- HTTP/HTTPS EPG URL parsing and chunked-body decoding
- Receiver scan command framing
- Protocol response validation
- Automatic LNB tone selection

## Quick start

### 1. Test the device connection

```bash
./soralink --device soralink.local --probe
```

A successful probe confirms that the TCP control port is reachable and that SORALink can claim the device session.

Use custom ports when required:

```bash
./soralink \
  --device soralink.local \
  --control-port 8802 \
  --stream-port 8800 \
  --probe
```

### 2. Start the playlist server

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml
```

Open this URL in VLC:

```text
http://127.0.0.1:8080/playlist.m3u
```

### 3. Open the administration dashboard

The dashboard is enabled by default:

```text
http://127.0.0.1:8080/admin/
```

The default web-assets directory is `web` beside the executable. Override it with `--web-root DIR`, or disable the dashboard with `--no-webui`.

## Server endpoints

### Media endpoints

| Endpoint | Description |
|---|---|
| `/` or `/playlist.m3u` | TV and radio playlist |
| `/tv.m3u` | TV-only playlist |
| `/radio.m3u` | Radio-only playlist |
| `/status.json` | Current stream, viewer, channel, and AES statistics |
| `/channel/<lcn>` | MPEG-TS stream for a logical channel number |

Media endpoints support `GET`, `HEAD`, and `OPTIONS`.

### Administration endpoints

| Endpoint | Method | Description |
|---|---:|---|
| `/admin/` | `GET` | Administration dashboard |
| `/admin/api/status` | `GET` | Full dashboard state, channels, viewers, EPG summaries, and maintenance state |
| `/admin/api/epg/<lcn>` | `GET` | Programme schedule for one logical channel number |
| `/admin/kick` | `POST` | Disconnect one viewer |
| `/admin/kick-all` | `POST` | Disconnect every viewer |
| `/admin/reload` | `POST` | Reload channel data and EPG data |
| `/admin/reload-epg` | `POST` | Reload only EPG data |
| `/admin/settings` | `POST` | Apply supported live settings and optionally persist them |
| `/admin/device/update-channels` | `POST` | Refresh the lineup from the receiver |
| `/admin/device/update-epg` | `POST` | Refresh XMLTV data from the receiver EPG cache |
| `/admin/device/update-all` | `POST` | Refresh lineup and EPG data |
| `/admin/device/scan` | `POST` | Start a receiver channel scan |
| `/admin/device/scan-cancel` | `POST` | Cancel an active receiver scan |

Administration endpoints support `GET`, `HEAD`, `POST`, and `OPTIONS` as appropriate. Mutating actions are accepted only through `POST`.

## Multi-client behavior

The first viewer tunes the requested channel. Additional viewers may join that same channel until `--max-clients` is reached.

Because the hardware has one active tuner path, a request for another channel returns HTTP `409 Conflict` while the current channel still has viewers. After every viewer disconnects, the next channel request may retune the device.

Maintenance actions and scans require an idle tuner. New stream requests receive HTTP `503 Service Unavailable` while receiver maintenance is active.

## Channel list format

The playlist server reads `<ch ...>` elements from an XML file. Elements may span multiple lines, and attributes may use single or double quotes.

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

### Channel attributes

| Attribute | Required | Description |
|---|---:|---|
| `type` | Yes | `TV` or `RADIO`; values are normalized to uppercase for filtering. |
| `pol` | Yes | `H`, `V`, `F`, or `O`; `F` and `O` are device-specific voltage modes. |
| `sym` | Yes | Symbol rate in kSym/s, such as `22000` or `27500`. |
| `s_id` | Yes | DVB service ID. |
| `freq` | Yes | Transponder frequency in MHz. |
| `lcn` | Yes | Logical channel number used in playlists and `/channel/<lcn>`. |
| `s_name` | Yes | Display name shown in VLC and the dashboard. |
| `fta` | No | Free-to-air flag, parsed as `0` or non-zero. |
| `epg_id` | No | XMLTV channel ID. Defaults to the decimal `lcn`. |
| `xmltv_id` | No | Alias for `epg_id`. |
| `prog_idx` | No | Saved receiver programme index used by native EPG retrieval. |
| `ts_id` | No | DVB transport-stream ID used by native lineup/EPG data. |

Use a unique `lcn` for every channel. Duplicate logical channel numbers are skipped with a warning.

The parser decodes `&amp;`, `&apos;`, `&quot;`, `&lt;`, `&gt;`, decimal numeric entities such as `&#9733;`, and hexadecimal numeric entities such as `&#x2605;`.

This is a deliberately lightweight parser rather than a general-purpose XML implementation. Attribute names must match the documented names, malformed entries are skipped, individual tags are limited to 64 KiB, and the complete file is limited to 64 MiB.

## Programme guide and XMLTV

SORALink loads an XMLTV file for dashboard now/next information and per-channel schedules.

The default path is `epg.xml` beside the executable:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --epg guide.xml
```

Each channel's `epg_id` or `xmltv_id` must match the corresponding XMLTV `<channel id="...">` and `<programme channel="...">` value. When neither attribute is present, SORALink uses the channel's decimal `lcn` as its EPG ID.

The parser reads programme start/stop times and the common `title`, `sub-title`, `desc`, and `category` elements.

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

Notes:

- `--epg-update` requires `--epg-url`.
- HTTP downloads work in the default build.
- HTTPS downloads require an OpenSSL build and verify the server certificate and hostname.
- The downloader accepts normal or chunked HTTP bodies, follows supported absolute redirects, and expects uncompressed XML text.
- A failed startup download leaves the existing local EPG file available for loading when possible.
- Use `--epg-update-timeout-ms N` to change the default `30000` ms timeout.

## Administration dashboard

The dashboard is enabled by default in server mode and serves static files from `web/`.

It provides:

- Current channel and stream throughput
- Connected-viewer details and connection durations
- One-viewer or all-viewer disconnection controls
- TV/radio channel browsing and stream links
- Now/next programme information and channel schedules
- Channel-list and EPG reload controls
- Native receiver lineup and EPG refresh controls
- Receiver scan start, cancellation, progress, and result counts
- Selected live server and maintenance settings
- Optional persistence of supported settings when SORALink was started with `--config`

Disable it when only VLC endpoints are required:

```bash
./soralink --device soralink.local --server --no-webui
```

### Dashboard authentication

Use dedicated administrator credentials:

```bash
./soralink \
  --device soralink.local \
  --server \
  --admin-user admin \
  --admin-password 'change-this-password'
```

When dedicated administrator credentials are not configured, the dashboard falls back to the viewer credentials from `--http-user` and `--http-password`.

For safety, SORALink refuses to expose the enabled dashboard on a non-loopback listener unless either administrator or viewer credentials are configured.

## Native device updates and scanning

Compatible receivers can provide their saved lineup and cached EPG data directly to SORALink.

### Refresh channels and EPG at startup

```bash
./soralink \
  --device soralink.local \
  --server \
  --device-channels-update \
  --device-epg-update
```

`--device-update-on-start` is enabled by default. Disable startup maintenance while keeping scheduled/manual functionality with `--no-device-update-on-start`.

When startup channel refresh is enabled, SORALink can use the receiver lineup if the local channel file is unavailable. When an existing channel file is present and the receiver refresh fails, SORALink continues with the local lineup.

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

The interval value `0` disables that periodic task. The default interval values are one day for channels and four hours for EPG, but the corresponding update feature must be enabled.

### Receiver scans

Manual scans are available from the dashboard. Scheduled scans are enabled by setting a non-zero interval:

```bash
./soralink \
  --device soralink.local \
  --server \
  --device-scan-refresh-minutes 10080 \
  --device-scan-timeout-minutes 30 \
  --device-scan-search-range 0 \
  --device-scan-order-by 0 \
  --device-scan-network \
  --device-scan-epg-after
```

A completed scan downloads the refreshed lineup. By default, SORALink also refreshes EPG data afterward.

Receiver maintenance is device- and firmware-specific. Run it only when no viewers are connected.

## Configuration file

Load settings from a `key=value` file:

```bash
./soralink --config soralink.conf
```

Command-line options are processed after the file and therefore take precedence.

Example:

```ini
# Core device and server
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

# Programme guide
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
device_scan_refresh_minutes=0
device_scan_timeout_minutes=30
device_scan_search_range=0
device_scan_order_by=0
device_scan_network=true
device_scan_epg_after=true

# Streaming behavior
wait_ms=800
timeout_ms=3000
missing_key=pass
```

Configuration notes:

- Empty lines and lines beginning with `#` or `;` are ignored.
- Section-header lines beginning with `[` are ignored, so INI-style grouping is permitted.
- Keys are case-normalized, and hyphens are treated like underscores.
- Values may be wrapped in matching single or double quotes.
- Unknown or invalid settings stop startup with an error.
- Credentials are stored as plaintext; protect the config file with appropriate filesystem permissions.
- Dashboard persistence is available only when an active config file was supplied.

## Single-channel UDP mode

In UDP mode, SORALink forwards MPEG-TS packets to `127.0.0.1:1234` by default.

First open this network URL in VLC:

```text
udp://@:1234
```

Then use one of the tuning modes below.

### Tune a saved programme index

```bash
./soralink \
  --device soralink.local \
  --progidx 0
```

### Tune a service directly

Replace the example values with the service parameters for your channel:

```bash
./soralink \
  --device soralink.local \
  --freq 11494 \
  --sr 22000 \
  --pol H \
  --sid 10301 \
  --orbital 192
```

`--orbital 192` means `19.2°`. East is the default; add `--west` for a western orbital position.

### Send the stream to another VLC host

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --vlc-ip media-pc.local \
  --vlc-port 1234
```

The destination may be a DNS name, IPv4 address, or IPv6 address and must permit incoming UDP traffic on the selected port.

### Save a transport-stream dump

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --dump recording.ts
```

The dump option is available in UDP mode. The same processed MPEG-TS packets sent to VLC are written to the file.

## Stream keys and encrypted packets

SORALink has a legacy built-in even AES-128 key enabled by default. You can replace it, add an odd key, or disable it.

Keys must contain exactly 16 bytes. Plain hexadecimal and separators such as colons, hyphens, spaces, or tabs are accepted.

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --even-key 00112233445566778899aabbccddeeff \
  --odd-key 00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff
```

Disable the built-in even key:

```bash
./soralink \
  --device soralink.local \
  --progidx 0 \
  --no-default-key
```

When a packet is marked as encrypted but its matching key is unavailable:

- `--missing-key pass` forwards the packet unchanged and preserves its scrambling marker. This is the default.
- `--missing-key drop` discards the packet.

Use keys only with hardware, services, and content that you are authorized to access.

## Satellite and LNB configuration

By default, SORALink uses the device's normal tuning path and skips the extended LNB/DiSEqC setup sequence.

Enable the extended setup when your installation requires SORALink to configure these values explicitly:

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

Universal-LNB defaults:

- Low local oscillator: `9750 MHz`
- High local oscillator: `10600 MHz`
- Switch frequency: `11700 MHz`
- DiSEqC value: `0` (none)
- Orbital position: `19.2°E`
- 22 kHz tone: automatic

Use `--tone on` or `--tone off` only when automatic tone selection is unsuitable.

## HTTP server access and security

The server listens on `127.0.0.1` by default and is therefore accessible only from the same computer.

To make it available on the local network:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0 \
  --http-user vlc \
  --http-password 'change-this-password' \
  --admin-user admin \
  --admin-password 'change-this-admin-password'
```

Open the playlist using the SORALink computer's LAN address, for example:

```text
http://192.168.1.20:8080/playlist.m3u
```

You may also bind to a hostname or IPv6 address. Use `*` to request a wildcard listener through the platform resolver.

### Viewer authentication

Both viewer options must be supplied together:

```bash
--http-user vlc --http-password 'change-this-password'
```

Viewer authentication applies to playlists, status, and channel streams.

### Administrator authentication

Both administrator options must be supplied together:

```bash
--admin-user admin --admin-password 'change-this-admin-password'
```

When administrator credentials are omitted, the dashboard uses the viewer credentials. When neither credential pair is configured, the enabled dashboard may run only on loopback.

HTTP Basic authentication does not encrypt credentials. Use it with TLS or only on a trusted network.

### HTTPS with OpenSSL

TLS requires a binary built with `SORALINK_USE_OPENSSL` and a PEM certificate/private-key pair:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0 \
  --http-user vlc \
  --http-password 'change-this-password' \
  --admin-user admin \
  --admin-password 'change-this-admin-password' \
  --tls-cert server-chain.pem \
  --tls-key server-key.pem
```

The built-in TLS server requires TLS 1.2 or newer and disables TLS compression.

> [!WARNING]
> Exposing a stream or administration interface outside loopback can reveal channel activity, credentials, configuration details, viewer addresses, and stream content. Use a firewall, authentication, and TLS or a trusted TLS reverse proxy.

## Status APIs

### Basic stream status

Request `/status.json` for a compact public/viewer status response.

When idle:

```json
{
  "streaming": false,
  "clients": 0
}
```

While streaming, the response includes the current channel parameters plus even-key, odd-key, and missing-key counters.

### Dashboard status

`/admin/api/status` returns a broader authenticated response containing:

- Server uptime and transfer statistics
- Current channel
- Every configured channel with now/next EPG data
- Connected viewer identifiers and addresses
- Active configuration paths and selected settings
- XMLTV load state
- Native update and scan state

`/admin/api/epg/<lcn>` returns up to 100 relevant programmes around the current time for one channel.

## Command-line reference

### Configuration and device

| Option | Description | Default |
|---|---|---|
| `--config FILE` | Load `key=value` settings before command-line overrides. | Off |
| `--device HOST` | Device DNS name, IPv4 address, or IPv6 address. Required except for `--self-test`. | — |
| `--control-port N` | Device TCP control port. | `8802` |
| `--stream-port N` | Device UDP stream port. | `8800` |

### HTTP, dashboard, EPG, and maintenance

| Option | Description | Default |
|---|---|---|
| `--server`, `--http-server` | Run the HTTP/HTTPS playlist and stream server. | Off |
| `--channels FILE` | Channel XML file. | `channels.xml` |
| `--http-bind HOST` | Listen address or hostname. | `127.0.0.1` |
| `--http-port N` | HTTP listen port. | `8080` |
| `--max-clients N` | Simultaneous viewers of one channel, from `1` to `64`. | `8` |
| `--http-user USER` | Viewer HTTP Basic username; requires `--http-password`. | Off |
| `--http-password PASS` | Viewer HTTP Basic password; requires `--http-user`. | Off |
| `--admin-user USER` | Dashboard administrator username; requires `--admin-password`. | Viewer credentials |
| `--admin-password PASS` | Dashboard administrator password; requires `--admin-user`. | Viewer credentials |
| `--webui` | Enable the administration dashboard. | Enabled |
| `--no-webui` | Disable the administration dashboard. | Off |
| `--web-root DIR` | Dashboard static-assets directory. | `web` beside executable |
| `--epg FILE` | XMLTV programme-guide file. | `epg.xml` beside executable |
| `--epg-update` | Download XMLTV once before server startup/reload. | Off |
| `--no-epg-update` | Disable startup XMLTV download. | Enabled |
| `--epg-url URL` | HTTP/HTTPS XMLTV URL for `--epg-update`. | — |
| `--epg-update-timeout-ms N` | XMLTV download timeout, from `1000` to `120000` ms. | `30000` |
| `--device-channels-update` | Enable native receiver lineup refresh. | Off |
| `--no-device-channels-update` | Disable native lineup refresh. | Enabled |
| `--device-epg-update` | Enable native receiver EPG refresh. | Off |
| `--no-device-epg-update` | Disable native EPG refresh. | Enabled |
| `--device-channels-refresh-minutes N` | Native lineup refresh interval; `0` disables periodic refresh. | `1440` |
| `--device-epg-refresh-minutes N` | Native EPG refresh interval; `0` disables periodic refresh. | `240` |
| `--device-scan-refresh-minutes N` | Receiver scan interval; `0` disables scheduled scans. | `0` |
| `--device-scan-timeout-minutes N` | Receiver scan timeout, from `1` to `120` minutes. | `30` |
| `--device-scan-search-range N` | Receiver-specific scan range code, from `0` to `7`. | `0` |
| `--device-scan-order-by N` | Receiver-specific channel ordering code, from `0` to `3`. | `0` |
| `--device-scan-network` | Enable network scanning. | Enabled |
| `--no-device-scan-network` | Disable network scanning. | Off |
| `--device-scan-epg-after` | Refresh native EPG after a completed scan. | Enabled |
| `--no-device-scan-epg-after` | Skip native EPG refresh after a scan. | Off |
| `--device-update-on-start` | Run enabled native updates during startup. | Enabled |
| `--no-device-update-on-start` | Skip enabled native updates during startup. | Off |
| `--tls-cert FILE` | TLS certificate chain in PEM format; requires OpenSSL and `--tls-key`. | Off |
| `--tls-key FILE` | TLS private key in PEM format; requires OpenSSL and `--tls-cert`. | Off |

### Tuning and satellite

| Option | Description | Default |
|---|---|---|
| `--progidx N` | Tune saved programme index `N`. | — |
| `--freq MHz` | Transponder frequency in MHz. | — |
| `--sr N` | Symbol rate in kSym/s. | — |
| `--pol H\|V\|F\|O` | Polarization or device-specific voltage mode. | — |
| `--sid N` | DVB service ID. | — |
| `--orbital N` | Orbital position in tenths of a degree. | `192` |
| `--east` | Use an eastern orbital position. | Enabled |
| `--west` | Use a western orbital position. | Off |
| `--tone auto\|on\|off` | 22 kHz tone mode. | `auto` |
| `--lnb-low N` | Low-band LNB local oscillator in MHz. | `9750` |
| `--lnb-high N` | High-band LNB local oscillator in MHz. | `10600` |
| `--lnb-switch N` | LNB switch frequency in MHz. | `11700` |
| `--diseqc N` | DiSEqC input value from `0` to `255`. | `0` |
| `--sat-setup` | Send the extended LNB/DiSEqC setup sequence. | Off |
| `--no-sat-setup` | Skip the extended setup sequence. | Enabled |

Direct service tuning requires all four options: `--freq`, `--sr`, `--pol`, and `--sid`.

### Stream processing and UDP output

| Option | Description | Default |
|---|---|---|
| `--even-key HEX` | Set the 16-byte even AES key. | Legacy built-in key |
| `--odd-key HEX` | Set the 16-byte odd AES key. | Unset |
| `--no-default-key` | Disable the legacy built-in even key. | Off |
| `--missing-key pass\|drop` | Forward or discard encrypted packets without a matching key. | `pass` |
| `--vlc-ip HOST` | UDP destination DNS name, IPv4 address, or IPv6 address. | `127.0.0.1` |
| `--vlc-port N` | UDP destination port. | `1234` |
| `--dump FILE.ts` | Also write the processed stream to a file in UDP mode. | Off |
| `--wait-ms N` | Delay after tuning, up to `60000` ms. | `800` |
| `--timeout-ms N` | Network timeout from `100` to `60000` ms. | `3000` |

### Diagnostics

| Option | Description | Default |
|---|---|---|
| `--probe` | Test only the device permission handshake. | Off |
| `--self-test` | Run internal tests without connecting to a device. | Off |
| `--verbose` | Print protocol frames and diagnostics. | Off |
| `--help`, `-h` | Show built-in help. | — |

## Recovery behavior

SORALink sends a control heartbeat approximately every five seconds. If the control connection fails or the transport stream repeatedly times out, it attempts to reconnect, reacquire permission, and retune the active channel.

Native EPG retrieval rotates or restores receiver control sessions when required. Receiver scan polling also attempts to reconnect after a control-session failure.

A failed stream recovery ends the current run or disconnects affected viewers rather than continuing with an unknown device state.

## Troubleshooting

### The control port is not reachable

- Confirm the device hostname or IP address.
- Confirm `--control-port` if the device does not use TCP `8802`.
- Verify that the computer and device are on a reachable network.
- Check local and network firewalls.
- Fully close the official device application and any other client.
- If a hostname resolves to multiple addresses, test a literal working address.

### The device is occupied or refuses permission

SORALink needs an exclusive control session. Force-stop or completely exit the official application on every phone, tablet, or computer that may still be connected, then retry.

### The device reports no signal lock

- Verify frequency, symbol rate, polarization, and service ID.
- Confirm the orbital position and east/west setting.
- Check dish signal and cabling.
- Check the universal-LNB values.
- Select the correct DiSEqC input.
- Try `--sat-setup` when explicit LNB/DiSEqC initialization is required.

### Repeated transport timeouts or recovery attempts

- Allow UDP traffic to and from the configured device stream port.
- Confirm that tuning completed with a signal lock.
- Check whether a host firewall is dropping UDP replies.
- Confirm `--stream-port` if the device does not use UDP `8800`.
- Increase `--timeout-ms` on slow or unstable networks.
- Run with `--verbose` to inspect protocol frames and recovery activity.

### VLC opens the playlist but cannot play a channel

- Run SORALink with `--verbose` and inspect the tuning response.
- Verify the channel's `lcn`, `freq`, `sym`, `pol`, and `s_id` values.
- Try opening `/channel/<lcn>` directly in VLC.
- Check `/status.json` for the current channel and viewer count.
- Confirm that no receiver maintenance or scan is active.
- If authentication is enabled, confirm that VLC sends the same credentials for the playlist and stream URLs.
- If TLS is enabled, confirm that VLC trusts the certificate.

### The dashboard reports a missing asset

- Confirm that `index.html`, `styles.css`, `app.js`, and referenced images are present in the configured `web/` directory.
- Use `--web-root DIR` when the assets are not beside the executable.
- Use `--no-webui` when the dashboard is not required.

### The dashboard will not start on a LAN address

The enabled dashboard cannot be exposed beyond loopback without authentication. Configure either:

- `--admin-user` and `--admin-password`, or
- `--http-user` and `--http-password`

Dedicated administrator credentials are recommended.

### EPG data is empty or does not match channels

- Confirm that `--epg FILE` points to readable XMLTV data.
- Match each channel's `epg_id` or `xmltv_id` to the XMLTV channel ID.
- Check the dashboard EPG programme count.
- Reload the EPG from the dashboard after replacing the file.
- For native EPG, confirm that channels contain a valid `prog_idx`.

### EPG download fails

- Confirm that `--epg-update` and `--epg-url` are both present.
- Use an uncompressed XML URL.
- Increase `--epg-update-timeout-ms` for slow servers.
- Build with OpenSSL for HTTPS URLs.
- Confirm that the operating system has a usable CA trust store for certificate verification.

### Native channel or EPG refresh fails

- Disconnect every viewer before running maintenance.
- Confirm that the target receiver/firmware supports the implemented commands.
- Run with `--verbose` to inspect protocol traffic.
- Keep a known-good local `channels.xml` and `epg.xml` as fallback data.

### A receiver scan does not finish

- Increase `--device-scan-timeout-minutes` within the allowed range.
- Check scan progress in the dashboard.
- Cancel the scan before retrying with different receiver-specific range/order values.
- Confirm that no official receiver application has reclaimed the control session.

### A different channel returns HTTP 409

Another viewer is still using the currently tuned channel. Close all clients on that stream, or disconnect them from the dashboard, then select the new channel again.

### HTTP client limit reached

Increase `--max-clients` up to the hard limit of `64`, or close inactive viewers.

### HTTP bind fails

Another process may already be using the selected address or port. Choose a different port:

```bash
./soralink --device soralink.local --server --http-port 8090
```

For IPv6, ensure that the selected address exists on the host and use bracketed addresses in URLs, for example `http://[::1]:8080/playlist.m3u`.

### TLS support is unavailable

Rebuild with `-DSORALINK_USE_OPENSSL` and link `libssl` and `libcrypto`. Both `--tls-cert` and `--tls-key` are required.

### Encrypted channels remain scrambled

- Confirm whether the stream uses the even or odd scrambling state.
- Supply the corresponding `--even-key` or `--odd-key`.
- Check AES counters in `/status.json`, `/admin/api/status`, or the console statistics.
- Remember that `--missing-key pass` preserves packets that cannot be decrypted; use `drop` only when discarding them is preferable.

## Current limitations

- The hardware supports one tuned HTTP channel at a time, although multiple viewers may share it.
- The dashboard requires external static assets and is not embedded in the executable.
- The built-in HTTP service is purpose-built for SORALink endpoints; it is not a general-purpose web server.
- Authentication is HTTP Basic only and has no roles beyond the viewer/administrator credential split.
- Native TLS and HTTPS EPG downloads are available only in OpenSSL builds.
- The channel XML and XMLTV readers are deliberately lightweight and support only the documented data.
- The EPG downloader expects uncompressed XML and does not support arbitrary relative redirects.
- Native receiver lineup, EPG, and scan commands are implementation- and firmware-specific.
- `F` and `O` polarization/voltage modes are device-specific.
- The legacy built-in key and protocol framing are implementation-specific.
- Hardware compatibility depends on the target device and firmware.

## Stopping SORALink

Press `Ctrl+C`. SORALink handles the interrupt, stops streaming, closes viewer and device sockets, releases TLS resources when enabled, and exits.

## Disclaimer

Use SORALink only with hardware, services, keys, and content that you are authorized to access. This project is an independent interoperability tool and is not presented as an official application from the device manufacturer or VLC.

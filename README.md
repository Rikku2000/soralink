# SORALink ソラリンク

![SORALink](soralink.jpg)

SORALink is a small, single-file C bridge that connects a compatible network satellite tuner/dongle to [VLC media player](https://www.videolan.org/vlc/).

It acquires the device's exclusive control session, tunes a saved program or DVB service, receives MPEG transport-stream data, processes the packets, and forwards the resulting stream to VLC.

The recommended mode runs a local HTTP or HTTPS server that generates VLC-compatible M3U playlists and tunes the device when a playlist item is selected. Multiple clients can watch the same tuned channel simultaneously.

## Highlights in this update

- DNS names, IPv4, and IPv6 for the device, HTTP listener, and UDP destination
- Configurable device control and stream ports
- Up to 64 simultaneous HTTP viewers of the same channel, with a default limit of 8
- HTTP Basic authentication and optional TLS 1.2+ when built with OpenSSL
- `/status.json` endpoint with current channel, client, and AES statistics
- Configurable even and odd AES-128 stream keys
- Configurable pass/drop behavior when an encrypted packet has no matching key
- Multiline channel elements, single- or double-quoted XML attributes, and numeric XML entities
- Automatic control-session recovery and retuning after repeated transport failures
- Stricter protocol response validation
- Built-in AES, transport-stream, XML, HTTP, and protocol self-tests

## Features

- HTTP playlists for TV, radio, or all channels
- Channel selection directly from VLC
- Multiple viewers sharing one tuned HTTP channel
- Direct tuning by frequency, symbol rate, polarization, and service ID
- Tuning by the device's saved program index
- UDP MPEG-TS output for VLC
- Optional `.ts` stream recording in UDP mode
- Universal-LNB and DiSEqC configuration options
- Device permission handshake and periodic connection heartbeat
- Automatic control reconnection and channel retuning
- MPEG-TS alignment, packet validation, AES-128 processing, and removal of PID `0x1FFE`
- HTTP Basic authentication and optional TLS
- Cross-platform socket support for Windows and POSIX systems
- No required third-party runtime libraries; OpenSSL is optional for TLS

## How it works

```text
SORALink device
  ├─ TCP 8802: control, permission, tuning, heartbeat
  └─ UDP 8800: MPEG-TS block requests and replies
                    │
                    ▼
                 soralink
                  ├─ HTTP/HTTPS playlists and MPEG-TS streams → VLC clients
                  └─ UDP MPEG-TS forwarding                  → VLC
```

The device ports are configurable with `--control-port` and `--stream-port`.

SORALink uses an exclusive device session. Fully close any official device application or other client before starting it.

## Requirements

- A compatible network tuner/dongle reachable by DNS name, IPv4, or IPv6
- Network access to the device's TCP control port and UDP stream port
- VLC media player
- A C11 compiler
- A `channels.xml` file for playlist-server mode
- OpenSSL development files only when native TLS support is required

The default device ports are TCP `8802` and UDP `8800`.

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

Run the following command from a Developer Command Prompt:

```powershell
cl /O2 /W4 soralink.c ws2_32.lib
```

The default build produces a single executable and uses its built-in AES, HTTP, and XML implementations.

### Optional OpenSSL/TLS build

Define `SORALINK_USE_OPENSSL` and link OpenSSL when HTTPS support is needed.

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

OpenSSL library filenames and setup can vary between Windows distributions.

## Run the self-tests

The self-test does not require a tuner:

```bash
./soralink --self-test
```

It tests AES-128 decryption, key parsing, even/odd transport-stream processing, missing-key policies, XML parsing, HTTP Basic-auth encoding, and protocol status validation.

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

### Server endpoints

| Endpoint | Description |
|---|---|
| `/` or `/playlist.m3u` | TV and radio playlist |
| `/tv.m3u` | TV-only playlist |
| `/radio.m3u` | Radio-only playlist |
| `/status.json` | Current stream, channel, client, and AES status |
| `/channel/<lcn>` | MPEG-TS stream for a logical channel number |

The server supports `GET`, `HEAD`, and `OPTIONS` requests.

### Multi-client behavior

The first viewer tunes the requested channel. Additional viewers may join that same channel until `--max-clients` is reached.

Because the hardware has one active tuner path, a request for a different channel returns HTTP `409 Conflict` while another channel still has viewers. After all viewers disconnect, the next channel request can retune the device.

## Channel list format

The playlist server reads `<ch ...>` elements from an XML file. Elements may span multiple lines and attributes may use single or double quotes.

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
    s_name="Example TV" />

  <ch type='radio' pol='V' sym='27500' s_id='10402'
      freq='11538' lcn='101' fta='1'
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
| `s_name` | Yes | Display name shown in VLC. |
| `fta` | No | Free-to-air flag, parsed as `0` or non-zero. |

Use a unique `lcn` for every channel. Duplicate logical channel numbers are skipped with a warning.

The parser decodes `&amp;`, `&apos;`, `&quot;`, `&lt;`, `&gt;`, decimal numeric entities such as `&#9733;`, and hexadecimal numeric entities such as `&#x2605;`.

This remains a lightweight parser rather than a general-purpose XML implementation. Attribute names must match the documented names, malformed entries are skipped, individual tags are limited to 64 KiB, and the complete file is limited to 64 MiB.

## Single-channel UDP mode

In UDP mode, SORALink forwards MPEG-TS packets to `127.0.0.1:1234` by default.

First open the following network URL in VLC:

```text
udp://@:1234
```

Then start one of the tuning modes below.

### Tune a saved program index

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

The defaults describe a universal LNB:

- Low local oscillator: `9750 MHz`
- High local oscillator: `10600 MHz`
- Switch frequency: `11700 MHz`
- DiSEqC value: `0` (none)
- Orbital position: `19.2°E`
- 22 kHz tone: automatic

Use `--tone on` or `--tone off` only when automatic tone selection is unsuitable for the installation.

## HTTP server access and security

The server listens on `127.0.0.1` by default, so it is accessible only from the same computer.

To make it available on the local network:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0
```

Then open the playlist using the SORALink computer's LAN address, for example:

```text
http://192.168.1.20:8080/playlist.m3u
```

You may also bind to a hostname or IPv6 address. Use `*` to request a wildcard listener through the platform resolver.

### HTTP Basic authentication

Both options must be supplied together:

```bash
./soralink \
  --device soralink.local \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0 \
  --http-user vlc \
  --http-password 'change-this-password'
```

Authentication applies to playlists, status, and channel streams. Basic authentication does not encrypt credentials, so use it with TLS or only on a trusted network.

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
  --tls-cert server-chain.pem \
  --tls-key server-key.pem
```

The built-in TLS server requires TLS 1.2 or newer and disables TLS compression.

> [!WARNING]
> Exposing an unauthenticated or unencrypted stream outside loopback can reveal channel activity and stream content. Use a firewall, authentication, and TLS or a trusted TLS reverse proxy when serving beyond the local machine.

## Status endpoint

Request `/status.json` to inspect the current server state.

When idle:

```json
{
  "streaming": false,
  "clients": 0
}
```

While streaming, the response also includes the current channel parameters and counters for even-key decryptions, odd-key decryptions, and missing keys.

## Command-line reference

| Option | Description | Default |
|---|---|---|
| `--device HOST` | Device DNS name, IPv4 address, or IPv6 address. Required except for `--self-test`. | — |
| `--control-port N` | Device TCP control port. | `8802` |
| `--stream-port N` | Device UDP stream port. | `8800` |
| `--server`, `--http-server` | Run the HTTP/HTTPS playlist and stream server. | Off |
| `--channels FILE` | Channel XML file. | `channels.xml` |
| `--http-bind HOST` | HTTP listen address or hostname. | `127.0.0.1` |
| `--http-port N` | HTTP listen port. | `8080` |
| `--max-clients N` | Simultaneous viewers of the same tuned channel, from `1` to `64`. | `8` |
| `--http-user USER` | Enable HTTP Basic authentication; requires `--http-password`. | Off |
| `--http-password PASS` | Basic-auth password; requires `--http-user`. | Off |
| `--tls-cert FILE` | TLS certificate chain in PEM format; requires an OpenSSL build and `--tls-key`. | Off |
| `--tls-key FILE` | TLS private key in PEM format; requires an OpenSSL build and `--tls-cert`. | Off |
| `--progidx N` | Tune saved program index `N`. | — |
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
| `--even-key HEX` | Set the 16-byte even AES key. | Legacy built-in key |
| `--odd-key HEX` | Set the 16-byte odd AES key. | Unset |
| `--no-default-key` | Disable the legacy built-in even key. | Off |
| `--missing-key pass\|drop` | Forward or discard encrypted packets without a matching key. | `pass` |
| `--vlc-ip HOST` | UDP destination DNS name, IPv4 address, or IPv6 address. | `127.0.0.1` |
| `--vlc-port N` | UDP destination port. | `1234` |
| `--dump FILE.ts` | Also write the processed stream to a file in UDP mode. | Off |
| `--wait-ms N` | Delay after tuning before streaming, up to 60000 ms. | `800` |
| `--timeout-ms N` | Network timeout from 100 to 60000 ms. | `3000` |
| `--probe` | Test only the device permission handshake. | Off |
| `--self-test` | Run internal tests without connecting to a device. | Off |
| `--verbose` | Print protocol frames and diagnostics. | Off |
| `--help`, `-h` | Show the built-in help. | — |

Direct service tuning requires all four options: `--freq`, `--sr`, `--pol`, and `--sid`.

## Recovery behavior

SORALink sends a control heartbeat approximately every five seconds. If the control connection fails or the transport stream repeatedly times out, it attempts to reconnect to the device up to three times, reacquire permission, and retune the active channel.

A failed recovery ends the current run rather than continuing with an unknown device state.

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
- Check the dish signal and cabling.
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
- Check `/status.json` for the current channel and client count.
- If authentication is enabled, confirm that VLC sends the same credentials for the playlist and stream URLs.
- If TLS is enabled, confirm that VLC trusts the certificate.

### A different channel returns HTTP 409

Another viewer is still using the currently tuned channel. Close all clients on that stream, then select the new channel again.

### HTTP client limit reached

Increase `--max-clients` up to the hard limit of `64`, or close inactive viewers.

### HTTP bind fails

Another process may already be using the selected address or port. Choose a different port:

```bash
./soralink --device soralink.local --server --http-port 8090
```

For IPv6, make sure the selected address exists on the host and use bracketed addresses in URLs, for example `http://[::1]:8080/playlist.m3u`.

### TLS support is unavailable

Rebuild with `-DSORALINK_USE_OPENSSL` and link `libssl` and `libcrypto`. Both `--tls-cert` and `--tls-key` are required.

### Encrypted channels remain scrambled

- Confirm whether the stream uses the even or odd scrambling state.
- Supply the corresponding `--even-key` or `--odd-key`.
- Check the AES counters in `/status.json` or the console statistics.
- Remember that `--missing-key pass` preserves packets that cannot be decrypted; use `drop` only when discarding them is preferable.

## Current limitations

- The hardware supports one tuned HTTP channel at a time, although multiple viewers may share it.
- The built-in HTTP server supports only `GET`, `HEAD`, and `OPTIONS`; it is not a general-purpose web server.
- Authentication is HTTP Basic only and has no users, roles, or per-endpoint permissions.
- Native TLS is available only in builds compiled with OpenSSL.
- The XML reader is deliberately lightweight and supports only the documented channel attributes.
- `F` and `O` polarization/voltage modes are device-specific.
- The legacy built-in key and protocol framing are implementation-specific.
- Hardware compatibility depends on the target device and firmware.

## Stopping SORALink

Press `Ctrl+C`. SORALink handles the interrupt, stops streaming, closes client and device sockets, releases TLS resources when enabled, and exits.

## Disclaimer

Use SORALink only with hardware, services, keys, and content that you are authorized to access. This project is an independent interoperability tool and is not presented as an official application from the device manufacturer or VLC.

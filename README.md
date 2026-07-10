# SORALink

SORALink is a small, dependency-free C bridge that connects a compatible network satellite tuner/dongle to [VLC media player](https://www.videolan.org/vlc/).

It acquires the device's control session, tunes a saved program or a DVB service, receives MPEG transport-stream data from the device, processes the packets, and forwards the resulting stream to VLC.

The recommended mode runs a local HTTP server that generates VLC-compatible M3U playlists and changes the tuner whenever a playlist item is selected.

## Features

- Local HTTP playlists for TV, radio, or all channels
- Channel switching directly from VLC
- Direct tuning by frequency, symbol rate, polarization, and service ID
- Tuning by the device's saved program index
- UDP MPEG-TS output for VLC
- Optional `.ts` stream recording
- Universal-LNB and DiSEqC configuration options
- Device permission handshake and periodic connection heartbeat
- MPEG-TS alignment, packet processing, and removal of PID `0x1FFE`
- Built-in AES-128 processing with a startup self-test
- Cross-platform socket support for Windows and POSIX systems
- No third-party runtime libraries

## How it works

```text
SORALink device
  ├─ TCP 8802: control, permission, tuning, heartbeat
  └─ UDP 8800: MPEG-TS block requests and replies
                    │
                    ▼
                 soralink
                  ├─ HTTP playlist/stream server → VLC
                  └─ UDP MPEG-TS forwarding      → VLC
```

SORALink uses an exclusive device session. Fully close any official device application or other client before starting it.

## Requirements

- A compatible network tuner/dongle reachable by IPv4
- Network access to the device on TCP port `8802` and UDP port `8800`
- VLC media player
- A C11 compiler
- A `channels.xml` file for playlist-server mode

Only literal IPv4 addresses are currently accepted. Hostnames and IPv6 addresses are not supported.

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

The source builds as a single executable and does not require an external cryptography, HTTP, or XML library.

## Quick start

### 1. Test the connection

```bash
./soralink --device 192.168.1.100 --probe
```

A successful probe confirms that TCP port `8802` is reachable and that SORALink can claim the device session.

### 2. Start the playlist server

```bash
./soralink \
  --device 192.168.1.100 \
  --server \
  --channels channels.xml
```

Open this URL in VLC:

```text
http://127.0.0.1:8080/playlist.m3u
```

Additional playlists:

| Playlist | URL |
|---|---|
| TV and radio | `http://127.0.0.1:8080/playlist.m3u` |
| TV only | `http://127.0.0.1:8080/tv.m3u` |
| Radio only | `http://127.0.0.1:8080/radio.m3u` |

Selecting another playlist item closes the current stream, retunes the device if necessary, selects the new service ID, and starts a new MPEG-TS stream.

## Channel list format

The playlist server reads a simple XML file. Each channel must be written as a complete `<ch ... />` element on a single line.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<channels>
  <ch type="TV" pol="H" sym="22000" s_id="10301" freq="11494" lcn="1" fta="1" s_name="Example TV" />
  <ch type="RADIO" pol="V" sym="27500" s_id="10402" freq="11538" lcn="101" fta="1" s_name="Example Radio" />
</channels>
```

### Channel attributes

| Attribute | Required | Description |
|---|---:|---|
| `type` | Yes | Use `TV` or `RADIO` for the filtered playlists. |
| `pol` | Yes | Polarization: `H` or `V`. |
| `sym` | Yes | Symbol rate in kSym/s, such as `22000` or `27500`. |
| `s_id` | Yes | DVB service ID. |
| `freq` | Yes | Transponder frequency in MHz. |
| `lcn` | Yes | Logical channel number used in the playlist and `/channel/<lcn>` URL. |
| `s_name` | Yes | Display name shown in VLC. |
| `fta` | No | Free-to-air flag. Parsed as `0` or non-zero. |

Use a unique `lcn` for every channel. The parser decodes the XML entities `&amp;`, `&apos;`, `&quot;`, `&lt;`, and `&gt;` in channel names.

This is intentionally a lightweight line-based parser, not a general-purpose XML parser. Multiline channel elements, single-quoted attributes, and alternative attribute names are not supported.

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
  --device 192.168.1.100 \
  --progidx 0
```

### Tune a service directly

Replace the example values with the service parameters for your channel:

```bash
./soralink \
  --device 192.168.1.100 \
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
  --device 192.168.1.100 \
  --progidx 0 \
  --vlc-ip 192.168.1.50 \
  --vlc-port 1234
```

The destination must permit incoming UDP traffic on the selected port.

### Save a transport-stream dump

```bash
./soralink \
  --device 192.168.1.100 \
  --progidx 0 \
  --dump recording.ts
```

The dump option is available in UDP mode. The same processed MPEG-TS packets sent to VLC are written to the file.

## Satellite and LNB configuration

By default, SORALink uses the device's normal tuning path and skips the extended LNB/DiSEqC setup sequence.

Enable the extended setup when your installation requires SORALink to configure these values explicitly:

```bash
./soralink \
  --device 192.168.1.100 \
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

## Command-line reference

| Option | Description | Default |
|---|---|---|
| `--device IP` | Device IPv4 address. Required. | — |
| `--server` | Run the local HTTP playlist and stream server. | Off |
| `--channels FILE` | Channel XML file. | `channels.xml` |
| `--http-bind IP` | HTTP listen address. | `127.0.0.1` |
| `--http-port N` | HTTP listen port. | `8080` |
| `--progidx N` | Tune saved program index `N`. | — |
| `--freq MHz` | Transponder frequency in MHz. | — |
| `--sr N` | Symbol rate in kSym/s. | — |
| `--pol H\|V` | Horizontal or vertical polarization. | — |
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
| `--vlc-ip IP` | UDP destination IPv4 address. | `127.0.0.1` |
| `--vlc-port N` | UDP destination port. | `1234` |
| `--dump FILE.ts` | Also write the processed stream to a file in UDP mode. | Off |
| `--wait-ms N` | Delay after tuning before streaming. | `800` |
| `--timeout-ms N` | Socket send and receive timeout. | `3000` |
| `--probe` | Test only the device permission handshake. | Off |
| `--verbose` | Print protocol frames and diagnostics. | Off |
| `--help`, `-h` | Show the built-in help. | — |

Direct service tuning requires all four options: `--freq`, `--sr`, `--pol`, and `--sid`.

## HTTP server access

The server listens on `127.0.0.1` by default, so it is only accessible from the same computer.

To make it available on the local network:

```bash
./soralink \
  --device 192.168.1.100 \
  --server \
  --channels channels.xml \
  --http-bind 0.0.0.0
```

Then open the playlist using the SORALink computer's LAN address, for example:

```text
http://192.168.1.20:8080/playlist.m3u
```

> [!WARNING]
> The built-in server has no authentication, authorization, or TLS encryption. Bind it to `0.0.0.0` only on a trusted network and protect the port with an appropriate firewall.

## Troubleshooting

### Port 8802 is not reachable

- Confirm the device's IPv4 address.
- Verify that the computer and device are on the same reachable network.
- Check local and network firewalls.
- Fully close the official device application and any other client.

### The device is occupied or refuses permission

SORALink needs an exclusive control session. Force-stop or completely exit the official application on every phone, tablet, or computer that may still be connected, then retry.

### The device reports no signal lock

- Verify frequency, symbol rate, polarization, and service ID.
- Confirm the orbital position and east/west setting.
- Check the dish signal and cabling.
- Check the universal-LNB values.
- Select the correct DiSEqC input.
- Try `--sat-setup` when explicit LNB/DiSEqC initialization is required.

### Repeated `No TS reply yet` messages

- Allow UDP traffic to and from device port `8800`.
- Confirm that tuning completed with a signal lock.
- Check whether a host firewall is dropping UDP replies.
- Increase `--timeout-ms` on slow or unstable networks.

### VLC opens the playlist but cannot play a channel

- Run SORALink with `--verbose` and inspect the tuning response.
- Verify that the channel's `lcn`, `freq`, `sym`, `pol`, and `s_id` values are correct.
- Ensure every `<ch>` entry is on a single line.
- Try opening `/channel/<lcn>` directly in VLC.

### HTTP bind fails

Another process may already be using the selected port. Choose a different one:

```bash
./soralink --device 192.168.1.100 --server --http-port 8090
```

## Current limitations

- IPv4 literals only; no DNS names or IPv6
- One active HTTP channel stream at a time
- Minimal GET-only HTTP implementation
- No HTTP authentication or TLS
- Lightweight, line-based XML parsing
- Horizontal and vertical polarization only in user-facing options
- The current stream transform decrypts packets marked with the even scrambling key; odd-key packets are counted but not decrypted
- The embedded device key and protocol framing are implementation-specific
- Hardware compatibility depends on the target device and firmware

## Stopping SORALink

Press `Ctrl+C`. SORALink handles the interrupt, stops streaming, closes its sockets, and exits.

## Disclaimer

Use SORALink only with hardware, services, and content that you are authorized to access. This project is an independent interoperability tool and is not presented as an official application from the device manufacturer or VLC.

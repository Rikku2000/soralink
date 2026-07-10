#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
#define CLOSESOCKET closesocket
#define SOCKET_INVALID INVALID_SOCKET
#define SOCKET_ERROR_CODE() WSAGetLastError()
#define SLEEP_MS(ms) Sleep((DWORD)(ms))
#else
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
typedef int socket_t;
#define CLOSESOCKET close
#define SOCKET_INVALID (-1)
#define SOCKET_ERROR_CODE() errno
static void sleep_ms(unsigned milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}
#define SLEEP_MS(ms) sleep_ms((unsigned)(ms))
#endif

#define CONTROL_PORT 8802
#define STREAM_PORT  8800

#define FRAME_SIZE       31
#define CSW_SIZE         13
#define TS_PACKET_SIZE   188
#define TS_BLOCK_SIZE    48128
#define EXTRA_CSW_SIZE   64
#define TS_REPLY_SIZE    (TS_BLOCK_SIZE + EXTRA_CSW_SIZE + CSW_SIZE)
#define VLC_CHUNK_SIZE   (7 * TS_PACKET_SIZE)

static volatile int g_running = 1;

typedef enum {
    TUNE_NONE = 0,
    TUNE_PROGIDX,
    TUNE_DVBS
} tune_mode_t;

typedef enum {
    TONE_AUTO = 0,
    TONE_ON,
    TONE_OFF
} tone_mode_t;

typedef struct {
    const char *device_ip;
    const char *vlc_ip;
    uint16_t vlc_port;
    tune_mode_t tune_mode;
    int program_index;

    uint32_t frequency_mhz;
    uint32_t symbol_rate_ks;
    char polarization;
    uint32_t service_id;
    uint32_t orbital_tenths;
    bool west;
    tone_mode_t tone;

    uint32_t lnb_low_mhz;
    uint32_t lnb_high_mhz;
    uint32_t lnb_switch_mhz;
    uint32_t diseqc_port;
    bool satellite_setup;

    const char *dump_path;
    int wait_ms;
    int timeout_ms;
    bool probe_only;
    bool verbose;

    bool http_server;
    const char *channels_path;
    const char *http_bind_ip;
    uint16_t http_port;
} options_t;

typedef struct {
    char type[8];
    char polarization;
    uint32_t symbol_rate_ks;
    uint32_t service_id;
    uint32_t frequency_mhz;
    uint32_t lcn;
    bool fta;
    char name[256];
} channel_t;

typedef struct {
    channel_t *items;
    size_t count;
} channel_list_t;

typedef struct {
    bool valid;
    uint32_t frequency_mhz;
    uint32_t symbol_rate_ks;
    char polarization;
    uint32_t service_id;
} tuning_state_t;

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD signal_type)
{
    if (signal_type == CTRL_C_EVENT ||
        signal_type == CTRL_BREAK_EVENT ||
        signal_type == CTRL_CLOSE_EVENT) {
        g_running = 0;
        return TRUE;
    }
    return FALSE;
}
#else
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}
#endif

static void print_usage(const char *program)
{
    fprintf(stderr,
        "SORALink -> VLC bridge\n\n"
        "Playlist-server mode (recommended for VLC channel switching):\n"
        "  %s --device IP --server --channels channels.xml [options]\n\n"
        "Single-channel UDP modes:\n"
        "  %s --device IP --progidx N [options]\n"
        "  %s --device IP --freq MHz --sr kSym/s --pol H|V --sid N [options]\n"
        "  %s --device IP --probe [options]\n\n"
        "Server example:\n"
        "  %s --device <IPv4> --server --channels channels.xml\n"
        "  Then open in VLC: http://127.0.0.1:8080/playlist.m3u\n\n"
        "Options:\n"
        "  --device IP          Device IPv4 address (required)\n"
        "  --server             Run the local HTTP playlist/stream server\n"
        "  --channels FILE      Channel XML file (default channels.xml)\n"
        "  --http-bind IP       HTTP listen address (default 127.0.0.1)\n"
        "  --http-port N        HTTP listen port (default 8080)\n"
        "  --progidx N          Tune saved channel/program index N\n"
        "  --freq MHz           Satellite transponder frequency in MHz\n"
        "  --sr N               Symbol rate in kSym/s, e.g. 22000 or 27500\n"
        "  --pol H|V            Polarization\n"
        "  --sid N              DVB service ID\n"
        "  --orbital N          Orbital position in tenths of a degree (default 192)\n"
        "  --east               East orbital position (default)\n"
        "  --west               West orbital position\n"
        "  --tone auto|on|off   22 kHz tone mode (default auto)\n"
        "  --lnb-low N          Universal-LNB low LO in MHz (default 9750)\n"
        "  --lnb-high N         Universal-LNB high LO in MHz (default 10600)\n"
        "  --lnb-switch N       LNB switch frequency in MHz (default 11700)\n"
        "  --diseqc N           DiSEqC 1.1 input value (default 0 = none)\n"
        "  --sat-setup          Send the extended LNB/DiSEqC setup sequence\n"
        "  --no-sat-setup       Skip extended setup (default)\n"
        "  --vlc-ip IP          UDP-mode VLC address (default 127.0.0.1)\n"
        "  --vlc-port N         UDP-mode VLC port (default 1234)\n"
        "  --dump FILE.ts       UDP mode: also save transport stream\n"
        "  --wait-ms N          Delay after tuning (default 800)\n"
        "  --timeout-ms N       Network timeout (default 3000)\n"
        "  --probe              Test permission handshake only\n"
        "  --verbose            Print protocol frames and diagnostics\n"
        "  --help               Show this help\n\n"
        "Server playlists:\n"
        "  http://127.0.0.1:8080/playlist.m3u  (TV and radio)\n"
        "  http://127.0.0.1:8080/tv.m3u        (TV only)\n"
        "  http://127.0.0.1:8080/radio.m3u     (radio only)\n",
        program, program, program, program, program);
}

static bool parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool parse_port(const char *text, uint16_t *port)
{
    uint32_t value;
    if (!parse_u32(text, &value) || value == 0 || value > 65535) {
        return false;
    }
    *port = (uint16_t)value;
    return true;
}

static bool parse_options(int argc, char **argv, options_t *opt)
{
    int i;
    bool have_freq = false;
    bool have_sr = false;
    bool have_pol = false;
    bool have_sid = false;

    memset(opt, 0, sizeof(*opt));
    opt->vlc_ip = "127.0.0.1";
    opt->vlc_port = 1234;
    opt->orbital_tenths = 192;
    opt->tone = TONE_AUTO;
    opt->lnb_low_mhz = 9750;
    opt->lnb_high_mhz = 10600;
    opt->lnb_switch_mhz = 11700;
    opt->diseqc_port = 0;
    opt->satellite_setup = false;
    opt->wait_ms = 800;
    opt->timeout_ms = 3000;
    opt->program_index = -1;
    opt->channels_path = "channels.xml";
    opt->http_bind_ip = "127.0.0.1";
    opt->http_port = 8080;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (strcmp(arg, "--device") == 0 && i + 1 < argc) {
            opt->device_ip = argv[++i];
        } else if (strcmp(arg, "--server") == 0 ||
                   strcmp(arg, "--http-server") == 0) {
            opt->http_server = true;
            opt->tune_mode = TUNE_NONE;
        } else if (strcmp(arg, "--channels") == 0 && i + 1 < argc) {
            opt->channels_path = argv[++i];
        } else if (strcmp(arg, "--http-bind") == 0 && i + 1 < argc) {
            opt->http_bind_ip = argv[++i];
        } else if (strcmp(arg, "--http-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &opt->http_port)) {
                fprintf(stderr, "Invalid --http-port value.\n");
                return false;
            }
        } else if (strcmp(arg, "--vlc-ip") == 0 && i + 1 < argc) {
            opt->vlc_ip = argv[++i];
        } else if (strcmp(arg, "--vlc-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &opt->vlc_port)) {
                fprintf(stderr, "Invalid --vlc-port value.\n");
                return false;
            }
        } else if (strcmp(arg, "--progidx") == 0 && i + 1 < argc) {
            uint32_t value;
            if (!parse_u32(argv[++i], &value) || value > INT32_MAX) {
                fprintf(stderr, "Invalid --progidx value.\n");
                return false;
            }
            opt->program_index = (int)value;
            opt->tune_mode = TUNE_PROGIDX;
        } else if (strcmp(arg, "--freq") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->frequency_mhz)) {
                fprintf(stderr, "Invalid --freq value.\n");
                return false;
            }
            have_freq = true;
        } else if (strcmp(arg, "--sr") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->symbol_rate_ks)) {
                fprintf(stderr, "Invalid --sr value.\n");
                return false;
            }
            have_sr = true;
        } else if (strcmp(arg, "--pol") == 0 && i + 1 < argc) {
            const char *pol = argv[++i];
            if (pol[0] == 'H' || pol[0] == 'h') {
                opt->polarization = 'H';
            } else if (pol[0] == 'V' || pol[0] == 'v') {
                opt->polarization = 'V';
            } else {
                fprintf(stderr, "--pol must be H or V.\n");
                return false;
            }
            have_pol = true;
        } else if (strcmp(arg, "--sid") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->service_id)) {
                fprintf(stderr, "Invalid --sid value.\n");
                return false;
            }
            have_sid = true;
        } else if (strcmp(arg, "--orbital") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->orbital_tenths) ||
                opt->orbital_tenths > 32767) {
                fprintf(stderr, "Invalid --orbital value.\n");
                return false;
            }
        } else if (strcmp(arg, "--east") == 0) {
            opt->west = false;
        } else if (strcmp(arg, "--west") == 0) {
            opt->west = true;
        } else if (strcmp(arg, "--tone") == 0 && i + 1 < argc) {
            const char *tone = argv[++i];
            if (strcmp(tone, "auto") == 0) {
                opt->tone = TONE_AUTO;
            } else if (strcmp(tone, "on") == 0) {
                opt->tone = TONE_ON;
            } else if (strcmp(tone, "off") == 0) {
                opt->tone = TONE_OFF;
            } else {
                fprintf(stderr, "--tone must be auto, on, or off.\n");
                return false;
            }
        } else if (strcmp(arg, "--lnb-low") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->lnb_low_mhz) ||
                opt->lnb_low_mhz > 65535) {
                fprintf(stderr, "Invalid --lnb-low value.\n");
                return false;
            }
        } else if (strcmp(arg, "--lnb-high") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->lnb_high_mhz) ||
                opt->lnb_high_mhz > 65535) {
                fprintf(stderr, "Invalid --lnb-high value.\n");
                return false;
            }
        } else if (strcmp(arg, "--lnb-switch") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->lnb_switch_mhz) ||
                opt->lnb_switch_mhz > 65535) {
                fprintf(stderr, "Invalid --lnb-switch value.\n");
                return false;
            }
        } else if (strcmp(arg, "--diseqc") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->diseqc_port) ||
                opt->diseqc_port > 255) {
                fprintf(stderr, "Invalid --diseqc value.\n");
                return false;
            }
        } else if (strcmp(arg, "--sat-setup") == 0) {
            opt->satellite_setup = true;
        } else if (strcmp(arg, "--no-sat-setup") == 0) {
            opt->satellite_setup = false;
        } else if (strcmp(arg, "--dump") == 0 && i + 1 < argc) {
            opt->dump_path = argv[++i];
        } else if (strcmp(arg, "--wait-ms") == 0 && i + 1 < argc) {
            uint32_t value;
            if (!parse_u32(argv[++i], &value) || value > 60000) {
                fprintf(stderr, "Invalid --wait-ms value.\n");
                return false;
            }
            opt->wait_ms = (int)value;
        } else if (strcmp(arg, "--timeout-ms") == 0 && i + 1 < argc) {
            uint32_t value;
            if (!parse_u32(argv[++i], &value) || value < 100 || value > 60000) {
                fprintf(stderr, "Invalid --timeout-ms value.\n");
                return false;
            }
            opt->timeout_ms = (int)value;
        } else if (strcmp(arg, "--probe") == 0) {
            opt->probe_only = true;
            opt->tune_mode = TUNE_NONE;
        } else if (strcmp(arg, "--verbose") == 0) {
            opt->verbose = true;
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", arg);
            return false;
        }
    }

    if (opt->device_ip == NULL) {
        fprintf(stderr, "--device is required.\n");
        return false;
    }

    if (!opt->probe_only && !opt->http_server &&
        opt->tune_mode != TUNE_PROGIDX) {
        if (have_freq || have_sr || have_pol || have_sid) {
            if (!(have_freq && have_sr && have_pol && have_sid)) {
                fprintf(stderr,
                    "Direct DVB-S tuning requires --freq, --sr, --pol, and --sid.\n");
                return false;
            }
            opt->tune_mode = TUNE_DVBS;
        } else {
            fprintf(stderr,
                "Choose --progidx N, direct DVB-S parameters, or --probe.\n");
            return false;
        }
    }

    if (opt->frequency_mhz > 65535 ||
        opt->symbol_rate_ks > 0xFFFFFF) {
        fprintf(stderr, "One or more DVB-S values exceed protocol limits.\n");
        return false;
    }

    return true;
}

static void hex_dump(const char *label, const uint8_t *data, size_t length)
{
    size_t i;
    printf("%s (%zu bytes):", label, length);
    for (i = 0; i < length; ++i) {
        if ((i % 16) == 0) {
            printf("\n  ");
        }
        printf("%02X ", data[i]);
    }
    printf("\n");
}

static void put_u16_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static void put_u24_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 16) & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)(value & 0xFFU);
}

static void put_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFFU);
    dst[1] = (uint8_t)((value >> 16) & 0xFFU);
    dst[2] = (uint8_t)((value >> 8) & 0xFFU);
    dst[3] = (uint8_t)(value & 0xFFU);
}

static void build_frame(uint8_t frame[FRAME_SIZE],
                        uint32_t transfer_length,
                        uint8_t transport_marker,
                        uint8_t command)
{
    static const uint8_t prefix[8] = {
        0x55, 0x53, 0x42, 0x43, 0xA0, 0xCB, 0x03, 0x37
    };

    memset(frame, 0, FRAME_SIZE);
    memcpy(frame, prefix, sizeof(prefix));
    put_u32_be(&frame[8], transfer_length);
    frame[12] = 0x80;
    frame[13] = 0x00;
    frame[14] = 0x10;

    frame[15] = transport_marker;
    frame[16] = 0x41;
    frame[17] = 0x50;
    frame[18] = 0x49;
    frame[19] = 0x58;
    frame[20] = command;
}

static void build_permission_query(uint8_t frame[FRAME_SIZE])
{
    build_frame(frame, 12, 0xF6, 22);
}

static void build_permission_claim(uint8_t frame[FRAME_SIZE])
{
    build_frame(frame, 4, 0xF6, 23);
}

static void build_program_index_tune(uint8_t frame[FRAME_SIZE], uint32_t index)
{
    build_frame(frame, 4, 0xF6, 3);
    frame[24] = 2;
    put_u32_be(&frame[25], index);
}

static void build_service_id_tune(uint8_t frame[FRAME_SIZE], uint32_t sid)
{
    build_frame(frame, 4, 0xF6, 3);
    frame[24] = 3;
    put_u32_be(&frame[25], sid);
}

static uint8_t build_satellite_flags(char pol, tone_mode_t tone)
{
    uint8_t flags = 0x04;

    if (pol == 'H') {
        flags |= 0x10;
        flags &= (uint8_t)~0x20;
    } else if (pol == 'V') {
        flags &= (uint8_t)~0x10;
        flags |= 0x20;
    } else if (pol == 'F') {
        flags |= 0x10;
        flags |= 0x20;
    } else {
        flags &= (uint8_t)~0x10;
        flags &= (uint8_t)~0x20;
    }

    if (tone == TONE_ON) {
        flags |= 0x40;
        flags &= (uint8_t)~0x80;
    } else if (tone == TONE_OFF) {
        flags &= (uint8_t)~0x40;
        flags |= 0x80;
    } else {
        flags &= (uint8_t)~0x40;
        flags &= (uint8_t)~0x80;
    }

    return flags;
}

static void put_orbital(uint8_t *dst, uint32_t orbital_tenths, bool west)
{
    put_u16_be(dst, orbital_tenths);
    if (west) {
        dst[0] |= 0x80;
    } else {
        dst[0] &= 0x7F;
    }
}

static void build_lnb_config(uint8_t frame[FRAME_SIZE], const options_t *opt)
{
    build_frame(frame, 2, 0xF6, 7);
    put_u16_be(&frame[21], opt->lnb_low_mhz);
    put_u16_be(&frame[23], opt->lnb_high_mhz);
    put_u16_be(&frame[25], opt->lnb_switch_mhz);
    frame[27] = 0;
    put_u16_be(&frame[28], 0);
    frame[30] = 0;
}

static void build_diseqc_config(uint8_t frame[FRAME_SIZE], const options_t *opt)
{
    build_frame(frame, 2, 0xF6, 8);
    frame[21] = (uint8_t)opt->diseqc_port;
}

static void build_satellite_config(uint8_t frame[FRAME_SIZE],
                                   const options_t *opt)
{
    build_frame(frame, 2, 0xF6, 9);
    put_orbital(&frame[21], opt->orbital_tenths, opt->west);
    frame[23] = build_satellite_flags('O', opt->tone);
}

static void build_dvbs_tune(uint8_t frame[FRAME_SIZE], const options_t *opt)
{
    build_frame(frame, 32, 0xF6, 1);
    frame[21] = build_satellite_flags(opt->polarization, opt->tone);
    put_orbital(&frame[22], opt->orbital_tenths, opt->west);
    put_u16_be(&frame[24], opt->symbol_rate_ks);
    put_u24_be(&frame[26], opt->frequency_mhz);
}

static void build_ts_request(uint8_t frame[FRAME_SIZE], uint8_t retry_flag)
{
    build_frame(frame, TS_BLOCK_SIZE, 0xF9, 1);
    frame[21] = retry_flag;
}

static int socket_set_timeout(socket_t sock, int milliseconds)
{
#ifdef _WIN32
    DWORD timeout = (DWORD)milliseconds;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout, sizeof(timeout)) != 0) {
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                   (const char *)&timeout, sizeof(timeout)) != 0) {
        return -1;
    }
#else
    struct timeval timeout;
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) != 0) {
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) != 0) {
        return -1;
    }
#endif
    return 0;
}

static bool parse_ipv4(const char *text, struct sockaddr_in *address, uint16_t port)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    return inet_pton(AF_INET, text, &address->sin_addr) == 1;
}

static socket_t connect_tcp(const char *ip, uint16_t port, int timeout_ms)
{
    socket_t sock;
    struct sockaddr_in address;
    int keepalive = 1;
    int reuse = 1;
    int receive_buffer = 262144;

    if (!parse_ipv4(ip, &address, port)) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", ip);
        return SOCKET_INVALID;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == SOCKET_INVALID) {
        fprintf(stderr, "TCP socket() failed: %d\n", SOCKET_ERROR_CODE());
        return SOCKET_INVALID;
    }

    socket_set_timeout(sock, timeout_ms);
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
               (const char *)&keepalive, sizeof(keepalive));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof(reuse));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
               (const char *)&receive_buffer, sizeof(receive_buffer));

    if (connect(sock, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "TCP connect to %s:%u failed: %d\n",
                ip, (unsigned)port, SOCKET_ERROR_CODE());
        CLOSESOCKET(sock);
        return SOCKET_INVALID;
    }

    return sock;
}

static bool send_all(socket_t sock, const uint8_t *data, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        int result = send(sock,
                          (const char *)data + sent,
                          (int)(length - sent),
                          0);
        if (result <= 0) {
            fprintf(stderr, "TCP send failed: %d\n", SOCKET_ERROR_CODE());
            return false;
        }
        sent += (size_t)result;
    }

    return true;
}

static bool recv_all(socket_t sock, uint8_t *data, size_t length)
{
    size_t received = 0;

    while (received < length) {
        int result = recv(sock,
                          (char *)data + received,
                          (int)(length - received),
                          0);
        if (result == 0) {
            fprintf(stderr, "TCP connection closed by dongle.\n");
            return false;
        }
        if (result < 0) {
            fprintf(stderr, "TCP receive failed/timeout: %d\n",
                    SOCKET_ERROR_CODE());
            return false;
        }
        received += (size_t)result;
    }

    return true;
}

static bool response_has_csw(const uint8_t *response, size_t response_length,
                             size_t data_length)
{
    const uint8_t *csw;

    if (response_length < data_length + CSW_SIZE) {
        return false;
    }

    csw = response + data_length;
    return csw[0] == 0x55 && csw[1] == 0x53 &&
           csw[2] == 0x42 && csw[3] == 0x53;
}

static bool tcp_command(socket_t sock,
                        const uint8_t frame[FRAME_SIZE],
                        size_t expected_data_length,
                        uint8_t *response,
                        size_t response_capacity,
                        bool verbose)
{
    const size_t total = expected_data_length + CSW_SIZE;

    if (response_capacity < total) {
        fprintf(stderr, "Internal response buffer is too small.\n");
        return false;
    }

    if (verbose) {
        hex_dump("TCP request", frame, FRAME_SIZE);
    }

    if (!send_all(sock, frame, FRAME_SIZE)) {
        return false;
    }

    if (!recv_all(sock, response, total)) {
        return false;
    }

    if (verbose) {
        hex_dump("TCP response", response, total);
    }

    if (!response_has_csw(response, total, expected_data_length)) {
        fprintf(stderr,
                "Warning: response does not contain the expected USBS trailer.\n");
    }

    return true;
}

static bool acquire_permission(socket_t control, bool verbose)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t response[64];

    printf("Requesting exclusive Device connection permission...\n");

    build_permission_query(frame);
    if (!tcp_command(control, frame, 12, response, sizeof(response), verbose)) {
        return false;
    }

    if (response[0] == 0) {
        fprintf(stderr,
                "Dongle did not report an available network session.\n");
        return false;
    }

    if (response[2] != 0) {
        fprintf(stderr,
                "Dongle is occupied or refused permission (status byte 0x%02X).\n"
                "Fully force-stop Device on every device and retry.\n",
                response[2]);
        return false;
    }

    build_permission_claim(frame);
    if (!tcp_command(control, frame, 4, response, sizeof(response), verbose)) {
        return false;
    }

    if (response[2] != 0) {
        fprintf(stderr,
                "Permission claim failed (status byte 0x%02X).\n",
                response[2]);
        return false;
    }

    printf("Permission granted.\n");
    return true;
}

static bool send_heartbeat(socket_t control, bool verbose)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t response[64];

    build_permission_query(frame);
    if (!tcp_command(control, frame, 12,
                     response, sizeof(response), verbose)) {
        fprintf(stderr, "TCP heartbeat failed.\n");
        return false;
    }

    if (response[0] != 0xF6 || response[1] != 0x16) {
        fprintf(stderr,
                "Unexpected heartbeat response: %02X %02X.\n",
                response[0], response[1]);
        return false;
    }

    if (verbose) {
        printf("Heartbeat OK (connection status byte 0x%02X).\n",
               response[2]);
    }
    return true;
}

static bool response_reports_lock(const uint8_t *response,
                                  size_t length,
                                  const char *label)
{
    if (length < 4 || response[0] != 0xF6 || response[1] != 0x03) {
        fprintf(stderr, "%s returned an unexpected response.\n", label);
        return false;
    }

    printf("%s result: signal lock=%s, service=%s.\n",
           label,
           response[2] == 1 ? "YES" : "NO",
           response[3] == 1 ? "FTA" : "scrambled/unknown");
    return response[2] == 1;
}

static bool configure_satellite(socket_t control, const options_t *opt)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t response[64];

    if (!opt->satellite_setup) {
        printf("Using Device normal DVB-S2 path (no extended 07/08/09 setup).\n");
        return true;
    }

    printf("Configuring universal LNB: low=%u, high=%u, switch=%u MHz, "
           "DiSEqC=%u, satellite=%u.%u%c...\n",
           (unsigned)opt->lnb_low_mhz,
           (unsigned)opt->lnb_high_mhz,
           (unsigned)opt->lnb_switch_mhz,
           (unsigned)opt->diseqc_port,
           (unsigned)(opt->orbital_tenths / 10),
           (unsigned)(opt->orbital_tenths % 10),
           opt->west ? 'W' : 'E');

    build_lnb_config(frame, opt);
    if (!tcp_command(control, frame, 2,
                     response, sizeof(response), opt->verbose)) {
        return false;
    }
    SLEEP_MS(80);

    build_diseqc_config(frame, opt);
    if (!tcp_command(control, frame, 2,
                     response, sizeof(response), opt->verbose)) {
        return false;
    }
    SLEEP_MS(80);

    build_satellite_config(frame, opt);
    if (!tcp_command(control, frame, 2,
                     response, sizeof(response), opt->verbose)) {
        return false;
    }
    SLEEP_MS(150);
    return true;
}

static bool tune_device(socket_t control, const options_t *opt)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t response[64];

    if (!configure_satellite(control, opt)) {
        return false;
    }

    if (opt->tune_mode == TUNE_PROGIDX) {
        printf("Tuning saved program index %d...\n", opt->program_index);
        build_program_index_tune(frame, (uint32_t)opt->program_index);
        if (!tcp_command(control, frame, 4,
                         response, sizeof(response), opt->verbose)) {
            return false;
        }
        if (!response_reports_lock(response, 4, "Program-index tune")) {
            fprintf(stderr,
                    "The dongle reports NO SIGNAL LOCK. The saved channel "
                    "entry is valid, but the active LNB/DiSEqC setup does "
                    "not reach that satellite/transponder.\n");
            return false;
        }
    } else if (opt->tune_mode == TUNE_DVBS) {
        printf("Tuning %u MHz, %u kSym/s, %c, SID %u, %u.%u%c...\n",
               (unsigned)opt->frequency_mhz,
               (unsigned)opt->symbol_rate_ks,
               opt->polarization,
               (unsigned)opt->service_id,
               (unsigned)(opt->orbital_tenths / 10),
               (unsigned)(opt->orbital_tenths % 10),
               opt->west ? 'W' : 'E');

        build_dvbs_tune(frame, opt);
        if (!tcp_command(control, frame, 2,
                         response, sizeof(response), opt->verbose)) {
            return false;
        }
        SLEEP_MS(300);

        build_service_id_tune(frame, opt->service_id);
        if (!tcp_command(control, frame, 4,
                         response, sizeof(response), opt->verbose)) {
            return false;
        }
        if (!response_reports_lock(response, 4, "Service-ID tune")) {
            fprintf(stderr,
                    "The dongle reports NO SIGNAL LOCK. Check LNB type, "
                    "DiSEqC input, dish signal, and transponder values.\n");
            return false;
        }
    } else {
        return true;
    }

    printf("Signal locked; waiting %d ms for stream setup...\n",
           opt->wait_ms);
    SLEEP_MS(opt->wait_ms);
    return true;
}

static socket_t create_udp_socket(int timeout_ms)
{
    socket_t sock;
    int reuse = 1;
    int buffer_size = 262144;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == SOCKET_INVALID) {
        fprintf(stderr, "UDP socket() failed: %d\n", SOCKET_ERROR_CODE());
        return SOCKET_INVALID;
    }

    socket_set_timeout(sock, timeout_ms);
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof(reuse));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
               (const char *)&buffer_size, sizeof(buffer_size));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
               (const char *)&buffer_size, sizeof(buffer_size));

    return sock;
}

static int find_ts_offset(const uint8_t *data, size_t length)
{
    size_t offset;
    const int required_syncs = 5;

    for (offset = 0; offset < TS_PACKET_SIZE && offset < length; ++offset) {
        int matches = 0;
        size_t pos = offset;
        while (pos < length && matches < required_syncs) {
            if (data[pos] != 0x47) {
                break;
            }
            ++matches;
            pos += TS_PACKET_SIZE;
        }
        if (matches >= required_syncs) {
            return (int)offset;
        }
    }

    return -1;
}

static uint16_t ts_pid(const uint8_t *packet)
{
    return (uint16_t)(((packet[1] & 0x1FU) << 8) | packet[2]);
}

typedef struct {
    uint8_t round_key[176];
} aes128_ctx_t;

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t aes_inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static uint8_t aes_xtime(uint8_t value)
{
    return (uint8_t)((value << 1) ^ ((value & 0x80U) ? 0x1BU : 0x00U));
}

static uint8_t aes_mul(uint8_t a, uint8_t b)
{
    uint8_t result = 0;
    while (b != 0) {
        if (b & 1U) {
            result ^= a;
        }
        a = aes_xtime(a);
        b >>= 1;
    }
    return result;
}

static void aes128_init(aes128_ctx_t *ctx, const uint8_t key[16])
{
    static const uint8_t rcon[10] = {
        0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
    };
    unsigned generated = 16;
    unsigned rcon_index = 0;
    uint8_t temp[4];

    memcpy(ctx->round_key, key, 16);
    while (generated < sizeof(ctx->round_key)) {
        unsigned i;
        memcpy(temp, ctx->round_key + generated - 4, 4);
        if ((generated % 16U) == 0U) {
            const uint8_t first = temp[0];
            temp[0] = aes_sbox[temp[1]];
            temp[1] = aes_sbox[temp[2]];
            temp[2] = aes_sbox[temp[3]];
            temp[3] = aes_sbox[first];
            temp[0] ^= rcon[rcon_index++];
        }
        for (i = 0; i < 4; ++i) {
            ctx->round_key[generated] =
                (uint8_t)(ctx->round_key[generated - 16] ^ temp[i]);
            ++generated;
        }
    }
}

static void aes_add_round_key(uint8_t state[16], const uint8_t *round_key)
{
    unsigned i;
    for (i = 0; i < 16; ++i) {
        state[i] ^= round_key[i];
    }
}

static void aes_inv_sub_bytes(uint8_t state[16])
{
    unsigned i;
    for (i = 0; i < 16; ++i) {
        state[i] = aes_inv_sbox[state[i]];
    }
}

static void aes_inv_shift_rows(uint8_t state[16])
{
    uint8_t t;

    t = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = t;

    t = state[2];
    state[2] = state[10];
    state[10] = t;
    t = state[6];
    state[6] = state[14];
    state[14] = t;

    t = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = t;
}

static void aes_inv_mix_columns(uint8_t state[16])
{
    unsigned column;
    for (column = 0; column < 4; ++column) {
        const unsigned i = column * 4U;
        const uint8_t a = state[i];
        const uint8_t b = state[i + 1];
        const uint8_t c = state[i + 2];
        const uint8_t d = state[i + 3];
        state[i]     = (uint8_t)(aes_mul(a, 14) ^ aes_mul(b, 11) ^
                                 aes_mul(c, 13) ^ aes_mul(d, 9));
        state[i + 1] = (uint8_t)(aes_mul(a, 9) ^ aes_mul(b, 14) ^
                                 aes_mul(c, 11) ^ aes_mul(d, 13));
        state[i + 2] = (uint8_t)(aes_mul(a, 13) ^ aes_mul(b, 9) ^
                                 aes_mul(c, 14) ^ aes_mul(d, 11));
        state[i + 3] = (uint8_t)(aes_mul(a, 11) ^ aes_mul(b, 13) ^
                                 aes_mul(c, 9) ^ aes_mul(d, 14));
    }
}

static void aes128_decrypt_block(const aes128_ctx_t *ctx,
                                 const uint8_t input[16],
                                 uint8_t output[16])
{
    int round;
    uint8_t state[16];

    memcpy(state, input, 16);
    aes_add_round_key(state, ctx->round_key + 160);

    for (round = 9; round >= 1; --round) {
        aes_inv_shift_rows(state);
        aes_inv_sub_bytes(state);
        aes_add_round_key(state, ctx->round_key + (round * 16));
        aes_inv_mix_columns(state);
    }

    aes_inv_shift_rows(state);
    aes_inv_sub_bytes(state);
    aes_add_round_key(state, ctx->round_key);
    memcpy(output, state, 16);
}

static bool aes128_self_test(void)
{
    static const uint8_t key[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const uint8_t ciphertext[16] = {
        0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
        0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a
    };
    static const uint8_t plaintext[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
    };
    aes128_ctx_t ctx;
    uint8_t output[16];

    aes128_init(&ctx, key);
    aes128_decrypt_block(&ctx, ciphertext, output);
    return memcmp(output, plaintext, sizeof(plaintext)) == 0;
}

static bool transform_ts_packet(const aes128_ctx_t *aes,
                                const uint8_t input[TS_PACKET_SIZE],
                                uint8_t output[TS_PACKET_SIZE],
                                uint64_t *decrypted_packets,
                                uint64_t *odd_key_packets,
                                uint64_t *invalid_packets)
{
    unsigned scrambling;
    unsigned adaptation_control;
    size_t payload_offset = 4;
    size_t decrypt_length;
    size_t pos;

    memcpy(output, input, TS_PACKET_SIZE);
    if (input[0] != 0x47) {
        ++(*invalid_packets);
        return false;
    }

    scrambling = (unsigned)((input[3] >> 6) & 0x03U);
    adaptation_control = (unsigned)((input[3] >> 4) & 0x03U);

    output[3] &= 0x3FU;

    if (scrambling == 3U) {
        ++(*odd_key_packets);
        return true;
    }

    if (scrambling != 2U || (adaptation_control & 1U) == 0U) {
        return true;
    }

    if (adaptation_control == 3U) {
        payload_offset = 5U + (size_t)input[4];
        if (payload_offset > TS_PACKET_SIZE) {
            ++(*invalid_packets);
            return false;
        }
    }

    decrypt_length = (TS_PACKET_SIZE - payload_offset) & ~(size_t)15U;
    for (pos = 0; pos < decrypt_length; pos += 16U) {
        aes128_decrypt_block(aes,
                             input + payload_offset + pos,
                             output + payload_offset + pos);
    }

    ++(*decrypted_packets);
    return true;
}

static size_t forward_ts(socket_t output,
                         const struct sockaddr_in *vlc_address,
                         FILE *dump,
                         const aes128_ctx_t *aes,
                         const uint8_t *data,
                         size_t length,
                         uint64_t *dropped_pid_1ffe,
                         uint64_t *decrypted_packets,
                         uint64_t *odd_key_packets,
                         uint64_t *invalid_packets)
{
    uint8_t chunk[VLC_CHUNK_SIZE];
    uint8_t transformed[TS_PACKET_SIZE];
    size_t chunk_used = 0;
    size_t pos;
    size_t forwarded = 0;

    for (pos = 0; pos + TS_PACKET_SIZE <= length; pos += TS_PACKET_SIZE) {
        const uint8_t *packet = data + pos;

        if (!transform_ts_packet(aes, packet, transformed,
                                 decrypted_packets, odd_key_packets,
                                 invalid_packets)) {
            continue;
        }

        if (ts_pid(transformed) == 0x1FFE) {
            ++(*dropped_pid_1ffe);
            continue;
        }

        memcpy(chunk + chunk_used, transformed, TS_PACKET_SIZE);
        chunk_used += TS_PACKET_SIZE;

        if (dump != NULL) {
            fwrite(transformed, 1, TS_PACKET_SIZE, dump);
        }

        if (chunk_used == sizeof(chunk)) {
            int result = sendto(output,
                                (const char *)chunk,
                                (int)chunk_used,
                                0,
                                (const struct sockaddr *)vlc_address,
                                sizeof(*vlc_address));
            if (result < 0) {
                fprintf(stderr, "VLC UDP send failed: %d\n",
                        SOCKET_ERROR_CODE());
                return forwarded;
            }
            forwarded += chunk_used;
            chunk_used = 0;
        }
    }

    if (chunk_used > 0) {
        int result = sendto(output,
                            (const char *)chunk,
                            (int)chunk_used,
                            0,
                            (const struct sockaddr *)vlc_address,
                            sizeof(*vlc_address));
        if (result >= 0) {
            forwarded += chunk_used;
        }
    }

    return forwarded;
}

static double monotonic_seconds(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER value;
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&value);
    return (double)value.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
#endif
}

static int stream_to_vlc(socket_t control, const options_t *opt)
{
    socket_t dongle_udp = SOCKET_INVALID;
    socket_t output_udp = SOCKET_INVALID;
    struct sockaddr_in dongle_address;
    struct sockaddr_in vlc_address;
    FILE *dump = NULL;
    uint8_t request[FRAME_SIZE];
    uint8_t reply[65536];
    uint8_t retry_flag = 0;
    uint64_t blocks = 0;
    uint64_t bytes_forwarded = 0;
    uint64_t timeouts = 0;
    uint64_t bad_blocks = 0;
    uint64_t dropped_pid_1ffe = 0;
    uint64_t decrypted_packets = 0;
    uint64_t odd_key_packets = 0;
    uint64_t invalid_packets = 0;
    aes128_ctx_t aes;
    static const uint8_t device_default_key[16] = {
        0x12,0x34,0x56,0x78,0x80,0xab,0xcd,0xef,
        0x12,0x34,0x56,0x78,0x80,0xab,0xcd,0xef
    };
    double report_start = monotonic_seconds();
    double last_heartbeat = report_start;

    if (!aes128_self_test()) {
        fprintf(stderr, "Internal AES self-test failed; refusing to stream.\n");
        return EXIT_FAILURE;
    }
    aes128_init(&aes, device_default_key);

    if (!parse_ipv4(opt->device_ip, &dongle_address, STREAM_PORT)) {
        fprintf(stderr, "Invalid device IPv4 address.\n");
        return EXIT_FAILURE;
    }
    if (!parse_ipv4(opt->vlc_ip, &vlc_address, opt->vlc_port)) {
        fprintf(stderr, "Invalid VLC IPv4 address.\n");
        return EXIT_FAILURE;
    }

    dongle_udp = create_udp_socket(opt->timeout_ms);
    output_udp = create_udp_socket(opt->timeout_ms);
    if (dongle_udp == SOCKET_INVALID || output_udp == SOCKET_INVALID) {
        goto fail;
    }

    if (opt->dump_path != NULL) {
        dump = fopen(opt->dump_path, "wb");
        if (dump == NULL) {
            fprintf(stderr, "Cannot open dump file '%s': %s\n",
                    opt->dump_path, strerror(errno));
            goto fail;
        }
        printf("Also writing TS to %s\n", opt->dump_path);
    }

    printf("Forwarding MPEG-TS to udp://@%s:%u\n",
           opt->vlc_ip, (unsigned)opt->vlc_port);
    printf("Press Ctrl+C to stop.\n");

    while (g_running) {
        socklen_t source_length = (socklen_t)sizeof(dongle_address);
        struct sockaddr_in source;
        int received;
        int offset;
        size_t available;
        size_t aligned_length;
        size_t forwarded;
        double now;

        now = monotonic_seconds();
        if (now - last_heartbeat >= 5.0) {
            if (!send_heartbeat(control, opt->verbose)) {
                fprintf(stderr,
                        "The dongle control session was lost; stopping stream.\n");
                goto fail;
            }
            last_heartbeat = monotonic_seconds();
        }

        build_ts_request(request, retry_flag);

        if (opt->verbose && blocks == 0) {
            hex_dump("UDP TS request", request, sizeof(request));
        }

        if (sendto(dongle_udp,
                   (const char *)request,
                   (int)sizeof(request),
                   0,
                   (const struct sockaddr *)&dongle_address,
                   sizeof(dongle_address)) < 0) {
            fprintf(stderr, "Dongle UDP send failed: %d\n",
                    SOCKET_ERROR_CODE());
            goto fail;
        }

        memset(&source, 0, sizeof(source));
        received = recvfrom(dongle_udp,
                            (char *)reply,
                            (int)sizeof(reply),
                            0,
                            (struct sockaddr *)&source,
                            &source_length);

        if (received < 0) {
            ++timeouts;
            retry_flag = 1;
            if (timeouts <= 5 || (timeouts % 20) == 0) {
                fprintf(stderr,
                    "No TS reply yet (timeout %llu). "
                    "Check tune parameters and Firewall.\n",
                    (unsigned long long)timeouts);
            }
            continue;
        }

        retry_flag = 0;

        if (received < TS_BLOCK_SIZE) {
            ++bad_blocks;
            if (opt->verbose || bad_blocks <= 5) {
                fprintf(stderr,
                    "Short UDP reply: %d bytes (expected at least %d).\n",
                    received, TS_BLOCK_SIZE);
                if (opt->verbose) {
                    hex_dump("Short UDP reply", reply, (size_t)received);
                }
            }
            continue;
        }

        if (opt->verbose && blocks == 0) {
            printf("First UDP reply: %d bytes (APK expects %d).\n",
                   received, TS_REPLY_SIZE);
        }

        offset = find_ts_offset(reply, TS_BLOCK_SIZE);
        if (offset < 0) {
            ++bad_blocks;
            if (bad_blocks <= 5 || opt->verbose) {
                fprintf(stderr,
                    "UDP reply does not contain aligned MPEG-TS sync bytes.\n");
            }
            continue;
        }

        available = TS_BLOCK_SIZE - (size_t)offset;
        aligned_length = available - (available % TS_PACKET_SIZE);
        forwarded = forward_ts(output_udp,
                               &vlc_address,
                               dump,
                               &aes,
                               reply + offset,
                               aligned_length,
                               &dropped_pid_1ffe,
                               &decrypted_packets,
                               &odd_key_packets,
                               &invalid_packets);

        bytes_forwarded += forwarded;
        ++blocks;

        now = monotonic_seconds();
        if (now - report_start >= 2.0) {
            const double elapsed = now - report_start;
            const double mbps =
                elapsed > 0.0
                ? ((double)bytes_forwarded * 8.0 / elapsed / 1000000.0)
                : 0.0;

            printf("\rBlocks: %llu  Output: %.2f Mbit/s  "
                   "Timeouts: %llu  Bad: %llu  AES packets: %llu  "
                   "Odd-key: %llu  PID1FFE dropped: %llu      ",
                   (unsigned long long)blocks,
                   mbps,
                   (unsigned long long)timeouts,
                   (unsigned long long)(bad_blocks + invalid_packets),
                   (unsigned long long)decrypted_packets,
                   (unsigned long long)odd_key_packets,
                   (unsigned long long)dropped_pid_1ffe);
            fflush(stdout);

            bytes_forwarded = 0;
            report_start = now;
        }
    }

    printf("\nStopping.\n");

    if (dump != NULL) {
        fflush(dump);
        fclose(dump);
    }
    CLOSESOCKET(output_udp);
    CLOSESOCKET(dongle_udp);
    return EXIT_SUCCESS;

fail:
    if (dump != NULL) {
        fclose(dump);
    }
    if (output_udp != SOCKET_INVALID) {
        CLOSESOCKET(output_udp);
    }
    if (dongle_udp != SOCKET_INVALID) {
        CLOSESOCKET(dongle_udp);
    }
    return EXIT_FAILURE;
}

static bool xml_attribute(const char *line,
                          const char *attribute,
                          char *value,
                          size_t value_capacity)
{
    char needle[64];
    const char *start;
    const char *end;
    size_t length;

    if (snprintf(needle, sizeof(needle), "%s=\"", attribute) < 0) {
        return false;
    }
    start = strstr(line, needle);
    if (start == NULL) {
        return false;
    }
    start += strlen(needle);
    end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }
    length = (size_t)(end - start);
    if (length + 1 > value_capacity) {
        return false;
    }
    memcpy(value, start, length);
    value[length] = '\0';
    return true;
}

static void xml_decode(char *text)
{
    struct entity {
        const char *encoded;
        char decoded;
    };
    static const struct entity entities[] = {
        { "&amp;", '&' },
        { "&apos;", '\'' },
        { "&quot;", '"' },
        { "&lt;", '<' },
        { "&gt;", '>' }
    };
    size_t i;

    for (i = 0; i < sizeof(entities) / sizeof(entities[0]); ++i) {
        const size_t encoded_length = strlen(entities[i].encoded);
        char *found;
        while ((found = strstr(text, entities[i].encoded)) != NULL) {
            *found = entities[i].decoded;
            memmove(found + 1,
                    found + encoded_length,
                    strlen(found + encoded_length) + 1);
        }
    }
}

static bool parse_channel_line(const char *line, channel_t *channel)
{
    char text[256];
    char polarization;
    uint32_t number;

    memset(channel, 0, sizeof(*channel));
    if (strstr(line, "<ch ") == NULL) {
        return false;
    }

    if (!xml_attribute(line, "type", channel->type, sizeof(channel->type)) ||
        !xml_attribute(line, "pol", text, sizeof(text)) ||
        (text[0] != 'H' && text[0] != 'V')) {
        return false;
    }
    polarization = text[0];

    if (!xml_attribute(line, "sym", text, sizeof(text)) ||
        !parse_u32(text, &channel->symbol_rate_ks) ||
        !xml_attribute(line, "s_id", text, sizeof(text)) ||
        !parse_u32(text, &channel->service_id) ||
        !xml_attribute(line, "freq", text, sizeof(text)) ||
        !parse_u32(text, &channel->frequency_mhz) ||
        !xml_attribute(line, "lcn", text, sizeof(text)) ||
        !parse_u32(text, &channel->lcn) ||
        !xml_attribute(line, "s_name", channel->name, sizeof(channel->name))) {
        return false;
    }

    channel->polarization = polarization;
    xml_decode(channel->name);
    if (xml_attribute(line, "fta", text, sizeof(text)) &&
        parse_u32(text, &number)) {
        channel->fta = number != 0;
    }
    return true;
}

static void free_channel_list(channel_list_t *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static bool load_channel_list(const char *path, channel_list_t *list)
{
    FILE *file;
    char line[2048];
    size_t capacity = 0;
    size_t line_number = 0;

    memset(list, 0, sizeof(*list));
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Cannot open channel list '%s': %s\n",
                path, strerror(errno));
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        channel_t channel;
        ++line_number;
        if (strstr(line, "<ch ") == NULL) {
            continue;
        }
        if (!parse_channel_line(line, &channel)) {
            fprintf(stderr, "Warning: skipped malformed channel at line %zu.\n",
                    line_number);
            continue;
        }
        if (list->count == capacity) {
            const size_t new_capacity = capacity == 0 ? 128 : capacity * 2;
            channel_t *new_items = (channel_t *)realloc(
                list->items, new_capacity * sizeof(*new_items));
            if (new_items == NULL) {
                fprintf(stderr, "Out of memory while reading channels.\n");
                fclose(file);
                free_channel_list(list);
                return false;
            }
            list->items = new_items;
            capacity = new_capacity;
        }
        list->items[list->count++] = channel;
    }

    fclose(file);
    if (list->count == 0) {
        fprintf(stderr, "No usable channels found in '%s'.\n", path);
        free_channel_list(list);
        return false;
    }

    printf("Loaded %zu channels from %s.\n", list->count, path);
    return true;
}

static const channel_t *find_channel_by_lcn(const channel_list_t *list,
                                            uint32_t lcn)
{
    size_t i;
    for (i = 0; i < list->count; ++i) {
        if (list->items[i].lcn == lcn) {
            return &list->items[i];
        }
    }
    return NULL;
}

static bool same_transponder(const tuning_state_t *state,
                             const channel_t *channel)
{
    return state->valid &&
           state->frequency_mhz == channel->frequency_mhz &&
           state->symbol_rate_ks == channel->symbol_rate_ks &&
           state->polarization == channel->polarization;
}

static bool tune_channel(socket_t control,
                         const options_t *base_opt,
                         const channel_t *channel,
                         tuning_state_t *state)
{
    options_t tune_opt = *base_opt;
    uint8_t frame[FRAME_SIZE];
    uint8_t response[64];
    const bool service_only = same_transponder(state, channel);

    tune_opt.tune_mode = TUNE_DVBS;
    tune_opt.frequency_mhz = channel->frequency_mhz;
    tune_opt.symbol_rate_ks = channel->symbol_rate_ks;
    tune_opt.polarization = channel->polarization;
    tune_opt.service_id = channel->service_id;

    printf("\nChannel %u: %s [%s]\n",
           (unsigned)channel->lcn, channel->name, channel->type);

    if (service_only) {
        printf("Same transponder; selecting SID %u.\n",
               (unsigned)channel->service_id);
    } else {
        printf("Retuning to %u MHz, %u kSym/s, %c; SID %u.\n",
               (unsigned)channel->frequency_mhz,
               (unsigned)channel->symbol_rate_ks,
               channel->polarization,
               (unsigned)channel->service_id);
        build_dvbs_tune(frame, &tune_opt);
        if (!tcp_command(control, frame, 2,
                         response, sizeof(response), base_opt->verbose)) {
            state->valid = false;
            return false;
        }
        SLEEP_MS(300);
    }

    build_service_id_tune(frame, channel->service_id);
    if (!tcp_command(control, frame, 4,
                     response, sizeof(response), base_opt->verbose)) {
        state->valid = false;
        return false;
    }
    if (!response_reports_lock(response, 4, "Channel tune")) {
        state->valid = false;
        return false;
    }

    state->valid = true;
    state->frequency_mhz = channel->frequency_mhz;
    state->symbol_rate_ks = channel->symbol_rate_ks;
    state->polarization = channel->polarization;
    state->service_id = channel->service_id;

    SLEEP_MS(base_opt->wait_ms);
    return true;
}

static int wait_socket_readable(socket_t sock, int timeout_ms)
{
    fd_set read_set;
    struct timeval timeout;
    int result;

    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    result = select(0, &read_set, NULL, NULL, &timeout);
#else
    result = select(sock + 1, &read_set, NULL, NULL, &timeout);
#endif
    return result;
}

static bool socket_peer_closed(socket_t sock)
{
    char byte;
    int ready = wait_socket_readable(sock, 0);
    if (ready <= 0) {
        return false;
    }
    return recv(sock, &byte, 1, MSG_PEEK) <= 0;
}

static socket_t create_http_listener(const char *bind_ip, uint16_t port)
{
    socket_t listener;
    struct sockaddr_in address;
    int reuse = 1;

    if (!parse_ipv4(bind_ip, &address, port)) {
        fprintf(stderr, "Invalid HTTP bind IPv4 address: %s\n", bind_ip);
        return SOCKET_INVALID;
    }

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == SOCKET_INVALID) {
        fprintf(stderr, "HTTP socket() failed: %d\n", SOCKET_ERROR_CODE());
        return SOCKET_INVALID;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof(reuse));
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "HTTP bind to %s:%u failed: %d\n",
                bind_ip, (unsigned)port, SOCKET_ERROR_CODE());
        CLOSESOCKET(listener);
        return SOCKET_INVALID;
    }
    if (listen(listener, 8) != 0) {
        fprintf(stderr, "HTTP listen() failed: %d\n", SOCKET_ERROR_CODE());
        CLOSESOCKET(listener);
        return SOCKET_INVALID;
    }
    return listener;
}

static bool send_client_all(socket_t client,
                            const uint8_t *data,
                            size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        int result = send(client,
                          (const char *)data + sent,
                          (int)(length - sent),
                          0);
        if (result <= 0) {
            return false;
        }
        sent += (size_t)result;
    }
    return true;
}

static bool send_client_text(socket_t client, const char *text)
{
    return send_client_all(client, (const uint8_t *)text, strlen(text));
}

static void send_http_error(socket_t client,
                            int status,
                            const char *reason,
                            const char *message)
{
    char header[512];
    const size_t length = strlen(message);
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/plain; charset=utf-8\r\n"
             "Content-Length: %zu\r\n"
             "Cache-Control: no-store\r\n"
             "Connection: close\r\n\r\n",
             status, reason, length);
    send_client_text(client, header);
    send_client_text(client, message);
}

static bool read_http_request(socket_t client,
                              char *request,
                              size_t capacity)
{
    size_t used = 0;

    while (used + 1 < capacity) {
        int result = recv(client, request + used,
                          (int)(capacity - used - 1), 0);
        if (result <= 0) {
            return false;
        }
        used += (size_t)result;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL ||
            strstr(request, "\n\n") != NULL) {
            return true;
        }
    }
    return false;
}

static bool get_http_path(const char *request,
                          char *path,
                          size_t path_capacity)
{
    const char *space1;
    const char *space2;
    size_t length;

    if (strncmp(request, "GET ", 4) != 0) {
        return false;
    }
    space1 = request + 4;
    space2 = strchr(space1, ' ');
    if (space2 == NULL) {
        return false;
    }
    length = (size_t)(space2 - space1);
    if (length == 0 || length + 1 > path_capacity) {
        return false;
    }
    memcpy(path, space1, length);
    path[length] = '\0';
    return true;
}

static void get_http_host(const char *request,
                          const options_t *opt,
                          char *host,
                          size_t host_capacity)
{
    const char *line = request;
    const char *host_start = NULL;
    const char *host_end;

    while ((line = strstr(line, "\n")) != NULL) {
        ++line;
        if ((line[0] == 'H' || line[0] == 'h') &&
            (line[1] == 'o' || line[1] == 'O') &&
            (line[2] == 's' || line[2] == 'S') &&
            (line[3] == 't' || line[3] == 'T') &&
            line[4] == ':') {
            host_start = line + 5;
            while (*host_start == ' ' || *host_start == '\t') {
                ++host_start;
            }
            break;
        }
    }

    if (host_start != NULL) {
        size_t length;
        host_end = strpbrk(host_start, "\r\n");
        if (host_end == NULL) {
            host_end = host_start + strlen(host_start);
        }
        length = (size_t)(host_end - host_start);
        if (length > 0 && length + 1 <= host_capacity) {
            memcpy(host, host_start, length);
            host[length] = '\0';
            return;
        }
    }

    snprintf(host, host_capacity, "%s:%u",
             strcmp(opt->http_bind_ip, "0.0.0.0") == 0
                 ? "127.0.0.1" : opt->http_bind_ip,
             (unsigned)opt->http_port);
}

static bool playlist_type_matches(const channel_t *channel,
                                  const char *filter)
{
    return filter == NULL || strcmp(channel->type, filter) == 0;
}

static void serve_playlist(socket_t client,
                           const channel_list_t *channels,
                           const options_t *opt,
                           const char *request,
                           const char *filter)
{
    char host[256];
    size_t i;

    get_http_host(request, opt, host, sizeof(host));
    if (!send_client_text(client,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: audio/x-mpegurl; charset=utf-8\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Connection: close\r\n\r\n"
            "#EXTM3U\r\n")) {
        return;
    }

    for (i = 0; i < channels->count; ++i) {
        const channel_t *channel = &channels->items[i];
        char entry[1024];
        int length;

        if (!playlist_type_matches(channel, filter)) {
            continue;
        }
        length = snprintf(entry, sizeof(entry),
             "#EXTINF:-1 tvg-chno=\"%u\" group-title=\"%s\",%u - %s\r\n"
             "#EXTVLCOPT:network-caching=1000\r\n"
             "http://%s/channel/%u\r\n",
             (unsigned)channel->lcn,
             channel->type,
             (unsigned)channel->lcn,
             channel->name,
             host,
             (unsigned)channel->lcn);
        if (length < 0 || (size_t)length >= sizeof(entry) ||
            !send_client_all(client, (const uint8_t *)entry, (size_t)length)) {
            return;
        }
    }
}

static size_t forward_ts_http(socket_t client,
                              const aes128_ctx_t *aes,
                              const uint8_t *data,
                              size_t length,
                              uint64_t *dropped_pid_1ffe,
                              uint64_t *decrypted_packets,
                              uint64_t *odd_key_packets,
                              uint64_t *invalid_packets,
                              bool *client_connected)
{
    uint8_t chunk[VLC_CHUNK_SIZE];
    uint8_t transformed[TS_PACKET_SIZE];
    size_t chunk_used = 0;
    size_t pos;
    size_t forwarded = 0;

    for (pos = 0; pos + TS_PACKET_SIZE <= length; pos += TS_PACKET_SIZE) {
        const uint8_t *packet = data + pos;

        if (!transform_ts_packet(aes, packet, transformed,
                                 decrypted_packets, odd_key_packets,
                                 invalid_packets)) {
            continue;
        }
        if (ts_pid(transformed) == 0x1FFE) {
            ++(*dropped_pid_1ffe);
            continue;
        }

        memcpy(chunk + chunk_used, transformed, TS_PACKET_SIZE);
        chunk_used += TS_PACKET_SIZE;
        if (chunk_used == sizeof(chunk)) {
            if (!send_client_all(client, chunk, chunk_used)) {
                *client_connected = false;
                return forwarded;
            }
            forwarded += chunk_used;
            chunk_used = 0;
        }
    }

    if (chunk_used > 0) {
        if (!send_client_all(client, chunk, chunk_used)) {
            *client_connected = false;
            return forwarded;
        }
        forwarded += chunk_used;
    }
    return forwarded;
}

static int stream_channel_http(socket_t control,
                               socket_t client,
                               const options_t *opt)
{
    socket_t dongle_udp = SOCKET_INVALID;
    struct sockaddr_in dongle_address;
    uint8_t request[FRAME_SIZE];
    uint8_t reply[65536];
    uint8_t retry_flag = 0;
    uint64_t blocks = 0;
    uint64_t bytes_forwarded = 0;
    uint64_t timeouts = 0;
    uint64_t bad_blocks = 0;
    uint64_t dropped_pid_1ffe = 0;
    uint64_t decrypted_packets = 0;
    uint64_t odd_key_packets = 0;
    uint64_t invalid_packets = 0;
    aes128_ctx_t aes;
    bool client_connected = true;
    static const uint8_t device_default_key[16] = {
        0x12,0x34,0x56,0x78,0x80,0xab,0xcd,0xef,
        0x12,0x34,0x56,0x78,0x80,0xab,0xcd,0xef
    };
    double report_start = monotonic_seconds();
    double last_heartbeat = report_start;

    if (!aes128_self_test()) {
        fprintf(stderr, "Internal AES self-test failed.\n");
        return EXIT_FAILURE;
    }
    aes128_init(&aes, device_default_key);

    if (!parse_ipv4(opt->device_ip, &dongle_address, STREAM_PORT)) {
        return EXIT_FAILURE;
    }
    dongle_udp = create_udp_socket(opt->timeout_ms);
    if (dongle_udp == SOCKET_INVALID) {
        return EXIT_FAILURE;
    }

    if (!send_client_text(client,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: video/MP2T\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Connection: close\r\n\r\n")) {
        CLOSESOCKET(dongle_udp);
        return EXIT_SUCCESS;
    }

    printf("Streaming to VLC HTTP client. Select another playlist item "
           "to change channel.\n");

    while (g_running && client_connected) {
        socklen_t source_length = (socklen_t)sizeof(dongle_address);
        struct sockaddr_in source;
        int received;
        int offset;
        size_t available;
        size_t aligned_length;
        size_t forwarded;
        double now = monotonic_seconds();

        if (socket_peer_closed(client)) {
            client_connected = false;
            break;
        }

        if (now - last_heartbeat >= 5.0) {
            if (!send_heartbeat(control, opt->verbose)) {
                CLOSESOCKET(dongle_udp);
                return EXIT_FAILURE;
            }
            last_heartbeat = monotonic_seconds();
        }

        build_ts_request(request, retry_flag);
        if (sendto(dongle_udp,
                   (const char *)request, (int)sizeof(request), 0,
                   (const struct sockaddr *)&dongle_address,
                   sizeof(dongle_address)) < 0) {
            fprintf(stderr, "Dongle UDP send failed: %d\n",
                    SOCKET_ERROR_CODE());
            CLOSESOCKET(dongle_udp);
            return EXIT_FAILURE;
        }

        memset(&source, 0, sizeof(source));
        received = recvfrom(dongle_udp, (char *)reply, (int)sizeof(reply), 0,
                            (struct sockaddr *)&source, &source_length);
        if (received < 0) {
            ++timeouts;
            retry_flag = 1;
            if (timeouts <= 3 || (timeouts % 20) == 0) {
                fprintf(stderr, "No TS reply yet (timeout %llu).\n",
                        (unsigned long long)timeouts);
            }
            continue;
        }
        retry_flag = 0;

        if (received < TS_BLOCK_SIZE) {
            ++bad_blocks;
            continue;
        }
        offset = find_ts_offset(reply, TS_BLOCK_SIZE);
        if (offset < 0) {
            ++bad_blocks;
            continue;
        }
        available = TS_BLOCK_SIZE - (size_t)offset;
        aligned_length = available - (available % TS_PACKET_SIZE);
        forwarded = forward_ts_http(client, &aes,
                                    reply + offset, aligned_length,
                                    &dropped_pid_1ffe,
                                    &decrypted_packets,
                                    &odd_key_packets,
                                    &invalid_packets,
                                    &client_connected);
        bytes_forwarded += forwarded;
        ++blocks;

        now = monotonic_seconds();
        if (now - report_start >= 2.0) {
            const double elapsed = now - report_start;
            const double mbps = elapsed > 0.0
                ? (double)bytes_forwarded * 8.0 / elapsed / 1000000.0
                : 0.0;
            printf("\rHTTP blocks: %llu  Output: %.2f Mbit/s  "
                   "Timeouts: %llu  Bad: %llu      ",
                   (unsigned long long)blocks,
                   mbps,
                   (unsigned long long)timeouts,
                   (unsigned long long)(bad_blocks + invalid_packets));
            fflush(stdout);
            bytes_forwarded = 0;
            report_start = now;
        }
    }

    printf("\nVLC closed this channel stream.\n");
    CLOSESOCKET(dongle_udp);
    return EXIT_SUCCESS;
}

static int run_http_server(socket_t control, const options_t *opt)
{
    channel_list_t channels;
    tuning_state_t tuning;
    socket_t listener = SOCKET_INVALID;
    double last_heartbeat = monotonic_seconds();
    int result = EXIT_SUCCESS;

    memset(&tuning, 0, sizeof(tuning));
    if (!load_channel_list(opt->channels_path, &channels)) {
        return EXIT_FAILURE;
    }
    if (!configure_satellite(control, opt)) {
        free_channel_list(&channels);
        return EXIT_FAILURE;
    }

    listener = create_http_listener(opt->http_bind_ip, opt->http_port);
    if (listener == SOCKET_INVALID) {
        free_channel_list(&channels);
        return EXIT_FAILURE;
    }

    printf("\nSORALink VLC channel server is ready.\n");
    printf("Open this URL in VLC:\n");
    printf("  http://%s:%u/playlist.m3u\n",
           strcmp(opt->http_bind_ip, "0.0.0.0") == 0
               ? "127.0.0.1" : opt->http_bind_ip,
           (unsigned)opt->http_port);
    printf("TV only:    http://%s:%u/tv.m3u\n",
           strcmp(opt->http_bind_ip, "0.0.0.0") == 0
               ? "127.0.0.1" : opt->http_bind_ip,
           (unsigned)opt->http_port);
    printf("Radio only: http://%s:%u/radio.m3u\n",
           strcmp(opt->http_bind_ip, "0.0.0.0") == 0
               ? "127.0.0.1" : opt->http_bind_ip,
           (unsigned)opt->http_port);
    printf("Press Ctrl+C to stop.\n\n");

    while (g_running) {
        socket_t client;
        struct sockaddr_in client_address;
        socklen_t client_length = (socklen_t)sizeof(client_address);
        char request[8192];
        char path[1024];
        int ready;
        double now = monotonic_seconds();

        if (now - last_heartbeat >= 5.0) {
            if (!send_heartbeat(control, opt->verbose)) {
                result = EXIT_FAILURE;
                break;
            }
            last_heartbeat = monotonic_seconds();
        }

        ready = wait_socket_readable(listener, 1000);
        if (ready < 0) {
            if (!g_running) {
                break;
            }
            fprintf(stderr, "HTTP select() failed: %d\n", SOCKET_ERROR_CODE());
            result = EXIT_FAILURE;
            break;
        }
        if (ready == 0) {
            continue;
        }

        client = accept(listener,
                        (struct sockaddr *)&client_address,
                        &client_length);
        if (client == SOCKET_INVALID) {
            fprintf(stderr, "HTTP accept() failed: %d\n", SOCKET_ERROR_CODE());
            continue;
        }
        socket_set_timeout(client, opt->timeout_ms);

        if (!read_http_request(client, request, sizeof(request)) ||
            !get_http_path(request, path, sizeof(path))) {
            send_http_error(client, 400, "Bad Request", "Bad HTTP request.\n");
            CLOSESOCKET(client);
            continue;
        }

        if (strcmp(path, "/playlist.m3u") == 0 ||
            strcmp(path, "/") == 0) {
            serve_playlist(client, &channels, opt, request, NULL);
        } else if (strcmp(path, "/tv.m3u") == 0) {
            serve_playlist(client, &channels, opt, request, "TV");
        } else if (strcmp(path, "/radio.m3u") == 0) {
            serve_playlist(client, &channels, opt, request, "RADIO");
        } else if (strncmp(path, "/channel/", 9) == 0) {
            uint32_t lcn;
            const channel_t *channel;
            char *query = strchr(path + 9, '?');
            if (query != NULL) {
                *query = '\0';
            }
            if (!parse_u32(path + 9, &lcn) ||
                (channel = find_channel_by_lcn(&channels, lcn)) == NULL) {
                send_http_error(client, 404, "Not Found",
                                "Unknown channel number.\n");
                CLOSESOCKET(client);
                continue;
            }
            if (!tune_channel(control, opt, channel, &tuning)) {
                send_http_error(client, 503, "Service Unavailable",
                                "SORALink could not lock this channel.\n");
                CLOSESOCKET(client);
                continue;
            }
            result = stream_channel_http(control, client, opt);
            last_heartbeat = monotonic_seconds();
            if (result != EXIT_SUCCESS) {
                CLOSESOCKET(client);
                break;
            }
        } else {
            send_http_error(client, 404, "Not Found",
                            "Use /playlist.m3u, /tv.m3u, or /radio.m3u.\n");
        }
        CLOSESOCKET(client);
    }

    if (listener != SOCKET_INVALID) {
        CLOSESOCKET(listener);
    }
    free_channel_list(&channels);
    return result;
}

int main(int argc, char **argv)
{
    options_t opt;
    socket_t control = SOCKET_INVALID;
    int result = EXIT_FAILURE;

#ifdef _WIN32
    WSADATA winsock;
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return EXIT_FAILURE;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
#endif

    if (!parse_options(argc, argv, &opt)) {
        print_usage(argv[0]);
        goto cleanup;
    }

    printf("Connecting to Device at %s:%d...\n",
           opt.device_ip, CONTROL_PORT);

    control = connect_tcp(opt.device_ip, CONTROL_PORT, opt.timeout_ms);
    if (control == SOCKET_INVALID) {
        fprintf(stderr,
            "\nPort 8802 is not reachable. Confirm the IP address and "
            "fully close Device.\n");
        goto cleanup;
    }

    printf("TCP control connection established.\n");

    if (!acquire_permission(control, opt.verbose)) {
        goto cleanup;
    }

    if (opt.probe_only) {
        printf("Probe completed successfully.\n");
        result = EXIT_SUCCESS;
        goto cleanup;
    }

    if (opt.http_server) {
        result = run_http_server(control, &opt);
        goto cleanup;
    }

    if (!tune_device(control, &opt)) {
        goto cleanup;
    }

    result = stream_to_vlc(control, &opt);

cleanup:
    if (control != SOCKET_INVALID) {
        CLOSESOCKET(control);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}

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
#include <limits.h>

#ifdef SORALINK_USE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
typedef int socket_io_t;
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
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
typedef int socket_t;
typedef ssize_t socket_io_t;
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

#ifdef _WIN32
#define STRNCASECMP _strnicmp
#else
#include <strings.h>
#define STRNCASECMP strncasecmp
#endif

#define DEFAULT_CONTROL_PORT 8802
#define DEFAULT_STREAM_PORT  8800
#define HTTP_CLIENT_LIMIT     64
#define XML_FILE_LIMIT        (64U * 1024U * 1024U)

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

typedef enum {
    MISSING_KEY_PASS = 0,
    MISSING_KEY_DROP
} missing_key_policy_t;

typedef struct {
    struct sockaddr_storage address;
    socklen_t length;
    int family;
} endpoint_t;

typedef struct {
    const char *device_ip;
    uint16_t control_port;
    uint16_t stream_port;
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

    uint8_t even_key[16];
    uint8_t odd_key[16];
    bool have_even_key;
    bool have_odd_key;
    missing_key_policy_t missing_key_policy;

    const char *dump_path;
    int wait_ms;
    int timeout_ms;
    bool probe_only;
    bool self_test;
    bool verbose;

    bool http_server;
    const char *channels_path;
    const char *http_bind_ip;
    uint16_t http_port;
    uint32_t max_http_clients;
    const char *http_user;
    const char *http_password;
    const char *tls_cert_path;
    const char *tls_key_path;
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

static const uint8_t device_default_key[16] = {
    0x12,0x34,0x56,0x78,0x80,0xab,0xcd,0xef,
    0x12,0x34,0x56,0x78,0x80,0xab,0xcd,0xef
};

static void print_usage(const char *program)
{
    fprintf(stderr,
        "SORALink -> VLC bridge\n\n"
        "Playlist-server mode:\n"
        "  %s --device HOST --server --channels channels.xml [options]\n\n"
        "Single-channel UDP modes:\n"
        "  %s --device HOST --progidx N [options]\n"
        "  %s --device HOST --freq MHz --sr kSym/s --pol H|V|F|O --sid N [options]\n"
        "  %s --device HOST --probe [options]\n"
        "  %s --self-test\n\n"
        "Server example:\n"
        "  %s --device soralink.local --server --channels channels.xml\n"
        "  Then open in VLC: http://127.0.0.1:8080/playlist.m3u\n\n"
        "Device and tuning options:\n"
        "  --device HOST        Device DNS name, IPv4, or IPv6 address\n"
        "  --control-port N     Device TCP control port (default 8802)\n"
        "  --stream-port N      Device UDP stream port (default 8800)\n"
        "  --progidx N          Tune saved channel/program index N\n"
        "  --freq MHz           Satellite transponder frequency in MHz\n"
        "  --sr N               Symbol rate in kSym/s, e.g. 22000 or 27500\n"
        "  --pol H|V|F|O        Polarization/voltage mode; F/O are device-specific\n"
        "  --sid N              DVB service ID\n"
        "  --orbital N          Orbital position in tenths of a degree (default 192)\n"
        "  --east | --west      Orbital direction (default east)\n"
        "  --tone auto|on|off   22 kHz tone mode (default auto)\n"
        "  --lnb-low N          Universal-LNB low LO in MHz (default 9750)\n"
        "  --lnb-high N         Universal-LNB high LO in MHz (default 10600)\n"
        "  --lnb-switch N       LNB switch frequency in MHz (default 11700)\n"
        "  --diseqc N           DiSEqC input value (default 0 = none)\n"
        "  --sat-setup          Send the extended LNB/DiSEqC setup sequence\n"
        "  --no-sat-setup       Skip extended setup (default)\n\n"
        "Stream-key options:\n"
        "  --even-key HEX       16-byte even AES key (32 hexadecimal digits)\n"
        "  --odd-key HEX        16-byte odd AES key (32 hexadecimal digits)\n"
        "  --no-default-key     Do not use the legacy built-in even key\n"
        "  --missing-key MODE   pass or drop encrypted packets (default pass)\n\n"
        "HTTP server options:\n"
        "  --server             Run the local HTTP playlist/stream server\n"
        "  --channels FILE      Channel XML file (default channels.xml)\n"
        "  --http-bind HOST     Listen address/name (default 127.0.0.1)\n"
        "  --http-port N        HTTP listen port (default 8080)\n"
        "  --max-clients N      Simultaneous viewers of one channel (default 8)\n"
        "  --http-user USER     Enable HTTP Basic authentication\n"
        "  --http-password PASS Password for HTTP Basic authentication\n"
        "  --tls-cert FILE      TLS certificate PEM (requires OpenSSL build)\n"
        "  --tls-key FILE       TLS private-key PEM (requires OpenSSL build)\n\n"
        "Other options:\n"
        "  --vlc-ip HOST        UDP-mode VLC DNS/IPv4/IPv6 destination\n"
        "  --vlc-port N         UDP-mode VLC port (default 1234)\n"
        "  --dump FILE.ts       UDP mode: also save transport stream\n"
        "  --wait-ms N          Delay after tuning (default 800)\n"
        "  --timeout-ms N       Network timeout (default 3000)\n"
        "  --probe              Test permission handshake only\n"
        "  --self-test          Run internal AES, TS, XML, and HTTP tests\n"
        "  --verbose            Print protocol frames and diagnostics\n"
        "  --help               Show this help\n\n"
        "Server endpoints:\n"
        "  /playlist.m3u        TV and radio playlist\n"
        "  /tv.m3u              TV-only playlist\n"
        "  /radio.m3u           Radio-only playlist\n"
        "  /status.json         Current channel/client status\n",
        program, program, program, program, program, program);
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
    if (!parse_u32(text, &value) || value == 0 || value > 65535U) {
        return false;
    }
    *port = (uint16_t)value;
    return true;
}

static int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool parse_hex_key(const char *text, uint8_t key[16])
{
    size_t out = 0;
    int high = -1;

    if (text == NULL) {
        return false;
    }
    while (*text != '\0') {
        const int value = hex_nibble(*text++);
        if (value < 0) {
            if (text[-1] == ':' || text[-1] == '-' ||
                text[-1] == ' ' || text[-1] == '\t') {
                continue;
            }
            return false;
        }
        if (high < 0) {
            high = value;
        } else {
            if (out >= 16U) {
                return false;
            }
            key[out++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    return out == 16U && high < 0;
}

static bool parse_polarization(const char *text, char *polarization)
{
    char value;
    if (text == NULL || text[0] == '\0' || text[1] != '\0') {
        return false;
    }
    value = (char)toupper((unsigned char)text[0]);
    if (value != 'H' && value != 'V' && value != 'F' && value != 'O') {
        return false;
    }
    *polarization = value;
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
    opt->control_port = DEFAULT_CONTROL_PORT;
    opt->stream_port = DEFAULT_STREAM_PORT;
    opt->vlc_ip = "127.0.0.1";
    opt->vlc_port = 1234;
    opt->orbital_tenths = 192;
    opt->tone = TONE_AUTO;
    opt->lnb_low_mhz = 9750;
    opt->lnb_high_mhz = 10600;
    opt->lnb_switch_mhz = 11700;
    opt->diseqc_port = 0;
    opt->satellite_setup = false;
    memcpy(opt->even_key, device_default_key, sizeof(opt->even_key));
    opt->have_even_key = true;
    opt->missing_key_policy = MISSING_KEY_PASS;
    opt->wait_ms = 800;
    opt->timeout_ms = 3000;
    opt->program_index = -1;
    opt->channels_path = "channels.xml";
    opt->http_bind_ip = "127.0.0.1";
    opt->http_port = 8080;
    opt->max_http_clients = 8;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (strcmp(arg, "--self-test") == 0) {
            opt->self_test = true;
        } else if (strcmp(arg, "--device") == 0 && i + 1 < argc) {
            opt->device_ip = argv[++i];
        } else if (strcmp(arg, "--control-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &opt->control_port)) {
                fprintf(stderr, "Invalid --control-port value.\n");
                return false;
            }
        } else if (strcmp(arg, "--stream-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &opt->stream_port)) {
                fprintf(stderr, "Invalid --stream-port value.\n");
                return false;
            }
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
        } else if (strcmp(arg, "--max-clients") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->max_http_clients) ||
                opt->max_http_clients == 0 ||
                opt->max_http_clients > HTTP_CLIENT_LIMIT) {
                fprintf(stderr, "--max-clients must be between 1 and %u.\n",
                        (unsigned)HTTP_CLIENT_LIMIT);
                return false;
            }
        } else if (strcmp(arg, "--http-user") == 0 && i + 1 < argc) {
            opt->http_user = argv[++i];
        } else if (strcmp(arg, "--http-password") == 0 && i + 1 < argc) {
            opt->http_password = argv[++i];
        } else if (strcmp(arg, "--tls-cert") == 0 && i + 1 < argc) {
            opt->tls_cert_path = argv[++i];
        } else if (strcmp(arg, "--tls-key") == 0 && i + 1 < argc) {
            opt->tls_key_path = argv[++i];
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
            if (!parse_polarization(argv[++i], &opt->polarization)) {
                fprintf(stderr, "--pol must be H, V, F, or O.\n");
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
                opt->orbital_tenths > 32767U) {
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
                opt->lnb_low_mhz > 65535U) {
                fprintf(stderr, "Invalid --lnb-low value.\n");
                return false;
            }
        } else if (strcmp(arg, "--lnb-high") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->lnb_high_mhz) ||
                opt->lnb_high_mhz > 65535U) {
                fprintf(stderr, "Invalid --lnb-high value.\n");
                return false;
            }
        } else if (strcmp(arg, "--lnb-switch") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->lnb_switch_mhz) ||
                opt->lnb_switch_mhz > 65535U) {
                fprintf(stderr, "Invalid --lnb-switch value.\n");
                return false;
            }
        } else if (strcmp(arg, "--diseqc") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->diseqc_port) ||
                opt->diseqc_port > 255U) {
                fprintf(stderr, "Invalid --diseqc value.\n");
                return false;
            }
        } else if (strcmp(arg, "--sat-setup") == 0) {
            opt->satellite_setup = true;
        } else if (strcmp(arg, "--no-sat-setup") == 0) {
            opt->satellite_setup = false;
        } else if (strcmp(arg, "--even-key") == 0 && i + 1 < argc) {
            if (!parse_hex_key(argv[++i], opt->even_key)) {
                fprintf(stderr, "Invalid --even-key; expected 32 hex digits.\n");
                return false;
            }
            opt->have_even_key = true;
        } else if (strcmp(arg, "--odd-key") == 0 && i + 1 < argc) {
            if (!parse_hex_key(argv[++i], opt->odd_key)) {
                fprintf(stderr, "Invalid --odd-key; expected 32 hex digits.\n");
                return false;
            }
            opt->have_odd_key = true;
        } else if (strcmp(arg, "--no-default-key") == 0) {
            memset(opt->even_key, 0, sizeof(opt->even_key));
            opt->have_even_key = false;
        } else if (strcmp(arg, "--missing-key") == 0 && i + 1 < argc) {
            const char *policy = argv[++i];
            if (strcmp(policy, "pass") == 0) {
                opt->missing_key_policy = MISSING_KEY_PASS;
            } else if (strcmp(policy, "drop") == 0) {
                opt->missing_key_policy = MISSING_KEY_DROP;
            } else {
                fprintf(stderr, "--missing-key must be pass or drop.\n");
                return false;
            }
        } else if (strcmp(arg, "--dump") == 0 && i + 1 < argc) {
            opt->dump_path = argv[++i];
        } else if (strcmp(arg, "--wait-ms") == 0 && i + 1 < argc) {
            uint32_t value;
            if (!parse_u32(argv[++i], &value) || value > 60000U) {
                fprintf(stderr, "Invalid --wait-ms value.\n");
                return false;
            }
            opt->wait_ms = (int)value;
        } else if (strcmp(arg, "--timeout-ms") == 0 && i + 1 < argc) {
            uint32_t value;
            if (!parse_u32(argv[++i], &value) || value < 100U ||
                value > 60000U) {
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

    if (opt->self_test) {
        return true;
    }
    if (opt->device_ip == NULL) {
        fprintf(stderr, "--device is required.\n");
        return false;
    }
    if ((opt->http_user == NULL) != (opt->http_password == NULL)) {
        fprintf(stderr, "--http-user and --http-password must be used together.\n");
        return false;
    }
    if ((opt->tls_cert_path == NULL) != (opt->tls_key_path == NULL)) {
        fprintf(stderr, "--tls-cert and --tls-key must be used together.\n");
        return false;
    }
#ifndef SORALINK_USE_OPENSSL
    if (opt->tls_cert_path != NULL) {
        fprintf(stderr,
                "TLS was requested, but this binary was built without OpenSSL. "
                "Rebuild with -DSORALINK_USE_OPENSSL and link libssl/libcrypto.\n");
        return false;
    }
#endif

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
                "Choose --progidx N, direct DVB-S parameters, --server, or --probe.\n");
            return false;
        }
    }

    if (opt->frequency_mhz > 65535U ||
        opt->symbol_rate_ks > 0xFFFFFFU) {
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

static socket_io_t socket_send_bytes(socket_t sock,
                                         const char *buffer,
                                         int length,
                                         int flags)
{
#ifdef _WIN32
    return send(sock, buffer, length, flags);
#else
    return send(sock, buffer, (size_t)length, flags);
#endif
}

static socket_io_t socket_recv_bytes(socket_t sock,
                                     char *buffer,
                                     int length,
                                     int flags)
{
#ifdef _WIN32
    return recv(sock, buffer, length, flags);
#else
    return recv(sock, buffer, (size_t)length, flags);
#endif
}

static socket_io_t socket_sendto_bytes(socket_t sock,
                                       const char *buffer,
                                       int length,
                                       int flags,
                                       const struct sockaddr *address,
                                       socklen_t address_length)
{
#ifdef _WIN32
    return sendto(sock, buffer, length, flags, address, address_length);
#else
    return sendto(sock, buffer, (size_t)length, flags,
                  address, address_length);
#endif
}

static socket_io_t socket_recvfrom_bytes(socket_t sock,
                                         char *buffer,
                                         int length,
                                         int flags,
                                         struct sockaddr *address,
                                         socklen_t *address_length)
{
#ifdef _WIN32
    return recvfrom(sock, buffer, length, flags, address, address_length);
#else
    return recvfrom(sock, buffer, (size_t)length, flags,
                    address, address_length);
#endif
}

static int socket_set_nonblocking(socket_t sock, bool enabled)
{
#ifdef _WIN32
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(sock, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(sock, F_SETFL,
                 enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

static bool connect_in_progress_error(int error_code)
{
#ifdef _WIN32
    return error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS ||
           error_code == WSAEINVAL;
#else
    return error_code == EINPROGRESS || error_code == EWOULDBLOCK;
#endif
}

static int wait_socket_writable(socket_t sock, int timeout_ms)
{
    fd_set write_set;
    struct timeval timeout;

    FD_ZERO(&write_set);
    FD_SET(sock, &write_set);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    return select(0, NULL, &write_set, NULL, &timeout);
#else
    return select(sock + 1, NULL, &write_set, NULL, &timeout);
#endif
}

static bool connect_with_timeout(socket_t sock,
                                 const struct sockaddr *address,
                                 socklen_t address_length,
                                 int timeout_ms)
{
    int error_code = 0;
    socklen_t error_length = (socklen_t)sizeof(error_code);

    if (socket_set_nonblocking(sock, true) != 0) {
        return false;
    }
    if (connect(sock, address, address_length) == 0) {
        socket_set_nonblocking(sock, false);
        return true;
    }
    if (!connect_in_progress_error(SOCKET_ERROR_CODE())) {
        socket_set_nonblocking(sock, false);
        return false;
    }
    if (wait_socket_writable(sock, timeout_ms) <= 0) {
        socket_set_nonblocking(sock, false);
        return false;
    }
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
#ifdef _WIN32
                   (char *)&error_code,
#else
                   &error_code,
#endif
                   &error_length) != 0 || error_code != 0) {
        socket_set_nonblocking(sock, false);
        return false;
    }
    return socket_set_nonblocking(sock, false) == 0;
}

static bool resolve_endpoint(const char *host,
                             uint16_t port,
                             int socket_type,
                             bool passive,
                             endpoint_t *endpoint)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *item;
    char service[16];
    int status;

    memset(endpoint, 0, sizeof(*endpoint));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socket_type;
    hints.ai_protocol = socket_type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    snprintf(service, sizeof(service), "%u", (unsigned)port);

    status = getaddrinfo((passive && host != NULL && *host == '\0') ? NULL : host,
                         service, &hints, &results);
    if (status != 0) {
#ifdef _WIN32
        fprintf(stderr, "Cannot resolve '%s': %d\n",
                host != NULL ? host : "*", status);
#else
        fprintf(stderr, "Cannot resolve '%s': %s\n",
                host != NULL ? host : "*", gai_strerror(status));
#endif
        return false;
    }

    for (item = results; item != NULL; item = item->ai_next) {
        if ((item->ai_family == AF_INET || item->ai_family == AF_INET6) &&
            item->ai_addrlen <= sizeof(endpoint->address)) {
            memcpy(&endpoint->address, item->ai_addr, item->ai_addrlen);
            endpoint->length = (socklen_t)item->ai_addrlen;
            endpoint->family = item->ai_family;
            freeaddrinfo(results);
            return true;
        }
    }
    freeaddrinfo(results);
    fprintf(stderr, "No usable IPv4 or IPv6 address found for '%s'.\n", host);
    return false;
}

static bool sockaddr_same_endpoint(const struct sockaddr *left,
                                   const struct sockaddr *right)
{
    if (left == NULL || right == NULL || left->sa_family != right->sa_family) {
        return false;
    }
    if (left->sa_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)left;
        const struct sockaddr_in *b = (const struct sockaddr_in *)right;
        return a->sin_port == b->sin_port &&
               memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr)) == 0;
    }
    if (left->sa_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)left;
        const struct sockaddr_in6 *b = (const struct sockaddr_in6 *)right;
        return a->sin6_port == b->sin6_port &&
               a->sin6_scope_id == b->sin6_scope_id &&
               memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
    }
    return false;
}

static bool endpoint_is_loopback(const endpoint_t *endpoint)
{
    if (endpoint->family == AF_INET) {
        const struct sockaddr_in *address =
            (const struct sockaddr_in *)&endpoint->address;
        return (ntohl(address->sin_addr.s_addr) >> 24) == 127U;
    }
    if (endpoint->family == AF_INET6) {
        const struct sockaddr_in6 *address =
            (const struct sockaddr_in6 *)&endpoint->address;
        return IN6_IS_ADDR_LOOPBACK(&address->sin6_addr) != 0;
    }
    return false;
}

static void format_host_for_url(const char *host,
                                char *output,
                                size_t output_capacity)
{
    if (host != NULL && strchr(host, ':') != NULL && host[0] != '[') {
        snprintf(output, output_capacity, "[%s]", host);
    } else {
        snprintf(output, output_capacity, "%s", host != NULL ? host : "localhost");
    }
}

static socket_t connect_tcp(const char *host, uint16_t port, int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *item;
    char service[16];
    int status;
    int last_error = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(service, sizeof(service), "%u", (unsigned)port);

    status = getaddrinfo(host, service, &hints, &results);
    if (status != 0) {
#ifdef _WIN32
        fprintf(stderr, "Cannot resolve device host '%s': %d\n", host, status);
#else
        fprintf(stderr, "Cannot resolve device host '%s': %s\n",
                host, gai_strerror(status));
#endif
        return SOCKET_INVALID;
    }

    for (item = results; item != NULL; item = item->ai_next) {
        socket_t sock;
        int keepalive = 1;
        int receive_buffer = 262144;

        if (item->ai_family != AF_INET && item->ai_family != AF_INET6) {
            continue;
        }
        sock = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (sock == SOCKET_INVALID) {
            last_error = SOCKET_ERROR_CODE();
            continue;
        }
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
                   (const char *)&keepalive, sizeof(keepalive));
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                   (const char *)&receive_buffer, sizeof(receive_buffer));

        if (connect_with_timeout(sock, item->ai_addr,
                                 (socklen_t)item->ai_addrlen, timeout_ms)) {
            socket_set_timeout(sock, timeout_ms);
            freeaddrinfo(results);
            return sock;
        }
        last_error = SOCKET_ERROR_CODE();
        CLOSESOCKET(sock);
    }

    freeaddrinfo(results);
    fprintf(stderr, "TCP connect to %s:%u failed: %d\n",
            host, (unsigned)port, last_error);
    return SOCKET_INVALID;
}

static bool send_all(socket_t sock, const uint8_t *data, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        const size_t remaining = length - sent;
        const int request_length =
            remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        const socket_io_t result = socket_send_bytes(sock, (const char *)data + sent,
                          request_length, 0);
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
        const size_t remaining = length - received;
        const int request_length =
            remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        const socket_io_t result = socket_recv_bytes(sock, (char *)data + received,
                          request_length, 0);
        if (result == 0) {
            fprintf(stderr, "TCP connection closed by device.\n");
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

static bool validate_csw(const uint8_t *response,
                         size_t response_length,
                         size_t data_length,
                         const uint8_t frame[FRAME_SIZE])
{
    const uint8_t *csw;

    if (response_length != data_length + CSW_SIZE) {
        fprintf(stderr, "Protocol error: incorrect response length.\n");
        return false;
    }
    csw = response + data_length;
    if (memcmp(csw, "USBS", 4) != 0) {
        fprintf(stderr, "Protocol error: missing USBS command-status trailer.\n");
        return false;
    }
    if (memcmp(csw + 4, frame + 4, 4) != 0) {
        fprintf(stderr, "Protocol error: response transaction tag mismatch.\n");
        return false;
    }
    if (csw[8] != 0 || csw[9] != 0 || csw[10] != 0 || csw[11] != 0) {
        fprintf(stderr, "Protocol error: device reported non-zero data residue.\n");
        return false;
    }
    if (csw[12] != 0) {
        fprintf(stderr, "Protocol error: command status 0x%02X.\n", csw[12]);
        return false;
    }
    return true;
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

    if (!send_all(sock, frame, FRAME_SIZE) ||
        !recv_all(sock, response, total)) {
        return false;
    }

    if (verbose) {
        hex_dump("TCP response", response, total);
    }

    return validate_csw(response, total, expected_data_length, frame);
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

static socket_t create_udp_socket(int family, int timeout_ms)
{
    socket_t sock;
    int reuse = 1;
    int buffer_size = 262144;

    sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
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
    const uint8_t shifted = (uint8_t)(value << 1);
    const uint8_t reduction = (value & 0x80U) != 0U ? 0x1BU : 0x00U;
    return (uint8_t)(shifted ^ reduction);
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

typedef struct {
    aes128_ctx_t even;
    aes128_ctx_t odd;
    bool have_even;
    bool have_odd;
    missing_key_policy_t missing_policy;
} stream_keys_t;

typedef struct {
    uint64_t dropped_pid_1ffe;
    uint64_t even_decrypted;
    uint64_t odd_decrypted;
    uint64_t missing_even_key;
    uint64_t missing_odd_key;
    uint64_t reserved_scrambling;
    uint64_t invalid_packets;
} stream_stats_t;

static void stream_keys_init(stream_keys_t *keys, const options_t *opt)
{
    memset(keys, 0, sizeof(*keys));
    keys->have_even = opt->have_even_key;
    keys->have_odd = opt->have_odd_key;
    keys->missing_policy = opt->missing_key_policy;
    if (keys->have_even) {
        aes128_init(&keys->even, opt->even_key);
    }
    if (keys->have_odd) {
        aes128_init(&keys->odd, opt->odd_key);
    }
}

static bool transform_ts_packet(const stream_keys_t *keys,
                                const uint8_t input[TS_PACKET_SIZE],
                                uint8_t output[TS_PACKET_SIZE],
                                stream_stats_t *stats)
{
    const aes128_ctx_t *aes = NULL;
    unsigned scrambling;
    unsigned adaptation_control;
    size_t payload_offset = 4U;
    size_t decrypt_length;
    size_t pos;

    memcpy(output, input, TS_PACKET_SIZE);
    if (input[0] != 0x47) {
        ++stats->invalid_packets;
        return false;
    }

    scrambling = (unsigned)((input[3] >> 6) & 0x03U);
    adaptation_control = (unsigned)((input[3] >> 4) & 0x03U);

    if (scrambling == 0U) {
        return true;
    }
    if (scrambling == 1U) {
        ++stats->reserved_scrambling;
        return false;
    }
    if (adaptation_control == 0U) {
        ++stats->invalid_packets;
        return false;
    }
    if ((adaptation_control & 1U) == 0U) {
        return true;
    }

    if (scrambling == 2U) {
        if (!keys->have_even) {
            ++stats->missing_even_key;
            return keys->missing_policy == MISSING_KEY_PASS;
        }
        aes = &keys->even;
    } else {
        if (!keys->have_odd) {
            ++stats->missing_odd_key;
            return keys->missing_policy == MISSING_KEY_PASS;
        }
        aes = &keys->odd;
    }

    if (adaptation_control == 3U) {
        payload_offset = 5U + (size_t)input[4];
        if (payload_offset > TS_PACKET_SIZE) {
            ++stats->invalid_packets;
            return false;
        }
    }

    decrypt_length = (TS_PACKET_SIZE - payload_offset) & ~(size_t)15U;
    if (decrypt_length == 0U) {
        return keys->missing_policy == MISSING_KEY_PASS;
    }

    for (pos = 0; pos < decrypt_length; pos += 16U) {
        aes128_decrypt_block(aes,
                             input + payload_offset + pos,
                             output + payload_offset + pos);
    }

    output[3] &= 0x3FU;
    if (scrambling == 2U) {
        ++stats->even_decrypted;
    } else {
        ++stats->odd_decrypted;
    }
    return true;
}

static size_t forward_ts(socket_t output,
                         const endpoint_t *vlc_endpoint,
                         FILE *dump,
                         const stream_keys_t *keys,
                         const uint8_t *data,
                         size_t length,
                         stream_stats_t *stats)
{
    uint8_t chunk[VLC_CHUNK_SIZE];
    uint8_t transformed[TS_PACKET_SIZE];
    size_t chunk_used = 0;
    size_t pos;
    size_t forwarded = 0;

    for (pos = 0; pos + TS_PACKET_SIZE <= length; pos += TS_PACKET_SIZE) {
        const uint8_t *packet = data + pos;

        if (!transform_ts_packet(keys, packet, transformed, stats)) {
            continue;
        }

        if (ts_pid(transformed) == 0x1FFE) {
            ++stats->dropped_pid_1ffe;
            continue;
        }

        memcpy(chunk + chunk_used, transformed, TS_PACKET_SIZE);
        chunk_used += TS_PACKET_SIZE;

        if (dump != NULL &&
            fwrite(transformed, 1, TS_PACKET_SIZE, dump) != TS_PACKET_SIZE) {
            fprintf(stderr, "Transport-stream dump write failed: %s\n",
                    strerror(errno));
        }

        if (chunk_used == sizeof(chunk)) {
            const socket_io_t result = socket_sendto_bytes(
                output, (const char *)chunk, (int)chunk_used, 0,
                (const struct sockaddr *)&vlc_endpoint->address,
                vlc_endpoint->length);
            if (result < 0) {
                fprintf(stderr, "VLC UDP send failed: %d\n",
                        SOCKET_ERROR_CODE());
                return forwarded;
            }
            forwarded += chunk_used;
            chunk_used = 0;
        }
    }

    if (chunk_used > 0U) {
        const socket_io_t result = socket_sendto_bytes(
            output, (const char *)chunk, (int)chunk_used, 0,
            (const struct sockaddr *)&vlc_endpoint->address,
            vlc_endpoint->length);
        if (result >= 0) {
            forwarded += chunk_used;
        } else {
            fprintf(stderr, "VLC UDP send failed: %d\n",
                    SOCKET_ERROR_CODE());
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

static bool reconnect_control_session(socket_t *control,
                                      const options_t *opt)
{
    unsigned attempt;

    if (*control != SOCKET_INVALID) {
        CLOSESOCKET(*control);
        *control = SOCKET_INVALID;
    }

    for (attempt = 1; attempt <= 3U && g_running; ++attempt) {
        fprintf(stderr, "Reconnecting device control session (attempt %u/3)...\n",
                attempt);
        *control = connect_tcp(opt->device_ip, opt->control_port,
                               opt->timeout_ms);
        if (*control != SOCKET_INVALID &&
            acquire_permission(*control, opt->verbose)) {
            fprintf(stderr, "Device control session restored.\n");
            return true;
        }
        if (*control != SOCKET_INVALID) {
            CLOSESOCKET(*control);
            *control = SOCKET_INVALID;
        }
        SLEEP_MS(250U * attempt);
    }
    return false;
}

static bool recover_udp_mode(socket_t *control, const options_t *opt)
{
    if (!reconnect_control_session(control, opt)) {
        return false;
    }
    if (!tune_device(*control, opt)) {
        fprintf(stderr, "Retuning after reconnect failed.\n");
        return false;
    }
    return true;
}

static int stream_to_vlc(socket_t *control, const options_t *opt)
{
    socket_t dongle_udp = SOCKET_INVALID;
    socket_t output_udp = SOCKET_INVALID;
    endpoint_t dongle_endpoint;
    endpoint_t vlc_endpoint;
    FILE *dump = NULL;
    uint8_t request[FRAME_SIZE];
    uint8_t reply[65536];
    uint8_t retry_flag = 0;
    uint64_t blocks = 0;
    uint64_t bytes_forwarded = 0;
    uint64_t timeouts = 0;
    uint64_t bad_blocks = 0;
    unsigned consecutive_timeouts = 0;
    stream_keys_t keys;
    stream_stats_t stats;
    double report_start = monotonic_seconds();
    double last_heartbeat = report_start;
    char vlc_url_host[512];

    memset(&stats, 0, sizeof(stats));
    if (!aes128_self_test()) {
        fprintf(stderr, "Internal AES self-test failed; refusing to stream.\n");
        return EXIT_FAILURE;
    }
    stream_keys_init(&keys, opt);

    if (!resolve_endpoint(opt->device_ip, opt->stream_port,
                          SOCK_DGRAM, false, &dongle_endpoint) ||
        !resolve_endpoint(opt->vlc_ip, opt->vlc_port,
                          SOCK_DGRAM, false, &vlc_endpoint)) {
        return EXIT_FAILURE;
    }

    dongle_udp = create_udp_socket(dongle_endpoint.family, opt->timeout_ms);
    output_udp = create_udp_socket(vlc_endpoint.family, opt->timeout_ms);
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

    format_host_for_url(opt->vlc_ip, vlc_url_host, sizeof(vlc_url_host));
    printf("Forwarding MPEG-TS to udp://@%s:%u\n",
           vlc_url_host, (unsigned)opt->vlc_port);
    printf("Keys: even=%s, odd=%s; missing-key policy=%s.\n",
           keys.have_even ? "configured" : "absent",
           keys.have_odd ? "configured" : "absent",
           keys.missing_policy == MISSING_KEY_DROP ? "drop" : "pass");
    printf("Press Ctrl+C to stop.\n");

    while (g_running) {
        struct sockaddr_storage source;
        socklen_t source_length = (socklen_t)sizeof(source);
        socket_io_t received;
        int offset;
        size_t available;
        size_t aligned_length;
        size_t forwarded;
        double now = monotonic_seconds();

        if (now - last_heartbeat >= 5.0) {
            if (!send_heartbeat(*control, opt->verbose)) {
                fprintf(stderr, "Control heartbeat failed; attempting recovery.\n");
                if (!recover_udp_mode(control, opt)) {
                    goto fail;
                }
            }
            last_heartbeat = monotonic_seconds();
        }

        build_ts_request(request, retry_flag);
        if (opt->verbose && blocks == 0U) {
            hex_dump("UDP TS request", request, sizeof(request));
        }

        if (socket_sendto_bytes(dongle_udp, (const char *)request,
                   (int)sizeof(request), 0,
                   (const struct sockaddr *)&dongle_endpoint.address,
                   dongle_endpoint.length) < 0) {
            fprintf(stderr, "Device UDP send failed: %d\n",
                    SOCKET_ERROR_CODE());
            goto fail;
        }

        memset(&source, 0, sizeof(source));
        received = socket_recvfrom_bytes(dongle_udp, (char *)reply,
                            (int)sizeof(reply), 0,
                            (struct sockaddr *)&source, &source_length);

        if (received < 0) {
            ++timeouts;
            ++consecutive_timeouts;
            retry_flag = 1;
            if (timeouts <= 5U || (timeouts % 20U) == 0U) {
                fprintf(stderr,
                    "No TS reply yet (timeout %llu). Check tuning and firewall.\n",
                    (unsigned long long)timeouts);
            }
            if (consecutive_timeouts >= 5U) {
                fprintf(stderr,
                        "Five consecutive stream timeouts; reconnecting and retuning.\n");
                if (!recover_udp_mode(control, opt)) {
                    goto fail;
                }
                consecutive_timeouts = 0;
                retry_flag = 0;
                last_heartbeat = monotonic_seconds();
            }
            continue;
        }

        if (!sockaddr_same_endpoint((const struct sockaddr *)&source,
                                    (const struct sockaddr *)&dongle_endpoint.address)) {
            ++bad_blocks;
            if (opt->verbose) {
                fprintf(stderr, "Ignored UDP reply from an unexpected source.\n");
            }
            continue;
        }

        consecutive_timeouts = 0;
        retry_flag = 0;

        if ((size_t)received < TS_BLOCK_SIZE) {
            ++bad_blocks;
            if (opt->verbose || bad_blocks <= 5U) {
                fprintf(stderr,
                    "Short UDP reply: %lld bytes (expected at least %d).\n",
                    (long long)received, TS_BLOCK_SIZE);
                if (opt->verbose) {
                    hex_dump("Short UDP reply", reply, (size_t)received);
                }
            }
            continue;
        }

        if (opt->verbose && blocks == 0U) {
            printf("First UDP reply: %lld bytes (reference size %d).\n",
                   (long long)received, TS_REPLY_SIZE);
        }

        offset = find_ts_offset(reply, TS_BLOCK_SIZE);
        if (offset < 0) {
            ++bad_blocks;
            if (bad_blocks <= 5U || opt->verbose) {
                fprintf(stderr,
                    "UDP reply does not contain aligned MPEG-TS sync bytes.\n");
            }
            continue;
        }

        available = TS_BLOCK_SIZE - (size_t)offset;
        aligned_length = available - (available % TS_PACKET_SIZE);
        forwarded = forward_ts(output_udp, &vlc_endpoint, dump, &keys,
                               reply + offset, aligned_length, &stats);

        bytes_forwarded += forwarded;
        ++blocks;

        now = monotonic_seconds();
        if (now - report_start >= 2.0) {
            const double elapsed = now - report_start;
            const double mbps = elapsed > 0.0
                ? ((double)bytes_forwarded * 8.0 / elapsed / 1000000.0)
                : 0.0;

            printf("\rBlocks: %llu  Output: %.2f Mbit/s  Timeouts: %llu  "
                   "Bad: %llu  AES even/odd: %llu/%llu  "
                   "Missing even/odd: %llu/%llu  PID1FFE: %llu      ",
                   (unsigned long long)blocks,
                   mbps,
                   (unsigned long long)timeouts,
                   (unsigned long long)(bad_blocks + stats.invalid_packets +
                                        stats.reserved_scrambling),
                   (unsigned long long)stats.even_decrypted,
                   (unsigned long long)stats.odd_decrypted,
                   (unsigned long long)stats.missing_even_key,
                   (unsigned long long)stats.missing_odd_key,
                   (unsigned long long)stats.dropped_pid_1ffe);
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

static bool xml_name_char(char ch)
{
    return isalnum((unsigned char)ch) != 0 || ch == '_' ||
           ch == '-' || ch == ':' || ch == '.';
}

static bool xml_attribute(const char *tag,
                          const char *attribute,
                          char *value,
                          size_t value_capacity)
{
    const char *cursor = tag;
    const size_t wanted_length = strlen(attribute);

    while (*cursor != '\0' && *cursor != '>') {
        const char *name_start;
        const char *value_start;
        size_t name_length;
        size_t value_length;
        char quote;

        while (isspace((unsigned char)*cursor) || *cursor == '<' ||
               *cursor == '/' || *cursor == '?') {
            ++cursor;
        }
        name_start = cursor;
        while (xml_name_char(*cursor)) {
            ++cursor;
        }
        name_length = (size_t)(cursor - name_start);
        if (name_length == 0U) {
            if (*cursor != '\0') {
                ++cursor;
            }
            continue;
        }
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor != '=') {
            continue;
        }
        ++cursor;
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        quote = *cursor;
        if (quote != '\'' && quote != '"') {
            return false;
        }
        ++cursor;
        value_start = cursor;
        while (*cursor != '\0' && *cursor != quote) {
            ++cursor;
        }
        if (*cursor != quote) {
            return false;
        }
        value_length = (size_t)(cursor - value_start);
        ++cursor;

        if (name_length == wanted_length &&
            memcmp(name_start, attribute, wanted_length) == 0) {
            if (value_length + 1U > value_capacity) {
                return false;
            }
            memcpy(value, value_start, value_length);
            value[value_length] = '\0';
            return true;
        }
    }
    return false;
}

static size_t utf8_encode(uint32_t codepoint, char output[4])
{
    if (codepoint <= 0x7FU) {
        output[0] = (char)codepoint;
        return 1U;
    }
    if (codepoint <= 0x7FFU) {
        output[0] = (char)(0xC0U | (codepoint >> 6));
        output[1] = (char)(0x80U | (codepoint & 0x3FU));
        return 2U;
    }
    if (codepoint <= 0xFFFFU &&
        !(codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        output[0] = (char)(0xE0U | (codepoint >> 12));
        output[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[2] = (char)(0x80U | (codepoint & 0x3FU));
        return 3U;
    }
    if (codepoint <= 0x10FFFFU) {
        output[0] = (char)(0xF0U | (codepoint >> 18));
        output[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
        output[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[3] = (char)(0x80U | (codepoint & 0x3FU));
        return 4U;
    }
    return 0U;
}

static void xml_decode(char *text)
{
    char *read_cursor = text;
    char *write_cursor = text;

    while (*read_cursor != '\0') {
        if (*read_cursor == '&') {
            char *semicolon = strchr(read_cursor + 1, ';');
            if (semicolon != NULL && (size_t)(semicolon - read_cursor) <= 16U) {
                uint32_t codepoint = 0;
                bool recognized = true;
                if (strncmp(read_cursor, "&amp;", 5) == 0) {
                    codepoint = '&';
                } else if (strncmp(read_cursor, "&apos;", 6) == 0) {
                    codepoint = '\'';
                } else if (strncmp(read_cursor, "&quot;", 6) == 0) {
                    codepoint = '"';
                } else if (strncmp(read_cursor, "&lt;", 4) == 0) {
                    codepoint = '<';
                } else if (strncmp(read_cursor, "&gt;", 4) == 0) {
                    codepoint = '>';
                } else if (read_cursor[1] == '#') {
                    const bool hexadecimal =
                        read_cursor[2] == 'x' || read_cursor[2] == 'X';
                    const char *digits = read_cursor + (hexadecimal ? 3 : 2);
                    char number[16];
                    char *end = NULL;
                    const size_t digit_count = (size_t)(semicolon - digits);
                    unsigned long parsed;
                    if (digit_count == 0U || digit_count >= sizeof(number)) {
                        recognized = false;
                    } else {
                        memcpy(number, digits, digit_count);
                        number[digit_count] = '\0';
                        errno = 0;
                        parsed = strtoul(number, &end, hexadecimal ? 16 : 10);
                        if (errno != 0 || end == number || *end != '\0' ||
                            parsed > 0x10FFFFUL) {
                            recognized = false;
                        } else {
                            codepoint = (uint32_t)parsed;
                        }
                    }
                } else {
                    recognized = false;
                }

                if (recognized) {
                    char encoded[4];
                    const size_t encoded_length = utf8_encode(codepoint, encoded);
                    if (encoded_length > 0U) {
                        memcpy(write_cursor, encoded, encoded_length);
                        write_cursor += encoded_length;
                        read_cursor = semicolon + 1;
                        continue;
                    }
                }
            }
        }
        *write_cursor++ = *read_cursor++;
    }
    *write_cursor = '\0';
}

static bool parse_channel_tag(const char *tag, channel_t *channel)
{
    char text[256];
    uint32_t number;
    size_t i;

    memset(channel, 0, sizeof(*channel));
    if (!xml_attribute(tag, "type", channel->type, sizeof(channel->type)) ||
        !xml_attribute(tag, "pol", text, sizeof(text)) ||
        !parse_polarization(text, &channel->polarization) ||
        !xml_attribute(tag, "sym", text, sizeof(text)) ||
        !parse_u32(text, &channel->symbol_rate_ks) ||
        !xml_attribute(tag, "s_id", text, sizeof(text)) ||
        !parse_u32(text, &channel->service_id) ||
        !xml_attribute(tag, "freq", text, sizeof(text)) ||
        !parse_u32(text, &channel->frequency_mhz) ||
        !xml_attribute(tag, "lcn", text, sizeof(text)) ||
        !parse_u32(text, &channel->lcn) ||
        !xml_attribute(tag, "s_name", channel->name, sizeof(channel->name))) {
        return false;
    }

    for (i = 0; channel->type[i] != '\0'; ++i) {
        channel->type[i] = (char)toupper((unsigned char)channel->type[i]);
    }
    xml_decode(channel->name);
    if (xml_attribute(tag, "fta", text, sizeof(text)) &&
        parse_u32(text, &number)) {
        channel->fta = number != 0U;
    }
    return channel->frequency_mhz <= 65535U &&
           channel->symbol_rate_ks <= 0xFFFFFFU;
}

static void free_channel_list(channel_list_t *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static bool append_channel(channel_list_t *list,
                           size_t *capacity,
                           const channel_t *channel)
{
    size_t i;
    for (i = 0; i < list->count; ++i) {
        if (list->items[i].lcn == channel->lcn) {
            fprintf(stderr,
                    "Warning: duplicate channel number %u was skipped.\n",
                    (unsigned)channel->lcn);
            return true;
        }
    }

    if (list->count == *capacity) {
        const size_t new_capacity = *capacity == 0U ? 128U : *capacity * 2U;
        channel_t *new_items;
        if (new_capacity < *capacity ||
            new_capacity > SIZE_MAX / sizeof(*new_items)) {
            return false;
        }
        new_items = (channel_t *)realloc(
            list->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return false;
        }
        list->items = new_items;
        *capacity = new_capacity;
    }
    list->items[list->count++] = *channel;
    return true;
}

static char *find_xml_tag_end(char *start)
{
    char quote = '\0';
    char *cursor;
    for (cursor = start; *cursor != '\0'; ++cursor) {
        if (quote != '\0') {
            if (*cursor == quote) {
                quote = '\0';
            }
        } else if (*cursor == '\'' || *cursor == '"') {
            quote = *cursor;
        } else if (*cursor == '>') {
            return cursor;
        }
    }
    return NULL;
}

static bool load_channel_list(const char *path, channel_list_t *list)
{
    FILE *file;
    long file_length;
    char *document = NULL;
    char *cursor;
    size_t capacity = 0;
    size_t malformed = 0;

    memset(list, 0, sizeof(*list));
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Cannot open channel list '%s': %s\n",
                path, strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_length = ftell(file)) < 0 ||
        (unsigned long)file_length > XML_FILE_LIMIT ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Channel XML is unreadable or exceeds %u MiB.\n",
                (unsigned)(XML_FILE_LIMIT / (1024U * 1024U)));
        fclose(file);
        return false;
    }

    document = (char *)malloc((size_t)file_length + 1U);
    if (document == NULL) {
        fclose(file);
        return false;
    }
    if (fread(document, 1, (size_t)file_length, file) !=
        (size_t)file_length) {
        fprintf(stderr, "Could not read complete channel XML: %s\n",
                ferror(file) ? strerror(errno) : "unexpected end of file");
        fclose(file);
        free(document);
        return false;
    }
    fclose(file);
    document[file_length] = '\0';

    cursor = document;
    while ((cursor = strstr(cursor, "<ch")) != NULL) {
        const char next = cursor[3];
        char *tag_end;
        size_t tag_length;
        char *tag;
        channel_t channel;

        if (!(isspace((unsigned char)next) || next == '>' || next == '/')) {
            cursor += 3;
            continue;
        }
        tag_end = find_xml_tag_end(cursor);
        if (tag_end == NULL) {
            ++malformed;
            break;
        }
        tag_length = (size_t)(tag_end - cursor + 1);
        if (tag_length > 65536U) {
            ++malformed;
            cursor = tag_end + 1;
            continue;
        }
        tag = (char *)malloc(tag_length + 1U);
        if (tag == NULL) {
            free(document);
            free_channel_list(list);
            return false;
        }
        memcpy(tag, cursor, tag_length);
        tag[tag_length] = '\0';

        if (parse_channel_tag(tag, &channel)) {
            if (!append_channel(list, &capacity, &channel)) {
                free(tag);
                free(document);
                free_channel_list(list);
                fprintf(stderr, "Out of memory while reading channels.\n");
                return false;
            }
        } else {
            ++malformed;
        }
        free(tag);
        cursor = tag_end + 1;
    }

    free(document);
    if (malformed > 0U) {
        fprintf(stderr, "Warning: skipped %zu malformed channel element(s).\n",
                malformed);
    }
    if (list->count == 0U) {
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

    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    return select(0, &read_set, NULL, NULL, &timeout);
#else
    return select(sock + 1, &read_set, NULL, NULL, &timeout);
#endif
}

typedef struct {
    socket_t socket;
#ifdef SORALINK_USE_OPENSSL
    SSL *ssl;
#endif
} http_connection_t;

typedef struct {
    bool enabled;
#ifdef SORALINK_USE_OPENSSL
    SSL_CTX *context;
#endif
} tls_server_t;

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_OTHER
} http_method_t;

typedef struct {
    http_method_t method;
    char path[1024];
} http_request_line_t;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} string_buffer_t;

typedef struct {
    http_connection_t clients[HTTP_CLIENT_LIMIT];
    size_t max_clients;
    socket_t dongle_udp;
    endpoint_t dongle_endpoint;
    const channel_t *channel;
    stream_keys_t keys;
    stream_stats_t stats;
    uint8_t retry_flag;
    uint64_t blocks;
    uint64_t bytes_forwarded;
    uint64_t timeouts;
    uint64_t bad_blocks;
    unsigned consecutive_timeouts;
    double report_start;
} http_stream_t;

typedef enum {
    STREAM_STEP_OK = 0,
    STREAM_STEP_RECOVER,
    STREAM_STEP_FATAL
} stream_step_t;

static void http_connection_init(http_connection_t *connection)
{
    memset(connection, 0, sizeof(*connection));
    connection->socket = SOCKET_INVALID;
}

static void http_connection_close(http_connection_t *connection)
{
#ifdef SORALINK_USE_OPENSSL
    if (connection->ssl != NULL) {
        SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl);
        connection->ssl = NULL;
    }
#endif
    if (connection->socket != SOCKET_INVALID) {
        CLOSESOCKET(connection->socket);
        connection->socket = SOCKET_INVALID;
    }
}

static bool http_connection_peer_closed(http_connection_t *connection)
{
    char byte;
    const int ready = wait_socket_readable(connection->socket, 0);
    if (ready <= 0) {
        return false;
    }
#ifdef SORALINK_USE_OPENSSL
    if (connection->ssl != NULL) {
        const int result = SSL_peek(connection->ssl, &byte, 1);
        if (result > 0) {
            return false;
        }
        switch (SSL_get_error(connection->ssl, result)) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return false;
            default:
                return true;
        }
    }
#endif
#ifdef _WIN32
    return recv(connection->socket, &byte, 1, MSG_PEEK) <= 0;
#else
    return recv(connection->socket, &byte, 1, MSG_PEEK | MSG_DONTWAIT) <= 0;
#endif
}

static bool tls_server_init(tls_server_t *tls, const options_t *opt)
{
    memset(tls, 0, sizeof(*tls));
    if (opt->tls_cert_path == NULL) {
        return true;
    }
#ifdef SORALINK_USE_OPENSSL
    OPENSSL_init_ssl(0, NULL);
    tls->context = SSL_CTX_new(TLS_server_method());
    if (tls->context == NULL) {
        ERR_print_errors_fp(stderr);
        return false;
    }
    SSL_CTX_set_min_proto_version(tls->context, TLS1_2_VERSION);
    SSL_CTX_set_options(tls->context, SSL_OP_NO_COMPRESSION);
    if (SSL_CTX_use_certificate_chain_file(tls->context,
                                           opt->tls_cert_path) != 1 ||
        SSL_CTX_use_PrivateKey_file(tls->context, opt->tls_key_path,
                                    SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(tls->context) != 1) {
        fprintf(stderr, "Could not load or validate the TLS certificate/key.\n");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(tls->context);
        tls->context = NULL;
        return false;
    }
    tls->enabled = true;
    return true;
#else
    (void)opt;
    fprintf(stderr, "TLS support is unavailable in this build.\n");
    return false;
#endif
}

static void tls_server_cleanup(tls_server_t *tls)
{
#ifdef SORALINK_USE_OPENSSL
    if (tls->context != NULL) {
        SSL_CTX_free(tls->context);
        tls->context = NULL;
    }
#else
    (void)tls;
#endif
    tls->enabled = false;
}

static socket_io_t http_connection_recv(http_connection_t *connection,
                                        char *buffer,
                                        int length)
{
#ifdef SORALINK_USE_OPENSSL
    if (connection->ssl != NULL) {
        const int result = SSL_read(connection->ssl, buffer, length);
        return (socket_io_t)result;
    }
#endif
    return socket_recv_bytes(connection->socket, buffer, length, 0);
}

static bool http_connection_send_all(http_connection_t *connection,
                                     const uint8_t *data,
                                     size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        const size_t remaining = length - sent;
        const int amount = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        socket_io_t result;
#ifdef SORALINK_USE_OPENSSL
        if (connection->ssl != NULL) {
            result = (socket_io_t)SSL_write(connection->ssl,
                                            data + sent, amount);
        } else
#endif
        {
            result = socket_send_bytes(connection->socket,
                                       (const char *)data + sent, amount, 0);
        }
        if (result <= 0) {
            return false;
        }
        sent += (size_t)result;
    }
    return true;
}

static bool http_connection_send_text(http_connection_t *connection,
                                      const char *text)
{
    return http_connection_send_all(connection,
                                    (const uint8_t *)text, strlen(text));
}

static bool http_accept_connection(socket_t listener,
                                   const tls_server_t *tls,
                                   int timeout_ms,
                                   http_connection_t *connection)
{
    struct sockaddr_storage peer;
    socklen_t peer_length = (socklen_t)sizeof(peer);

    http_connection_init(connection);
    connection->socket = accept(listener, (struct sockaddr *)&peer, &peer_length);
    if (connection->socket == SOCKET_INVALID) {
        fprintf(stderr, "HTTP accept() failed: %d\n", SOCKET_ERROR_CODE());
        return false;
    }
    socket_set_timeout(connection->socket, timeout_ms);

#ifdef SORALINK_USE_OPENSSL
    if (tls->enabled) {
        connection->ssl = SSL_new(tls->context);
        if (connection->ssl == NULL ||
            SSL_set_fd(connection->ssl, (int)connection->socket) != 1 ||
            SSL_accept(connection->ssl) != 1) {
            fprintf(stderr, "TLS handshake failed.\n");
            if (connection->ssl != NULL) {
                ERR_print_errors_fp(stderr);
            }
            http_connection_close(connection);
            return false;
        }
    }
#else
    (void)tls;
#endif
    return true;
}

static socket_t create_http_listener(const char *bind_host,
                                     uint16_t port,
                                     endpoint_t *bound_endpoint)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *item;
    char service[16];
    int status;
    int last_error = 0;
    const char *host = bind_host;

    if (host != NULL && (strcmp(host, "*") == 0 || *host == '\0')) {
        host = NULL;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    snprintf(service, sizeof(service), "%u", (unsigned)port);

    status = getaddrinfo(host, service, &hints, &results);
    if (status != 0) {
#ifdef _WIN32
        fprintf(stderr, "Cannot resolve HTTP bind host '%s': %d\n",
                bind_host, status);
#else
        fprintf(stderr, "Cannot resolve HTTP bind host '%s': %s\n",
                bind_host, gai_strerror(status));
#endif
        return SOCKET_INVALID;
    }

    for (item = results; item != NULL; item = item->ai_next) {
        socket_t listener;
        int reuse = 1;
        if (item->ai_family != AF_INET && item->ai_family != AF_INET6) {
            continue;
        }
        listener = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (listener == SOCKET_INVALID) {
            last_error = SOCKET_ERROR_CODE();
            continue;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&reuse, sizeof(reuse));
#ifdef IPV6_V6ONLY
        if (item->ai_family == AF_INET6) {
            int ipv6_only = 0;
            setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY,
                       (const char *)&ipv6_only, sizeof(ipv6_only));
        }
#endif
        if (bind(listener, item->ai_addr, (socklen_t)item->ai_addrlen) == 0 &&
            listen(listener, 16) == 0) {
            memset(bound_endpoint, 0, sizeof(*bound_endpoint));
            memcpy(&bound_endpoint->address, item->ai_addr, item->ai_addrlen);
            bound_endpoint->length = (socklen_t)item->ai_addrlen;
            bound_endpoint->family = item->ai_family;
            freeaddrinfo(results);
            return listener;
        }
        last_error = SOCKET_ERROR_CODE();
        CLOSESOCKET(listener);
    }

    freeaddrinfo(results);
    fprintf(stderr, "HTTP bind to %s:%u failed: %d\n",
            bind_host, (unsigned)port, last_error);
    return SOCKET_INVALID;
}

static bool read_http_request(http_connection_t *connection,
                              char *request,
                              size_t capacity)
{
    size_t used = 0;
    while (used + 1U < capacity) {
        const size_t remaining = capacity - used - 1U;
        const int amount = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        const socket_io_t result = http_connection_recv(
            connection, request + used, amount);
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

static bool parse_http_request_line(const char *request,
                                    http_request_line_t *line)
{
    const char *space1 = strchr(request, ' ');
    const char *space2;
    size_t method_length;
    size_t path_length;

    memset(line, 0, sizeof(*line));
    if (space1 == NULL) {
        return false;
    }
    method_length = (size_t)(space1 - request);
    if (method_length == 3U && memcmp(request, "GET", 3) == 0) {
        line->method = HTTP_METHOD_GET;
    } else if (method_length == 4U && memcmp(request, "HEAD", 4) == 0) {
        line->method = HTTP_METHOD_HEAD;
    } else if (method_length == 7U && memcmp(request, "OPTIONS", 7) == 0) {
        line->method = HTTP_METHOD_OPTIONS;
    } else {
        line->method = HTTP_METHOD_OTHER;
    }

    while (*space1 == ' ') {
        ++space1;
    }
    space2 = strchr(space1, ' ');
    if (space2 == NULL) {
        return false;
    }
    path_length = (size_t)(space2 - space1);
    if (path_length == 0U || path_length + 1U > sizeof(line->path)) {
        return false;
    }
    memcpy(line->path, space1, path_length);
    line->path[path_length] = '\0';
    return true;
}

static bool http_get_header(const char *request,
                            const char *name,
                            char *value,
                            size_t value_capacity)
{
    const char *line = request;
    const size_t name_length = strlen(name);

    while (*line != '\0') {
        const char *line_end = strstr(line, "\r\n");
        const char *colon;
        size_t line_length;
        const char *start;
        const char *end;
        if (line_end == NULL) {
            line_end = strchr(line, '\n');
        }
        if (line_end == NULL) {
            line_end = line + strlen(line);
        }
        line_length = (size_t)(line_end - line);
        if (line_length == 0U) {
            break;
        }
        colon = memchr(line, ':', line_length);
        if (colon != NULL && (size_t)(colon - line) == name_length &&
            STRNCASECMP(line, name, name_length) == 0) {
            start = colon + 1;
            end = line + line_length;
            while (start < end && isspace((unsigned char)*start)) {
                ++start;
            }
            while (end > start && isspace((unsigned char)end[-1])) {
                --end;
            }
            if ((size_t)(end - start) + 1U > value_capacity) {
                return false;
            }
            memcpy(value, start, (size_t)(end - start));
            value[end - start] = '\0';
            return true;
        }
        line = *line_end == '\0' ? line_end : line_end + 1;
        if (*line == '\n') {
            ++line;
        }
    }
    return false;
}

static size_t base64_encode(const uint8_t *input,
                            size_t input_length,
                            char *output,
                            size_t output_capacity)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t in_pos = 0;
    size_t out_pos = 0;
    const size_t needed = ((input_length + 2U) / 3U) * 4U;
    if (needed + 1U > output_capacity) {
        return 0U;
    }

    while (in_pos < input_length) {
        const uint32_t a = input[in_pos++];
        const bool have_b = in_pos < input_length;
        const uint32_t b = have_b ? input[in_pos++] : 0U;
        const bool have_c = in_pos < input_length;
        const uint32_t c = have_c ? input[in_pos++] : 0U;
        const uint32_t triple = (a << 16) | (b << 8) | c;

        output[out_pos++] = alphabet[(triple >> 18) & 0x3FU];
        output[out_pos++] = alphabet[(triple >> 12) & 0x3FU];
        output[out_pos++] = have_b ? alphabet[(triple >> 6) & 0x3FU] : '=';
        output[out_pos++] = have_c ? alphabet[triple & 0x3FU] : '=';
    }
    output[out_pos] = '\0';
    return out_pos;
}

static bool constant_time_equal(const char *left, const char *right)
{
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);
    size_t i;
    unsigned difference = (unsigned)(left_length ^ right_length);
    const size_t length = left_length > right_length ? left_length : right_length;

    for (i = 0; i < length; ++i) {
        const unsigned a = i < left_length ? (unsigned char)left[i] : 0U;
        const unsigned b = i < right_length ? (unsigned char)right[i] : 0U;
        difference |= a ^ b;
    }
    return difference == 0U;
}

static bool http_authorized(const char *request, const options_t *opt)
{
    char credentials[1024];
    char encoded[1400];
    char expected[1410];
    char supplied[1500];
    int length;

    if (opt->http_user == NULL) {
        return true;
    }
    length = snprintf(credentials, sizeof(credentials), "%s:%s",
                      opt->http_user, opt->http_password);
    if (length < 0 || (size_t)length >= sizeof(credentials) ||
        base64_encode((const uint8_t *)credentials, (size_t)length,
                      encoded, sizeof(encoded)) == 0U) {
        return false;
    }
    snprintf(expected, sizeof(expected), "Basic %s", encoded);
    if (!http_get_header(request, "Authorization",
                         supplied, sizeof(supplied))) {
        return false;
    }
    return constant_time_equal(expected, supplied);
}

static void send_http_response(http_connection_t *connection,
                               int status,
                               const char *reason,
                               const char *content_type,
                               const char *extra_headers,
                               const char *body,
                               bool head_only)
{
    char header[1024];
    const size_t body_length = body != NULL ? strlen(body) : 0U;
    const int length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "%s"
        "Connection: close\r\n\r\n",
        status, reason, content_type, body_length,
        extra_headers != NULL ? extra_headers : "");
    if (length <= 0 || (size_t)length >= sizeof(header)) {
        return;
    }
    if (!http_connection_send_all(connection,
                                  (const uint8_t *)header, (size_t)length)) {
        return;
    }
    if (!head_only && body_length > 0U) {
        http_connection_send_all(connection,
                                 (const uint8_t *)body, body_length);
    }
}

static void send_http_error(http_connection_t *connection,
                            int status,
                            const char *reason,
                            const char *message,
                            bool head_only)
{
    send_http_response(connection, status, reason,
                       "text/plain; charset=utf-8", NULL,
                       message, head_only);
}

static void send_http_unauthorized(http_connection_t *connection,
                                   bool head_only)
{
    send_http_response(connection, 401, "Unauthorized",
                       "text/plain; charset=utf-8",
                       "WWW-Authenticate: Basic realm=\"SORALink\", charset=\"UTF-8\"\r\n",
                       "Authentication required.\n", head_only);
}

static bool string_buffer_reserve(string_buffer_t *buffer, size_t additional)
{
    size_t required;
    size_t capacity;
    char *data;
    if (additional > SIZE_MAX - buffer->length - 1U) {
        return false;
    }
    required = buffer->length + additional + 1U;
    if (required <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    data = (char *)realloc(buffer->data, capacity);
    if (data == NULL) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return true;
}

static bool string_buffer_append(string_buffer_t *buffer, const char *text)
{
    const size_t length = strlen(text);
    if (!string_buffer_reserve(buffer, length)) {
        return false;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static void string_buffer_free(string_buffer_t *buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static void sanitize_m3u_text(const char *input,
                              char *output,
                              size_t output_capacity)
{
    size_t used = 0;
    while (*input != '\0' && used + 1U < output_capacity) {
        char ch = *input++;
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        } else if (ch == '"') {
            ch = '\'';
        }
        output[used++] = ch;
    }
    output[used] = '\0';
}

static bool valid_host_header(const char *host)
{
    const unsigned char *cursor = (const unsigned char *)host;
    if (*cursor == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (!(isalnum(*cursor) || *cursor == '.' || *cursor == '-' ||
              *cursor == ':' || *cursor == '[' || *cursor == ']')) {
            return false;
        }
        ++cursor;
    }
    return true;
}

static void get_http_host(const char *request,
                          const options_t *opt,
                          char *host,
                          size_t host_capacity)
{
    char supplied[512];
    if (http_get_header(request, "Host", supplied, sizeof(supplied)) &&
        valid_host_header(supplied)) {
        snprintf(host, host_capacity, "%s", supplied);
        return;
    }

    if (strcmp(opt->http_bind_ip, "0.0.0.0") == 0 ||
        strcmp(opt->http_bind_ip, "*") == 0) {
        snprintf(host, host_capacity, "127.0.0.1:%u",
                 (unsigned)opt->http_port);
    } else if (strcmp(opt->http_bind_ip, "::") == 0) {
        snprintf(host, host_capacity, "[::1]:%u", (unsigned)opt->http_port);
    } else {
        char formatted[512];
        size_t used;
        format_host_for_url(opt->http_bind_ip, formatted, sizeof(formatted));
        snprintf(host, host_capacity, "%s", formatted);
        used = strlen(host);
        if (used < host_capacity) {
            snprintf(host + used, host_capacity - used, ":%u",
                     (unsigned)opt->http_port);
        }
    }
}

static bool playlist_type_matches(const channel_t *channel,
                                  const char *filter)
{
    return filter == NULL || strcmp(channel->type, filter) == 0;
}

static void serve_playlist(http_connection_t *connection,
                           const channel_list_t *channels,
                           const options_t *opt,
                           const tls_server_t *tls,
                           const char *request,
                           const char *filter,
                           bool head_only)
{
    string_buffer_t playlist;
    char host[512];
    size_t i;

    memset(&playlist, 0, sizeof(playlist));
    get_http_host(request, opt, host, sizeof(host));
    if (!string_buffer_append(&playlist, "#EXTM3U\r\n")) {
        send_http_error(connection, 500, "Internal Server Error",
                        "Out of memory.\n", head_only);
        return;
    }

    for (i = 0; i < channels->count; ++i) {
        const channel_t *channel = &channels->items[i];
        char name[512];
        char type[64];
        char entry[1400];
        int length;
        if (!playlist_type_matches(channel, filter)) {
            continue;
        }
        sanitize_m3u_text(channel->name, name, sizeof(name));
        sanitize_m3u_text(channel->type, type, sizeof(type));
        length = snprintf(
            entry, sizeof(entry),
            "#EXTINF:-1 tvg-chno=\"%u\" group-title=\"%s\",%u - %s\r\n"
            "#EXTVLCOPT:network-caching=1000\r\n"
            "%s://%s/channel/%u\r\n",
            (unsigned)channel->lcn, type,
            (unsigned)channel->lcn, name,
            tls->enabled ? "https" : "http", host,
            (unsigned)channel->lcn);
        if (length < 0 || (size_t)length >= sizeof(entry) ||
            !string_buffer_append(&playlist, entry)) {
            string_buffer_free(&playlist);
            send_http_error(connection, 500, "Internal Server Error",
                            "Could not build playlist.\n", head_only);
            return;
        }
    }

    send_http_response(connection, 200, "OK",
                       "audio/x-mpegurl; charset=utf-8", NULL,
                       playlist.data != NULL ? playlist.data : "",
                       head_only);
    string_buffer_free(&playlist);
}

static size_t http_stream_client_count(const http_stream_t *stream)
{
    size_t i;
    size_t count = 0;
    for (i = 0; i < stream->max_clients; ++i) {
        if (stream->clients[i].socket != SOCKET_INVALID) {
            ++count;
        }
    }
    return count;
}

static bool http_stream_add_client(http_stream_t *stream,
                                   http_connection_t *connection)
{
    size_t i;
    for (i = 0; i < stream->max_clients; ++i) {
        if (stream->clients[i].socket == SOCKET_INVALID) {
            stream->clients[i] = *connection;
            http_connection_init(connection);
            return true;
        }
    }
    return false;
}

static void http_stream_close_clients(http_stream_t *stream)
{
    size_t i;
    for (i = 0; i < stream->max_clients; ++i) {
        http_connection_close(&stream->clients[i]);
    }
}

static void http_stream_stop_if_idle(http_stream_t *stream)
{
    if (http_stream_client_count(stream) == 0U) {
        if (stream->dongle_udp != SOCKET_INVALID) {
            CLOSESOCKET(stream->dongle_udp);
            stream->dongle_udp = SOCKET_INVALID;
        }
        stream->channel = NULL;
        stream->retry_flag = 0;
        stream->consecutive_timeouts = 0;
    }
}

static bool http_stream_send_to_clients(http_stream_t *stream,
                                        const uint8_t *data,
                                        size_t length)
{
    size_t i;
    bool delivered = false;
    for (i = 0; i < stream->max_clients; ++i) {
        http_connection_t *client = &stream->clients[i];
        if (client->socket == SOCKET_INVALID) {
            continue;
        }
        if (http_connection_peer_closed(client)) {
            http_connection_close(client);
            continue;
        }
        if (!http_connection_send_all(client, data, length)) {
            http_connection_close(client);
        } else {
            delivered = true;
        }
    }
    return delivered;
}

static size_t forward_ts_http_clients(http_stream_t *stream,
                                      const uint8_t *data,
                                      size_t length)
{
    uint8_t chunk[VLC_CHUNK_SIZE];
    uint8_t transformed[TS_PACKET_SIZE];
    size_t chunk_used = 0;
    size_t pos;
    size_t forwarded = 0;

    for (pos = 0; pos + TS_PACKET_SIZE <= length; pos += TS_PACKET_SIZE) {
        if (!transform_ts_packet(&stream->keys, data + pos,
                                 transformed, &stream->stats)) {
            continue;
        }
        if (ts_pid(transformed) == 0x1FFE) {
            ++stream->stats.dropped_pid_1ffe;
            continue;
        }
        memcpy(chunk + chunk_used, transformed, TS_PACKET_SIZE);
        chunk_used += TS_PACKET_SIZE;
        if (chunk_used == sizeof(chunk)) {
            if (!http_stream_send_to_clients(stream, chunk, chunk_used)) {
                return forwarded;
            }
            forwarded += chunk_used;
            chunk_used = 0;
        }
    }
    if (chunk_used > 0U &&
        http_stream_send_to_clients(stream, chunk, chunk_used)) {
        forwarded += chunk_used;
    }
    return forwarded;
}

static bool http_stream_open_udp(http_stream_t *stream, int timeout_ms)
{
    if (stream->dongle_udp != SOCKET_INVALID) {
        return true;
    }
    stream->dongle_udp = create_udp_socket(stream->dongle_endpoint.family,
                                           timeout_ms);
    return stream->dongle_udp != SOCKET_INVALID;
}

static stream_step_t http_stream_service(http_stream_t *stream,
                                         const options_t *opt)
{
    uint8_t request[FRAME_SIZE];
    uint8_t reply[65536];
    struct sockaddr_storage source;
    socklen_t source_length = (socklen_t)sizeof(source);
    socket_io_t received;
    int ready;
    int offset;
    size_t available;
    size_t aligned_length;
    size_t forwarded;
    const int response_wait_ms = opt->timeout_ms < 500 ? opt->timeout_ms : 500;

    if (http_stream_client_count(stream) == 0U) {
        http_stream_stop_if_idle(stream);
        return STREAM_STEP_OK;
    }
    if (!http_stream_open_udp(stream, opt->timeout_ms)) {
        return STREAM_STEP_FATAL;
    }

    build_ts_request(request, stream->retry_flag);
    if (socket_sendto_bytes(stream->dongle_udp,
               (const char *)request, (int)sizeof(request), 0,
               (const struct sockaddr *)&stream->dongle_endpoint.address,
               stream->dongle_endpoint.length) < 0) {
        return STREAM_STEP_RECOVER;
    }

    ready = wait_socket_readable(stream->dongle_udp, response_wait_ms);
    if (ready < 0) {
        return STREAM_STEP_FATAL;
    }
    if (ready == 0) {
        ++stream->timeouts;
        ++stream->consecutive_timeouts;
        stream->retry_flag = 1;
        return stream->consecutive_timeouts >= 5U
            ? STREAM_STEP_RECOVER : STREAM_STEP_OK;
    }

    memset(&source, 0, sizeof(source));
    received = socket_recvfrom_bytes(stream->dongle_udp,
                        (char *)reply, (int)sizeof(reply), 0,
                        (struct sockaddr *)&source, &source_length);
    if (received < 0) {
        ++stream->timeouts;
        ++stream->consecutive_timeouts;
        stream->retry_flag = 1;
        return stream->consecutive_timeouts >= 5U
            ? STREAM_STEP_RECOVER : STREAM_STEP_OK;
    }
    if (!sockaddr_same_endpoint((const struct sockaddr *)&source,
                                (const struct sockaddr *)&stream->dongle_endpoint.address)) {
        ++stream->bad_blocks;
        return STREAM_STEP_OK;
    }

    stream->consecutive_timeouts = 0;
    stream->retry_flag = 0;
    if ((size_t)received < TS_BLOCK_SIZE) {
        ++stream->bad_blocks;
        return STREAM_STEP_OK;
    }
    offset = find_ts_offset(reply, TS_BLOCK_SIZE);
    if (offset < 0) {
        ++stream->bad_blocks;
        return STREAM_STEP_OK;
    }

    available = TS_BLOCK_SIZE - (size_t)offset;
    aligned_length = available - (available % TS_PACKET_SIZE);
    forwarded = forward_ts_http_clients(stream, reply + offset, aligned_length);
    stream->bytes_forwarded += forwarded;
    ++stream->blocks;

    if (monotonic_seconds() - stream->report_start >= 2.0) {
        const double now = monotonic_seconds();
        const double elapsed = now - stream->report_start;
        const double mbps = elapsed > 0.0
            ? (double)stream->bytes_forwarded * 8.0 / elapsed / 1000000.0
            : 0.0;
        printf("\rHTTP clients: %zu  Blocks: %llu  Output: %.2f Mbit/s  "
               "Timeouts: %llu  Bad: %llu  AES even/odd: %llu/%llu      ",
               http_stream_client_count(stream),
               (unsigned long long)stream->blocks,
               mbps,
               (unsigned long long)stream->timeouts,
               (unsigned long long)(stream->bad_blocks +
                                    stream->stats.invalid_packets +
                                    stream->stats.reserved_scrambling),
               (unsigned long long)stream->stats.even_decrypted,
               (unsigned long long)stream->stats.odd_decrypted);
        fflush(stdout);
        stream->bytes_forwarded = 0;
        stream->report_start = now;
    }

    http_stream_stop_if_idle(stream);
    return STREAM_STEP_OK;
}

static void json_escape(const char *input, char *output, size_t capacity)
{
    size_t used = 0;
    while (*input != '\0' && used + 1U < capacity) {
        const unsigned char ch = (unsigned char)*input++;
        const char *escape = NULL;
        if (ch == '"') {
            escape = "\\\"";
        } else if (ch == '\\') {
            escape = "\\\\";
        } else if (ch == '\n') {
            escape = "\\n";
        } else if (ch == '\r') {
            escape = "\\r";
        }
        if (escape != NULL) {
            const size_t length = strlen(escape);
            if (used + length >= capacity) {
                break;
            }
            memcpy(output + used, escape, length);
            used += length;
        } else if (ch >= 0x20U) {
            output[used++] = (char)ch;
        }
    }
    output[used] = '\0';
}

static void serve_status(http_connection_t *connection,
                         const http_stream_t *stream,
                         bool head_only)
{
    char body[1400];
    char name[600];
    int length;
    if (stream->channel != NULL) {
        json_escape(stream->channel->name, name, sizeof(name));
        length = snprintf(
            body, sizeof(body),
            "{\n  \"streaming\": true,\n  \"clients\": %zu,\n"
            "  \"channel\": {\"lcn\": %u, \"name\": \"%s\", "
            "\"frequency_mhz\": %u, \"symbol_rate_ks\": %u, "
            "\"polarization\": \"%c\", \"service_id\": %u},\n"
            "  \"aes\": {\"even_decrypted\": %llu, "
            "\"odd_decrypted\": %llu, \"missing_even\": %llu, "
            "\"missing_odd\": %llu}\n}\n",
            http_stream_client_count(stream),
            (unsigned)stream->channel->lcn, name,
            (unsigned)stream->channel->frequency_mhz,
            (unsigned)stream->channel->symbol_rate_ks,
            stream->channel->polarization,
            (unsigned)stream->channel->service_id,
            (unsigned long long)stream->stats.even_decrypted,
            (unsigned long long)stream->stats.odd_decrypted,
            (unsigned long long)stream->stats.missing_even_key,
            (unsigned long long)stream->stats.missing_odd_key);
    } else {
        length = snprintf(body, sizeof(body),
                          "{\n  \"streaming\": false,\n  \"clients\": 0\n}\n");
    }
    if (length < 0 || (size_t)length >= sizeof(body)) {
        send_http_error(connection, 500, "Internal Server Error",
                        "Status output overflow.\n", head_only);
        return;
    }
    send_http_response(connection, 200, "OK",
                       "application/json; charset=utf-8", NULL,
                       body, head_only);
}

static bool send_stream_headers(http_connection_t *connection)
{
    return http_connection_send_text(
        connection,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: video/MP2T\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n");
}

static bool recover_http_control(socket_t *control,
                                 const options_t *opt,
                                 const channel_t *channel,
                                 tuning_state_t *tuning)
{
    if (!reconnect_control_session(control, opt)) {
        return false;
    }
    memset(tuning, 0, sizeof(*tuning));
    if (!configure_satellite(*control, opt)) {
        return false;
    }
    return channel == NULL || tune_channel(*control, opt, channel, tuning);
}

static void print_server_url(const options_t *opt,
                             const tls_server_t *tls,
                             const char *path)
{
    char host[512];
    const char *display_host = opt->http_bind_ip;
    if (strcmp(display_host, "0.0.0.0") == 0 || strcmp(display_host, "*") == 0) {
        display_host = "127.0.0.1";
    } else if (strcmp(display_host, "::") == 0) {
        display_host = "::1";
    }
    format_host_for_url(display_host, host, sizeof(host));
    printf("  %s://%s:%u%s\n", tls->enabled ? "https" : "http",
           host, (unsigned)opt->http_port, path);
}

static int run_http_server(socket_t *control, const options_t *opt)
{
    channel_list_t channels;
    tuning_state_t tuning;
    socket_t listener = SOCKET_INVALID;
    endpoint_t bound_endpoint;
    tls_server_t tls;
    http_stream_t stream;
    double last_heartbeat = monotonic_seconds();
    int result = EXIT_SUCCESS;
    size_t i;

    memset(&tuning, 0, sizeof(tuning));
    memset(&stream, 0, sizeof(stream));
    stream.dongle_udp = SOCKET_INVALID;
    stream.max_clients = (size_t)opt->max_http_clients;
    stream.report_start = monotonic_seconds();
    for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
        http_connection_init(&stream.clients[i]);
    }
    stream_keys_init(&stream.keys, opt);

    if (!load_channel_list(opt->channels_path, &channels)) {
        return EXIT_FAILURE;
    }
    if (!resolve_endpoint(opt->device_ip, opt->stream_port,
                          SOCK_DGRAM, false, &stream.dongle_endpoint)) {
        free_channel_list(&channels);
        return EXIT_FAILURE;
    }
    if (!configure_satellite(*control, opt)) {
        free_channel_list(&channels);
        return EXIT_FAILURE;
    }
    if (!tls_server_init(&tls, opt)) {
        free_channel_list(&channels);
        return EXIT_FAILURE;
    }

    listener = create_http_listener(opt->http_bind_ip, opt->http_port,
                                    &bound_endpoint);
    if (listener == SOCKET_INVALID) {
        tls_server_cleanup(&tls);
        free_channel_list(&channels);
        return EXIT_FAILURE;
    }

    printf("\nSORALink VLC channel server is ready.\n");
    printf("Open this URL in VLC:\n");
    print_server_url(opt, &tls, "/playlist.m3u");
    printf("Other endpoints:\n");
    print_server_url(opt, &tls, "/tv.m3u");
    print_server_url(opt, &tls, "/radio.m3u");
    print_server_url(opt, &tls, "/status.json");
    printf("Up to %u clients may share the same tuned channel.\n",
           (unsigned)opt->max_http_clients);
    if (opt->http_user != NULL) {
        printf("HTTP Basic authentication is enabled.\n");
    }
    if (!tls.enabled && !endpoint_is_loopback(&bound_endpoint)) {
        fprintf(stderr,
                "Warning: HTTP is exposed beyond loopback without TLS. "
                "Use --tls-cert/--tls-key or a TLS reverse proxy.\n");
    }
    printf("Press Ctrl+C to stop.\n\n");

    while (g_running) {
        const size_t active_clients = http_stream_client_count(&stream);
        const int listener_wait = active_clients == 0U ? 500 : 0;
        int ready;
        double now = monotonic_seconds();

        if (now - last_heartbeat >= 5.0) {
            if (!send_heartbeat(*control, opt->verbose)) {
                fprintf(stderr, "Control heartbeat failed; attempting recovery.\n");
                if (!recover_http_control(control, opt, stream.channel, &tuning)) {
                    result = EXIT_FAILURE;
                    break;
                }
            }
            last_heartbeat = monotonic_seconds();
        }

        ready = wait_socket_readable(listener, listener_wait);
        if (ready < 0) {
            if (!g_running) {
                break;
            }
            fprintf(stderr, "HTTP select() failed: %d\n", SOCKET_ERROR_CODE());
            result = EXIT_FAILURE;
            break;
        }
        if (ready > 0) {
            http_connection_t connection;
            char request[16384];
            http_request_line_t request_line;
            bool keep_connection = false;
            bool head_only = false;

            if (http_accept_connection(listener, &tls, opt->timeout_ms,
                                       &connection)) {
                if (!read_http_request(&connection, request, sizeof(request)) ||
                    !parse_http_request_line(request, &request_line)) {
                    send_http_error(&connection, 400, "Bad Request",
                                    "Bad or oversized HTTP request.\n", false);
                } else {
                    char *query = strchr(request_line.path, '?');
                    if (query != NULL) {
                        *query = '\0';
                    }
                    head_only = request_line.method == HTTP_METHOD_HEAD;
                    if (!http_authorized(request, opt)) {
                        send_http_unauthorized(&connection, head_only);
                    } else if (request_line.method == HTTP_METHOD_OPTIONS) {
                        send_http_response(
                            &connection, 204, "No Content", "text/plain",
                            "Allow: GET, HEAD, OPTIONS\r\n", "", true);
                    } else if (request_line.method == HTTP_METHOD_OTHER) {
                        send_http_response(
                            &connection, 405, "Method Not Allowed",
                            "text/plain; charset=utf-8",
                            "Allow: GET, HEAD, OPTIONS\r\n",
                            "Only GET, HEAD, and OPTIONS are supported.\n",
                            false);
                    } else if (strcmp(request_line.path, "/playlist.m3u") == 0 ||
                               strcmp(request_line.path, "/") == 0) {
                        serve_playlist(&connection, &channels, opt, &tls,
                                       request, NULL, head_only);
                    } else if (strcmp(request_line.path, "/tv.m3u") == 0) {
                        serve_playlist(&connection, &channels, opt, &tls,
                                       request, "TV", head_only);
                    } else if (strcmp(request_line.path, "/radio.m3u") == 0) {
                        serve_playlist(&connection, &channels, opt, &tls,
                                       request, "RADIO", head_only);
                    } else if (strcmp(request_line.path, "/status.json") == 0) {
                        serve_status(&connection, &stream, head_only);
                    } else if (strncmp(request_line.path, "/channel/", 9) == 0) {
                        uint32_t lcn;
                        const channel_t *channel;
                        if (!parse_u32(request_line.path + 9, &lcn) ||
                            (channel = find_channel_by_lcn(&channels, lcn)) == NULL) {
                            send_http_error(&connection, 404, "Not Found",
                                            "Unknown channel number.\n", head_only);
                        } else if (head_only) {
                            send_http_response(&connection, 200, "OK",
                                               "video/MP2T", NULL, "", true);
                        } else if (stream.channel != NULL &&
                                   stream.channel->lcn != channel->lcn &&
                                   http_stream_client_count(&stream) > 0U) {
                            char message[512];
                            snprintf(message, sizeof(message),
                                     "The tuner is busy streaming channel %u (%s).\n",
                                     (unsigned)stream.channel->lcn,
                                     stream.channel->name);
                            send_http_error(&connection, 409, "Conflict",
                                            message, false);
                        } else if (http_stream_client_count(&stream) >=
                                   stream.max_clients) {
                            send_http_error(&connection, 503,
                                            "Service Unavailable",
                                            "The HTTP stream client limit was reached.\n",
                                            false);
                        } else {
                            if (http_stream_client_count(&stream) == 0U) {
                                bool tuned = tune_channel(*control, opt, channel,
                                                          &tuning);
                                if (!tuned) {
                                    fprintf(stderr,
                                            "Channel tune failed; reconnecting and retrying.\n");
                                    tuned = recover_http_control(control, opt,
                                                                 channel, &tuning);
                                    last_heartbeat = monotonic_seconds();
                                }
                                if (!tuned ||
                                    !http_stream_open_udp(&stream,
                                                          opt->timeout_ms)) {
                                    send_http_error(&connection, 503,
                                                    "Service Unavailable",
                                                    "SORALink could not lock this channel.\n",
                                                    false);
                                } else {
                                    stream.channel = channel;
                                    stream.retry_flag = 0;
                                    stream.consecutive_timeouts = 0;
                                    if (send_stream_headers(&connection) &&
                                        http_stream_add_client(&stream,
                                                               &connection)) {
                                        keep_connection = true;
                                        printf("\nClient joined channel %u (%s).\n",
                                               (unsigned)channel->lcn,
                                               channel->name);
                                    }
                                }
                            } else if (send_stream_headers(&connection) &&
                                       http_stream_add_client(&stream,
                                                              &connection)) {
                                keep_connection = true;
                                printf("\nClient joined existing channel %u (%s).\n",
                                       (unsigned)channel->lcn, channel->name);
                            }
                        }
                    } else {
                        send_http_error(&connection, 404, "Not Found",
                                        "Use /playlist.m3u, /tv.m3u, /radio.m3u, "
                                        "/status.json, or /channel/<number>.\n",
                                        head_only);
                    }
                }
                if (!keep_connection) {
                    http_connection_close(&connection);
                }
            }
        }

        if (http_stream_client_count(&stream) > 0U) {
            const stream_step_t step = http_stream_service(&stream, opt);
            if (step == STREAM_STEP_RECOVER) {
                fprintf(stderr,
                        "Stream transport stalled; reconnecting and retuning.\n");
                if (!recover_http_control(control, opt, stream.channel, &tuning)) {
                    http_stream_close_clients(&stream);
                    result = EXIT_FAILURE;
                    break;
                }
                stream.consecutive_timeouts = 0;
                stream.retry_flag = 0;
                last_heartbeat = monotonic_seconds();
            } else if (step == STREAM_STEP_FATAL) {
                result = EXIT_FAILURE;
                break;
            }
        } else {
            http_stream_stop_if_idle(&stream);
        }
    }

    printf("\nStopping HTTP server.\n");
    http_stream_close_clients(&stream);
    if (stream.dongle_udp != SOCKET_INVALID) {
        CLOSESOCKET(stream.dongle_udp);
    }
    if (listener != SOCKET_INVALID) {
        CLOSESOCKET(listener);
    }
    tls_server_cleanup(&tls);
    free_channel_list(&channels);
    return result;
}

static bool run_self_tests(void)
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
    uint8_t packet[TS_PACKET_SIZE];
    uint8_t output[TS_PACKET_SIZE];
    uint8_t parsed_key[16];
    stream_keys_t keys;
    stream_stats_t stats;
    channel_t channel;
    char encoded[64];
    uint8_t response[17];
    uint8_t frame[FRAME_SIZE];
    bool ok = true;

#define TEST_CHECK(condition, description) \
    do { \
        if (condition) { \
            printf("[PASS] %s\n", description); \
        } else { \
            fprintf(stderr, "[FAIL] %s\n", description); \
            ok = false; \
        } \
    } while (0)

    TEST_CHECK(aes128_self_test(), "AES-128 known-answer decryption");
    TEST_CHECK(parse_hex_key("00:01:02:03:04:05:06:07:08:09:0a:0b:0c:0d:0e:0f",
                             parsed_key) &&
               memcmp(parsed_key, key, sizeof(key)) == 0,
               "AES key parser accepts separated hexadecimal");

    memset(&keys, 0, sizeof(keys));
    keys.have_even = true;
    keys.missing_policy = MISSING_KEY_PASS;
    aes128_init(&keys.even, key);
    memset(&stats, 0, sizeof(stats));
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x47;
    packet[3] = 0x90;
    memcpy(packet + 4, ciphertext, sizeof(ciphertext));
    TEST_CHECK(transform_ts_packet(&keys, packet, output, &stats) &&
               (output[3] & 0xC0U) == 0U &&
               memcmp(output + 4, plaintext, sizeof(plaintext)) == 0 &&
               stats.even_decrypted == 1U,
               "Even-key packet decrypts and clears scrambling only afterward");

    memset(&keys, 0, sizeof(keys));
    keys.missing_policy = MISSING_KEY_PASS;
    memset(&stats, 0, sizeof(stats));
    packet[3] = 0xD0;
    TEST_CHECK(transform_ts_packet(&keys, packet, output, &stats) &&
               (output[3] & 0xC0U) == 0xC0U &&
               stats.missing_odd_key == 1U,
               "Missing odd key preserves the scrambling marker in pass mode");

    keys.missing_policy = MISSING_KEY_DROP;
    memset(&stats, 0, sizeof(stats));
    TEST_CHECK(!transform_ts_packet(&keys, packet, output, &stats),
               "Missing odd key drops the packet in drop mode");

    memset(&keys, 0, sizeof(keys));
    keys.have_odd = true;
    keys.missing_policy = MISSING_KEY_PASS;
    aes128_init(&keys.odd, key);
    memset(&stats, 0, sizeof(stats));
    TEST_CHECK(transform_ts_packet(&keys, packet, output, &stats) &&
               (output[3] & 0xC0U) == 0U &&
               memcmp(output + 4, plaintext, sizeof(plaintext)) == 0 &&
               stats.odd_decrypted == 1U,
               "Odd-key packet decrypts when an odd key is configured");

    TEST_CHECK(parse_channel_tag(
        "<ch\n type='tv' pol='F' sym='22000' s_id='101'\n"
        " freq='11500' lcn='7' s_name='News &amp; Sport &#x2605;' fta='1'>",
        &channel) && channel.polarization == 'F' &&
        strcmp(channel.type, "TV") == 0 &&
        strstr(channel.name, "News & Sport") != NULL && channel.fta,
        "Multiline XML attributes and named/numeric entities");

    TEST_CHECK(base64_encode((const uint8_t *)"user:pass", 9,
                             encoded, sizeof(encoded)) > 0U &&
               strcmp(encoded, "dXNlcjpwYXNz") == 0,
               "HTTP Basic-auth Base64 encoding");

    build_permission_claim(frame);
    memset(response, 0, sizeof(response));
    memcpy(response + 4, "USBS", 4);
    memcpy(response + 8, frame + 4, 4);
    TEST_CHECK(validate_csw(response, sizeof(response), 4, frame),
               "Strict CSW signature, tag, residue, and status validation");

#undef TEST_CHECK
    printf("Self-test result: %s\n", ok ? "PASS" : "FAIL");
    return ok;
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
    if (opt.self_test) {
        result = run_self_tests() ? EXIT_SUCCESS : EXIT_FAILURE;
        goto cleanup;
    }

    printf("Connecting to device at %s:%u...\n",
           opt.device_ip, (unsigned)opt.control_port);

    control = connect_tcp(opt.device_ip, opt.control_port, opt.timeout_ms);
    if (control == SOCKET_INVALID) {
        fprintf(stderr,
                "\nThe device control port is not reachable. Confirm the host, "
                "port, and that the vendor application is fully closed.\n");
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
        result = run_http_server(&control, &opt);
        goto cleanup;
    }

    if (!tune_device(control, &opt)) {
        goto cleanup;
    }

    result = stream_to_vlc(&control, &opt);

cleanup:
    if (control != SOCKET_INVALID) {
        CLOSESOCKET(control);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}

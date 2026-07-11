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
#include <stdarg.h>

#ifdef SORALINK_USE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
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
#define HTTP_DOWNLOAD_HEADER_LIMIT (64U * 1024U)
#define HTTP_REDIRECT_LIMIT    5

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
    char device_ip[512];
    char vlc_ip[512];
    char dump_path[1024];
    char channels_path[1024];
    char http_bind_ip[512];
    char http_user[256];
    char http_password[256];
    char admin_user[256];
    char admin_password[256];
    char tls_cert_path[1024];
    char tls_key_path[1024];
    char web_root[1024];
    char epg_path[1024];
    char epg_url[2048];
} option_text_storage_t;

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
    const char *admin_user;
    const char *admin_password;
    bool web_ui;
    const char *web_root;
    const char *epg_path;
    bool epg_update;
    const char *epg_url;
    int epg_update_timeout_ms;
    bool device_channels_update;
    bool device_epg_update;
    uint32_t device_channels_refresh_minutes;
    uint32_t device_epg_refresh_minutes;
    uint32_t device_scan_refresh_minutes;
    uint32_t device_scan_timeout_minutes;
    uint32_t device_scan_search_range;
    uint32_t device_scan_order_by;
    uint32_t device_scan_sort_mode;
    uint32_t device_scan_mode;
    bool device_scan_network;
    bool device_scan_epg_after;
    bool device_scan_apply_satellite;
    bool device_update_on_start;
    const char *tls_cert_path;
    const char *tls_key_path;
    const char *config_path;
    option_text_storage_t text;
} options_t;

typedef struct {
    char type[8];
    char polarization;
    uint32_t symbol_rate_ks;
    uint32_t service_id;
    uint32_t frequency_mhz;
    uint32_t lcn;
    uint32_t program_index;
    uint32_t transport_stream_id;
    bool have_program_index;
    bool fta;
    char name[256];
    char epg_id[256];
} channel_t;

typedef struct {
    channel_t *items;
    size_t count;
} channel_list_t;

typedef struct {
    char channel_id[256];
    int64_t start_utc;
    int64_t stop_utc;
    char title[256];
    char subtitle[256];
    char description[768];
    char category[128];
} epg_program_t;

typedef struct {
    epg_program_t *items;
    size_t count;
    int64_t loaded_utc;
} epg_list_t;

typedef struct {
    bool busy;
    int64_t last_channels_attempt_utc;
    int64_t last_channels_success_utc;
    int64_t last_epg_attempt_utc;
    int64_t last_epg_success_utc;
    size_t last_epg_updated_channels;
    size_t last_epg_skipped_channels;
    size_t last_epg_failed_channels;
    char last_action[64];
    char last_message[256];
    double next_channels_due;
    double next_epg_due;
    double next_scan_due;
    unsigned epg_length_response_bytes;

    bool scan_running;
    bool scan_cancel_requested;
    int64_t last_scan_attempt_utc;
    int64_t last_scan_success_utc;
    unsigned scan_progress;
    unsigned scan_state;
    uint32_t scan_frequency_mhz;
    uint32_t scan_symbol_rate_ks;
    unsigned scan_mode;
    unsigned scan_tv_count;
    unsigned scan_radio_count;
    double scan_started;
    double scan_next_poll;
    double scan_deadline;

    bool scan_transponder_sweep;
    size_t scan_sweep_index;
    size_t scan_sweep_total;
    channel_list_t scan_sweep_channels;
} device_update_state_t;

typedef struct {
    bool valid;
    uint32_t frequency_mhz;
    uint32_t symbol_rate_ks;
    char polarization;
    uint32_t service_id;
} tuning_state_t;

typedef enum {
    TUNE_RESULT_OK = 0,
    TUNE_RESULT_NO_LOCK,
    TUNE_RESULT_CONTROL_ERROR
} tune_result_t;

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
        "  %s --device HOST --server --channels channels.xml [options]\n"
        "  %s --config soralink.conf [command-line overrides]\n\n"
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
        "  --missing-key MODE   pass or drop encrypted packets (default pass)\n\n",
        program, program, program, program, program, program, program);

    fprintf(stderr,
        "HTTP server options:\n"
        "  --server             Run the local HTTP playlist/stream server\n"
        "  --channels FILE      Channel XML file (default channels.xml)\n"
        "  --http-bind HOST     Listen address/name (default 127.0.0.1)\n"
        "  --http-port N        HTTP listen port (default 8080)\n"
        "  --max-clients N      Simultaneous viewers of one channel (default 8)\n"
        "  --http-user USER     Viewer HTTP Basic username\n"
        "  --http-password PASS Viewer HTTP Basic password\n"
        "  --admin-user USER    Web UI administrator username\n"
        "  --admin-password PASS Web UI administrator password\n"
        "  --webui              Enable the administration Web UI (default)\n"
        "  --no-webui           Disable the administration Web UI\n"
        "  --web-root DIR       Web assets directory (default: web beside binary)\n"
        "  --epg FILE           Optional XMLTV EPG file (default: epg.xml beside binary)\n"
        "  --epg-update         Download the XMLTV file once before server startup\n"
        "  --no-epg-update      Do not download XMLTV at startup (default)\n"
        "  --epg-url URL        HTTP/HTTPS XMLTV URL used by --epg-update\n"
        "  --epg-update-timeout-ms N  EPG download timeout (default 30000)\n"
        "  --device-channels-update  Refresh channel XML from the Device\n"
        "  --no-device-channels-update  Disable native channel refresh (default)\n"
        "  --device-epg-update   Refresh XMLTV from the Device EPG cache\n"
        "  --no-device-epg-update  Disable native EPG refresh (default)\n"
        "  --device-channels-refresh-minutes N  Periodic channel refresh; 0 disables\n"
        "  --device-epg-refresh-minutes N  Periodic EPG refresh; 0 disables\n"
        "  --device-scan-refresh-minutes N  Periodic receiver scan; 0 disables\n"
        "  --device-scan-timeout-minutes N  Scan timeout (default 30)\n"
        "  --device-scan-mode N  110=automatic satellite scan, 0=compatibility scan (126 accepted as legacy alias)\n"
        "  --device-scan-sort-mode N  Receiver sorting code 0..3\n"
        "  --device-scan-network | --no-device-scan-network\n"
        "  --device-scan-epg-after | --no-device-scan-epg-after\n"
        "  --device-scan-apply-satellite | --no-device-scan-apply-satellite\n"
        "  --device-scan-search-range N  Receiver scan range code 0..7\n"
        "  --device-scan-order-by N  Receiver ordering code 0..3\n"
        "  --device-update-on-start | --no-device-update-on-start\n"
        "  --tls-cert FILE      TLS certificate PEM (requires OpenSSL build)\n"
        "  --tls-key FILE       TLS private-key PEM (requires OpenSSL build)\n\n"
        "Configuration:\n"
        "  --config FILE        Load key=value settings before CLI overrides\n\n"
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
        "  /status.json         Current channel/client status\n"
        "  /admin/              Administration dashboard and viewer controls\n"
        "  /admin/api/status    Dashboard JSON API\n"
        "  /admin/api/epg/LCN   XMLTV schedule for a channel\n"
        "  POST /admin/device/update-channels  Refresh lineup from device\n"
        "  POST /admin/device/update-epg       Refresh EPG from device\n"
        "  POST /admin/device/update-all      Refresh channels and EPG\n"
        "  POST /admin/device/scan            Start receiver channel scan\n"
        "  POST /admin/device/scan-cancel     Cancel receiver channel scan\n");
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

static bool normalize_device_scan_mode(uint32_t *value)
{
    if (*value == 0U || *value == 110U) return true;
    if (*value == 126U) {
        *value = 110U;
        return true;
    }
    return false;
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

static char *trim_config_text(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

static bool parse_bool_value(const char *text, bool *value)
{
    if (STRNCASECMP(text, "true", 5) == 0 && text[4] == '\0') {
        *value = true;
        return true;
    }
    if (STRNCASECMP(text, "yes", 4) == 0 && text[3] == '\0') {
        *value = true;
        return true;
    }
    if (strcmp(text, "1") == 0 ||
        (STRNCASECMP(text, "on", 3) == 0 && text[2] == '\0')) {
        *value = true;
        return true;
    }
    if (STRNCASECMP(text, "false", 6) == 0 && text[5] == '\0') {
        *value = false;
        return true;
    }
    if (STRNCASECMP(text, "no", 3) == 0 && text[2] == '\0') {
        *value = false;
        return true;
    }
    if (strcmp(text, "0") == 0 ||
        (STRNCASECMP(text, "off", 4) == 0 && text[3] == '\0')) {
        *value = false;
        return true;
    }
    return false;
}

static bool store_config_text(char *storage,
                              size_t storage_capacity,
                              const char *value,
                              const char **target,
                              bool empty_is_null)
{
    const size_t length = strlen(value);
    if (length + 1U > storage_capacity) {
        return false;
    }
    memcpy(storage, value, length + 1U);
    *target = empty_is_null && length == 0U ? NULL : storage;
    return true;
}

static void normalize_config_key(char *key)
{
    while (*key != '\0') {
        if (*key == '-') {
            *key = '_';
        } else {
            *key = (char)tolower((unsigned char)*key);
        }
        ++key;
    }
}

static bool apply_config_option(options_t *opt,
                                char *key,
                                const char *value,
                                bool *have_freq,
                                bool *have_sr,
                                bool *have_pol,
                                bool *have_sid)
{
    uint32_t number;
    bool flag;

    normalize_config_key(key);
    if (strcmp(key, "device") == 0 || strcmp(key, "device_ip") == 0) {
        return store_config_text(opt->text.device_ip,
                                 sizeof(opt->text.device_ip), value,
                                 &opt->device_ip, true);
    }
    if (strcmp(key, "control_port") == 0) {
        return parse_port(value, &opt->control_port);
    }
    if (strcmp(key, "stream_port") == 0) {
        return parse_port(value, &opt->stream_port);
    }
    if (strcmp(key, "vlc_ip") == 0) {
        return store_config_text(opt->text.vlc_ip,
                                 sizeof(opt->text.vlc_ip), value,
                                 &opt->vlc_ip, false);
    }
    if (strcmp(key, "vlc_port") == 0) {
        return parse_port(value, &opt->vlc_port);
    }
    if (strcmp(key, "server") == 0 || strcmp(key, "http_server") == 0) {
        if (!parse_bool_value(value, &flag)) {
            return false;
        }
        opt->http_server = flag;
        if (flag) {
            opt->tune_mode = TUNE_NONE;
        }
        return true;
    }
    if (strcmp(key, "channels") == 0 || strcmp(key, "channels_path") == 0) {
        return store_config_text(opt->text.channels_path,
                                 sizeof(opt->text.channels_path), value,
                                 &opt->channels_path, false);
    }
    if (strcmp(key, "http_bind") == 0 || strcmp(key, "http_bind_ip") == 0) {
        return store_config_text(opt->text.http_bind_ip,
                                 sizeof(opt->text.http_bind_ip), value,
                                 &opt->http_bind_ip, false);
    }
    if (strcmp(key, "http_port") == 0) {
        return parse_port(value, &opt->http_port);
    }
    if (strcmp(key, "max_clients") == 0) {
        return parse_u32(value, &opt->max_http_clients) &&
               opt->max_http_clients > 0U &&
               opt->max_http_clients <= HTTP_CLIENT_LIMIT;
    }
    if (strcmp(key, "http_user") == 0) {
        return store_config_text(opt->text.http_user,
                                 sizeof(opt->text.http_user), value,
                                 &opt->http_user, true);
    }
    if (strcmp(key, "http_password") == 0) {
        return store_config_text(opt->text.http_password,
                                 sizeof(opt->text.http_password), value,
                                 &opt->http_password, true);
    }
    if (strcmp(key, "admin_user") == 0) {
        return store_config_text(opt->text.admin_user,
                                 sizeof(opt->text.admin_user), value,
                                 &opt->admin_user, true);
    }
    if (strcmp(key, "admin_password") == 0) {
        return store_config_text(opt->text.admin_password,
                                 sizeof(opt->text.admin_password), value,
                                 &opt->admin_password, true);
    }
    if (strcmp(key, "webui") == 0 || strcmp(key, "web_ui") == 0) {
        return parse_bool_value(value, &opt->web_ui);
    }
    if (strcmp(key, "web_root") == 0 || strcmp(key, "webroot") == 0) {
        return store_config_text(opt->text.web_root,
                                 sizeof(opt->text.web_root), value,
                                 &opt->web_root, false);
    }
    if (strcmp(key, "epg") == 0 || strcmp(key, "epg_path") == 0 ||
        strcmp(key, "xmltv") == 0) {
        return store_config_text(opt->text.epg_path,
                                 sizeof(opt->text.epg_path), value,
                                 &opt->epg_path, true);
    }
    if (strcmp(key, "epg_update") == 0 ||
        strcmp(key, "epg_update_on_start") == 0 ||
        strcmp(key, "epg_download") == 0) {
        return parse_bool_value(value, &opt->epg_update);
    }
    if (strcmp(key, "epg_url") == 0 || strcmp(key, "xmltv_url") == 0) {
        return store_config_text(opt->text.epg_url,
                                 sizeof(opt->text.epg_url), value,
                                 &opt->epg_url, true);
    }
    if (strcmp(key, "epg_update_timeout_ms") == 0 ||
        strcmp(key, "epg_download_timeout_ms") == 0) {
        return parse_u32(value, &number) && number >= 1000U &&
               number <= 120000U &&
               (opt->epg_update_timeout_ms = (int)number, true);
    }
    if (strcmp(key, "device_channels_update") == 0 ||
        strcmp(key, "native_channels_update") == 0) {
        return parse_bool_value(value, &opt->device_channels_update);
    }
    if (strcmp(key, "device_epg_update") == 0 ||
        strcmp(key, "native_epg_update") == 0) {
        return parse_bool_value(value, &opt->device_epg_update);
    }
    if (strcmp(key, "device_channels_refresh_minutes") == 0) {
        return parse_u32(value, &opt->device_channels_refresh_minutes) &&
               opt->device_channels_refresh_minutes <= 10080U;
    }
    if (strcmp(key, "device_epg_refresh_minutes") == 0) {
        return parse_u32(value, &opt->device_epg_refresh_minutes) &&
               opt->device_epg_refresh_minutes <= 10080U;
    }
    if (strcmp(key, "device_scan_refresh_minutes") == 0) {
        return parse_u32(value, &opt->device_scan_refresh_minutes) &&
               opt->device_scan_refresh_minutes <= 10080U;
    }
    if (strcmp(key, "device_scan_timeout_minutes") == 0) {
        return parse_u32(value, &opt->device_scan_timeout_minutes) &&
               opt->device_scan_timeout_minutes >= 1U &&
               opt->device_scan_timeout_minutes <= 120U;
    }
    if (strcmp(key, "device_scan_search_range") == 0) {
        return parse_u32(value, &opt->device_scan_search_range) &&
               opt->device_scan_search_range <= 7U;
    }
    if (strcmp(key, "device_scan_order_by") == 0) {
        return parse_u32(value, &opt->device_scan_order_by) &&
               opt->device_scan_order_by <= 3U;
    }
    if (strcmp(key, "device_scan_sort_mode") == 0) {
        return parse_u32(value, &opt->device_scan_sort_mode) &&
               opt->device_scan_sort_mode <= 3U;
    }
    if (strcmp(key, "device_scan_mode") == 0) {
        if (!parse_u32(value, &number) ||
            !normalize_device_scan_mode(&number)) {
            return false;
        }
        opt->device_scan_mode = number;
        return true;
    }
    if (strcmp(key, "device_scan_network") == 0) {
        return parse_bool_value(value, &opt->device_scan_network);
    }
    if (strcmp(key, "device_scan_epg_after") == 0) {
        return parse_bool_value(value, &opt->device_scan_epg_after);
    }
    if (strcmp(key, "device_scan_apply_satellite") == 0) {
        return parse_bool_value(value, &opt->device_scan_apply_satellite);
    }
    if (strcmp(key, "device_update_on_start") == 0) {
        return parse_bool_value(value, &opt->device_update_on_start);
    }
    if (strcmp(key, "tls_cert") == 0 || strcmp(key, "tls_cert_path") == 0) {
        return store_config_text(opt->text.tls_cert_path,
                                 sizeof(opt->text.tls_cert_path), value,
                                 &opt->tls_cert_path, true);
    }
    if (strcmp(key, "tls_key") == 0 || strcmp(key, "tls_key_path") == 0) {
        return store_config_text(opt->text.tls_key_path,
                                 sizeof(opt->text.tls_key_path), value,
                                 &opt->tls_key_path, true);
    }
    if (strcmp(key, "progidx") == 0 || strcmp(key, "program_index") == 0) {
        if (!parse_u32(value, &number) || number > INT32_MAX) {
            return false;
        }
        opt->program_index = (int)number;
        opt->tune_mode = TUNE_PROGIDX;
        return true;
    }
    if (strcmp(key, "freq") == 0 || strcmp(key, "frequency_mhz") == 0) {
        if (!parse_u32(value, &opt->frequency_mhz)) {
            return false;
        }
        *have_freq = true;
        return true;
    }
    if (strcmp(key, "sr") == 0 || strcmp(key, "symbol_rate_ks") == 0) {
        if (!parse_u32(value, &opt->symbol_rate_ks)) {
            return false;
        }
        *have_sr = true;
        return true;
    }
    if (strcmp(key, "pol") == 0 || strcmp(key, "polarization") == 0) {
        if (!parse_polarization(value, &opt->polarization)) {
            return false;
        }
        *have_pol = true;
        return true;
    }
    if (strcmp(key, "sid") == 0 || strcmp(key, "service_id") == 0) {
        if (!parse_u32(value, &opt->service_id)) {
            return false;
        }
        *have_sid = true;
        return true;
    }
    if (strcmp(key, "orbital") == 0 || strcmp(key, "orbital_tenths") == 0) {
        return parse_u32(value, &opt->orbital_tenths) &&
               opt->orbital_tenths <= 32767U;
    }
    if (strcmp(key, "west") == 0) {
        return parse_bool_value(value, &opt->west);
    }
    if (strcmp(key, "tone") == 0) {
        if (strcmp(value, "auto") == 0) {
            opt->tone = TONE_AUTO;
        } else if (strcmp(value, "on") == 0) {
            opt->tone = TONE_ON;
        } else if (strcmp(value, "off") == 0) {
            opt->tone = TONE_OFF;
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(key, "lnb_low") == 0 || strcmp(key, "lnb_low_mhz") == 0) {
        return parse_u32(value, &opt->lnb_low_mhz) &&
               opt->lnb_low_mhz <= 65535U;
    }
    if (strcmp(key, "lnb_high") == 0 || strcmp(key, "lnb_high_mhz") == 0) {
        return parse_u32(value, &opt->lnb_high_mhz) &&
               opt->lnb_high_mhz <= 65535U;
    }
    if (strcmp(key, "lnb_switch") == 0 || strcmp(key, "lnb_switch_mhz") == 0) {
        return parse_u32(value, &opt->lnb_switch_mhz) &&
               opt->lnb_switch_mhz <= 65535U;
    }
    if (strcmp(key, "diseqc") == 0 || strcmp(key, "diseqc_port") == 0) {
        return parse_u32(value, &opt->diseqc_port) && opt->diseqc_port <= 255U;
    }
    if (strcmp(key, "sat_setup") == 0 || strcmp(key, "satellite_setup") == 0) {
        return parse_bool_value(value, &opt->satellite_setup);
    }
    if (strcmp(key, "even_key") == 0) {
        if (!parse_hex_key(value, opt->even_key)) {
            return false;
        }
        opt->have_even_key = true;
        return true;
    }
    if (strcmp(key, "odd_key") == 0) {
        if (!parse_hex_key(value, opt->odd_key)) {
            return false;
        }
        opt->have_odd_key = true;
        return true;
    }
    if (strcmp(key, "use_default_key") == 0) {
        if (!parse_bool_value(value, &flag)) {
            return false;
        }
        if (flag) {
            memcpy(opt->even_key, device_default_key, sizeof(opt->even_key));
            opt->have_even_key = true;
        } else {
            memset(opt->even_key, 0, sizeof(opt->even_key));
            opt->have_even_key = false;
        }
        return true;
    }
    if (strcmp(key, "missing_key") == 0) {
        if (strcmp(value, "pass") == 0) {
            opt->missing_key_policy = MISSING_KEY_PASS;
        } else if (strcmp(value, "drop") == 0) {
            opt->missing_key_policy = MISSING_KEY_DROP;
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(key, "dump") == 0 || strcmp(key, "dump_path") == 0) {
        return store_config_text(opt->text.dump_path,
                                 sizeof(opt->text.dump_path), value,
                                 &opt->dump_path, true);
    }
    if (strcmp(key, "wait_ms") == 0) {
        return parse_u32(value, &number) && number <= 60000U &&
               (opt->wait_ms = (int)number, true);
    }
    if (strcmp(key, "timeout_ms") == 0) {
        return parse_u32(value, &number) && number >= 100U &&
               number <= 60000U && (opt->timeout_ms = (int)number, true);
    }
    if (strcmp(key, "probe") == 0) {
        if (!parse_bool_value(value, &opt->probe_only)) {
            return false;
        }
        if (opt->probe_only) {
            opt->tune_mode = TUNE_NONE;
        }
        return true;
    }
    if (strcmp(key, "verbose") == 0) {
        return parse_bool_value(value, &opt->verbose);
    }
    return false;
}

static bool load_config_file(options_t *opt,
                             const char *path,
                             bool *have_freq,
                             bool *have_sr,
                             bool *have_pol,
                             bool *have_sid)
{
    FILE *file = fopen(path, "rb");
    char line[4096];
    unsigned line_number = 0;

    if (file == NULL) {
        fprintf(stderr, "Cannot open config file '%s': %s\n", path,
                strerror(errno));
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text;
        char *equals;
        char *key;
        char *value;
        size_t length;
        ++line_number;
        length = strlen(line);
        if (length > 0U && line[length - 1U] != '\n' && !feof(file)) {
            fprintf(stderr, "Config line %u is too long.\n", line_number);
            fclose(file);
            return false;
        }
        text = trim_config_text(line);
        if (*text == '\0' || *text == '#' || *text == ';' || *text == '[') {
            continue;
        }
        equals = strchr(text, '=');
        if (equals == NULL) {
            fprintf(stderr, "Config line %u must use key=value syntax.\n",
                    line_number);
            fclose(file);
            return false;
        }
        *equals = '\0';
        key = trim_config_text(text);
        value = trim_config_text(equals + 1);
        length = strlen(value);
        if (length >= 2U &&
            ((value[0] == '"' && value[length - 1U] == '"') ||
             (value[0] == '\'' && value[length - 1U] == '\''))) {
            value[length - 1U] = '\0';
            ++value;
        }
        if (*key == '\0' || !apply_config_option(opt, key, value,
                                                  have_freq, have_sr,
                                                  have_pol, have_sid)) {
            fprintf(stderr, "Invalid or unsupported config option on line %u.\n",
                    line_number);
            fclose(file);
            return false;
        }
    }
    if (ferror(file)) {
        fprintf(stderr, "Error reading config file '%s'.\n", path);
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

static bool set_program_relative_path(char *storage,
                                      size_t storage_capacity,
                                      const char *program,
                                      const char *leaf,
                                      const char **target)
{
    const char *slash = strrchr(program, '/');
#ifdef _WIN32
    const char *backslash = strrchr(program, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    if (slash == NULL) {
        return store_config_text(storage, storage_capacity, leaf, target, false);
    }
    {
        const size_t directory_length = (size_t)(slash - program + 1);
        const size_t leaf_length = strlen(leaf);
        if (directory_length + leaf_length + 1U > storage_capacity) {
            return false;
        }
        memcpy(storage, program, directory_length);
        memcpy(storage + directory_length, leaf, leaf_length + 1U);
        *target = storage;
        return true;
    }
}

static bool parse_options(int argc, char **argv, options_t *opt)
{
    int i;
    const char *config_path = NULL;
    bool have_freq = false;
    bool have_sr = false;
    bool have_pol = false;
    bool have_sid = false;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--config requires a file path.\n");
                return false;
            }
            config_path = argv[++i];
        }
    }

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
    opt->epg_update_timeout_ms = 30000;
    opt->device_channels_refresh_minutes = 1440U;
    opt->device_epg_refresh_minutes = 240U;
    opt->device_scan_refresh_minutes = 0U;
    opt->device_scan_timeout_minutes = 45U;
    opt->device_scan_search_range = 0U;
    opt->device_scan_order_by = 0U;
    opt->device_scan_sort_mode = 0U;
    opt->device_scan_mode = 110U;
    opt->device_scan_network = true;
    opt->device_scan_epg_after = true;
    opt->device_scan_apply_satellite = false;
    opt->device_update_on_start = true;
    opt->program_index = -1;
    opt->channels_path = "channels.xml";
    opt->http_bind_ip = "127.0.0.1";
    opt->http_port = 8080;
    opt->max_http_clients = 8;
    opt->web_ui = true;
    opt->config_path = config_path;
    if (!set_program_relative_path(opt->text.web_root,
                                   sizeof(opt->text.web_root), argv[0],
                                   "web", &opt->web_root) ||
        !set_program_relative_path(opt->text.epg_path,
                                   sizeof(opt->text.epg_path), argv[0],
                                   "epg.xml", &opt->epg_path)) {
        fprintf(stderr, "Program path is too long.\n");
        return false;
    }

    if (config_path != NULL &&
        !load_config_file(opt, config_path, &have_freq, &have_sr,
                          &have_pol, &have_sid)) {
        return false;
    }

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (strcmp(arg, "--config") == 0 && i + 1 < argc) {
            ++i;
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
        } else if (strcmp(arg, "--admin-user") == 0 && i + 1 < argc) {
            opt->admin_user = argv[++i];
        } else if (strcmp(arg, "--admin-password") == 0 && i + 1 < argc) {
            opt->admin_password = argv[++i];
        } else if (strcmp(arg, "--webui") == 0) {
            opt->web_ui = true;
        } else if (strcmp(arg, "--no-webui") == 0) {
            opt->web_ui = false;
        } else if (strcmp(arg, "--web-root") == 0 && i + 1 < argc) {
            opt->web_root = argv[++i];
        } else if (strcmp(arg, "--epg") == 0 && i + 1 < argc) {
            opt->epg_path = argv[++i];
        } else if (strcmp(arg, "--epg-update") == 0) {
            opt->epg_update = true;
        } else if (strcmp(arg, "--no-epg-update") == 0) {
            opt->epg_update = false;
        } else if (strcmp(arg, "--epg-url") == 0 && i + 1 < argc) {
            opt->epg_url = argv[++i];
        } else if (strcmp(arg, "--epg-update-timeout-ms") == 0 && i + 1 < argc) {
            uint32_t value;
            if (!parse_u32(argv[++i], &value) || value < 1000U ||
                value > 120000U) {
                fprintf(stderr, "Invalid --epg-update-timeout-ms value.\n");
                return false;
            }
            opt->epg_update_timeout_ms = (int)value;
        } else if (strcmp(arg, "--device-channels-update") == 0) {
            opt->device_channels_update = true;
        } else if (strcmp(arg, "--no-device-channels-update") == 0) {
            opt->device_channels_update = false;
        } else if (strcmp(arg, "--device-epg-update") == 0) {
            opt->device_epg_update = true;
        } else if (strcmp(arg, "--no-device-epg-update") == 0) {
            opt->device_epg_update = false;
        } else if (strcmp(arg, "--device-channels-refresh-minutes") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_channels_refresh_minutes) ||
                opt->device_channels_refresh_minutes > 10080U) {
                fprintf(stderr, "Invalid --device-channels-refresh-minutes value.\n");
                return false;
            }
        } else if (strcmp(arg, "--device-epg-refresh-minutes") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_epg_refresh_minutes) ||
                opt->device_epg_refresh_minutes > 10080U) {
                fprintf(stderr, "Invalid --device-epg-refresh-minutes value.\n");
                return false;
            }
        } else if (strcmp(arg, "--device-scan-refresh-minutes") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_scan_refresh_minutes) ||
                opt->device_scan_refresh_minutes > 10080U) {
                fprintf(stderr, "Invalid --device-scan-refresh-minutes value.\n");
                return false;
            }
        } else if (strcmp(arg, "--device-scan-timeout-minutes") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_scan_timeout_minutes) ||
                opt->device_scan_timeout_minutes < 1U ||
                opt->device_scan_timeout_minutes > 120U) {
                fprintf(stderr, "Invalid --device-scan-timeout-minutes value.\n");
                return false;
            }
        } else if (strcmp(arg, "--device-scan-search-range") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_scan_search_range) ||
                opt->device_scan_search_range > 7U) {
                fprintf(stderr, "Invalid --device-scan-search-range value.\n");
                return false;
            }
        } else if (strcmp(arg, "--device-scan-order-by") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_scan_order_by) ||
                opt->device_scan_order_by > 3U) {
                fprintf(stderr, "Invalid --device-scan-order-by value.\n");
                return false;
            }
        } else if (strcmp(arg, "--device-scan-mode") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_scan_mode) ||
                !normalize_device_scan_mode(&opt->device_scan_mode)) {
                fprintf(stderr, "Invalid --device-scan-mode value (use 0 or 126; 110 is accepted as a legacy alias).\n");
                return false;
            }
        } else if (strcmp(arg, "--device-scan-sort-mode") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &opt->device_scan_sort_mode) ||
                opt->device_scan_sort_mode > 3U) {
                fprintf(stderr, "Invalid --device-scan-sort-mode value.\n");
                return false;
            }
        } else if (strcmp(arg, "--device-scan-network") == 0) {
            opt->device_scan_network = true;
        } else if (strcmp(arg, "--no-device-scan-network") == 0) {
            opt->device_scan_network = false;
        } else if (strcmp(arg, "--device-scan-epg-after") == 0) {
            opt->device_scan_epg_after = true;
        } else if (strcmp(arg, "--no-device-scan-epg-after") == 0) {
            opt->device_scan_epg_after = false;
        } else if (strcmp(arg, "--device-scan-apply-satellite") == 0) {
            opt->device_scan_apply_satellite = true;
        } else if (strcmp(arg, "--no-device-scan-apply-satellite") == 0) {
            opt->device_scan_apply_satellite = false;
        } else if (strcmp(arg, "--device-update-on-start") == 0) {
            opt->device_update_on_start = true;
        } else if (strcmp(arg, "--no-device-update-on-start") == 0) {
            opt->device_update_on_start = false;
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
        fprintf(stderr, "--device is required (or set device= in the config file).\n");
        return false;
    }
    if ((opt->http_user == NULL) != (opt->http_password == NULL)) {
        fprintf(stderr, "--http-user and --http-password must be used together.\n");
        return false;
    }
    if ((opt->admin_user == NULL) != (opt->admin_password == NULL)) {
        fprintf(stderr, "--admin-user and --admin-password must be used together.\n");
        return false;
    }
    if (opt->epg_update &&
        (opt->epg_url == NULL || *opt->epg_url == '\0')) {
        fprintf(stderr, "EPG startup update is enabled, but no epg_url/--epg-url was supplied.\n");
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

static void build_scan_satellite_config(uint8_t frame[FRAME_SIZE],
                                        const options_t *opt)
{
    build_frame(frame, 2, 0xF6, 9);
    put_orbital(&frame[21], opt->orbital_tenths, opt->west);
    frame[23] = build_satellite_flags('O', opt->tone);
}

static tone_mode_t resolve_tone_mode(const options_t *opt)
{
    if (opt->tone != TONE_AUTO) return opt->tone;
    if (opt->lnb_switch_mhz == 0U || opt->frequency_mhz == 0U)
        return TONE_AUTO;
    return opt->frequency_mhz >= opt->lnb_switch_mhz
        ? TONE_ON : TONE_OFF;
}

static void build_dvbs_tune(uint8_t frame[FRAME_SIZE], const options_t *opt)
{
    const tone_mode_t tone = resolve_tone_mode(opt);
    build_frame(frame, 32, 0xF6, 1);
    frame[21] = build_satellite_flags(opt->polarization, tone);
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

    if (sock == SOCKET_INVALID) {
        fprintf(stderr, "TCP control socket is not connected.\n");
        return false;
    }

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

    if (sock == SOCKET_INVALID) {
        fprintf(stderr, "TCP control socket is not connected.\n");
        return false;
    }

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


typedef struct {
    bool https;
    char host[512];
    uint16_t port;
    char path[2048];
} download_url_t;

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} download_buffer_t;

static void download_buffer_free(download_buffer_t *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        memset(buffer, 0, sizeof(*buffer));
    }
}

static bool download_buffer_append(download_buffer_t *buffer,
                                   const uint8_t *data,
                                   size_t length)
{
    const size_t maximum = XML_FILE_LIMIT + HTTP_DOWNLOAD_HEADER_LIMIT;
    size_t required;
    size_t capacity;
    uint8_t *replacement;

    if (length == 0U) {
        return true;
    }
    if (buffer->length > maximum || length > maximum - buffer->length) {
        fprintf(stderr, "EPG download exceeds the %u MiB limit.\n",
                (unsigned)(XML_FILE_LIMIT / (1024U * 1024U)));
        return false;
    }
    required = buffer->length + length;
    if (required <= buffer->capacity) {
        memcpy(buffer->data + buffer->length, data, length);
        buffer->length = required;
        return true;
    }
    capacity = buffer->capacity == 0U ? 65536U : buffer->capacity;
    while (capacity < required) {
        if (capacity > maximum / 2U) {
            capacity = maximum;
            break;
        }
        capacity *= 2U;
    }
    replacement = (uint8_t *)realloc(buffer->data, capacity);
    if (replacement == NULL) {
        fprintf(stderr, "Out of memory while downloading EPG data.\n");
        return false;
    }
    buffer->data = replacement;
    buffer->capacity = capacity;
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length = required;
    return true;
}

static bool parse_download_url(const char *url, download_url_t *parsed)
{
    const char *cursor;
    const char *authority_end;
    const char *host_start;
    const char *host_end;
    const char *port_text = NULL;
    size_t host_length;
    size_t path_length;
    uint16_t default_port;

    if (url == NULL || parsed == NULL) {
        return false;
    }
    memset(parsed, 0, sizeof(*parsed));
    if (STRNCASECMP(url, "http://", 7) == 0) {
        parsed->https = false;
        cursor = url + 7;
        default_port = 80;
    } else if (STRNCASECMP(url, "https://", 8) == 0) {
        parsed->https = true;
        cursor = url + 8;
        default_port = 443;
    } else {
        fprintf(stderr, "EPG URL must begin with http:// or https://.\n");
        return false;
    }

    authority_end = cursor + strcspn(cursor, "/?#");
    if (authority_end == cursor) {
        fprintf(stderr, "EPG URL has no host name.\n");
        return false;
    }
    if (memchr(cursor, '@', (size_t)(authority_end - cursor)) != NULL) {
        fprintf(stderr, "EPG URLs containing user information are not supported.\n");
        return false;
    }

    host_start = cursor;
    host_end = authority_end;
    if (*host_start == '[') {
        const char *closing = memchr(host_start, ']',
                                     (size_t)(authority_end - host_start));
        if (closing == NULL) {
            fprintf(stderr, "Malformed IPv6 host in EPG URL.\n");
            return false;
        }
        host_start++;
        host_end = closing;
        if (closing + 1 < authority_end) {
            if (closing[1] != ':') {
                return false;
            }
            port_text = closing + 2;
        }
    } else {
        const char *colon = NULL;
        const char *scan;
        for (scan = cursor; scan < authority_end; ++scan) {
            if (*scan == ':') {
                colon = scan;
            }
        }
        if (colon != NULL) {
            host_end = colon;
            port_text = colon + 1;
        }
    }

    host_length = (size_t)(host_end - host_start);
    if (host_length == 0U || host_length >= sizeof(parsed->host)) {
        fprintf(stderr, "EPG URL host is empty or too long.\n");
        return false;
    }
    memcpy(parsed->host, host_start, host_length);
    parsed->host[host_length] = '\0';
    parsed->port = default_port;
    if (port_text != NULL) {
        char port_buffer[16];
        size_t port_length = (size_t)(authority_end - port_text);
        if (port_length == 0U || port_length >= sizeof(port_buffer)) {
            return false;
        }
        memcpy(port_buffer, port_text, port_length);
        port_buffer[port_length] = '\0';
        if (!parse_port(port_buffer, &parsed->port)) {
            fprintf(stderr, "Invalid port in EPG URL.\n");
            return false;
        }
    }

    cursor = authority_end;
    if (*cursor == '\0' || *cursor == '#') {
        snprintf(parsed->path, sizeof(parsed->path), "/");
        return true;
    }
    if (*cursor == '?') {
        if (snprintf(parsed->path, sizeof(parsed->path), "/%s", cursor) < 0 ||
            strlen(parsed->path) >= sizeof(parsed->path) - 1U) {
            return false;
        }
    } else {
        const char *fragment = strchr(cursor, '#');
        path_length = fragment != NULL ? (size_t)(fragment - cursor) : strlen(cursor);
        if (path_length == 0U || path_length >= sizeof(parsed->path)) {
            return false;
        }
        memcpy(parsed->path, cursor, path_length);
        parsed->path[path_length] = '\0';
    }
    return true;
}

static bool find_http_header_end(const uint8_t *data,
                                 size_t length,
                                 size_t *header_length)
{
    size_t i;
    for (i = 3U; i < length; ++i) {
        if (data[i - 3U] == '\r' && data[i - 2U] == '\n' &&
            data[i - 1U] == '\r' && data[i] == '\n') {
            *header_length = i + 1U;
            return true;
        }
    }
    return false;
}

static bool http_header_value(const uint8_t *headers,
                              size_t header_length,
                              const char *name,
                              char *output,
                              size_t output_capacity)
{
    const size_t name_length = strlen(name);
    size_t position = 0U;

    if (output_capacity == 0U) {
        return false;
    }
    output[0] = '\0';
    while (position < header_length) {
        size_t line_end = position;
        size_t value_start;
        size_t value_end;
        while (line_end + 1U < header_length &&
               !(headers[line_end] == '\r' && headers[line_end + 1U] == '\n')) {
            ++line_end;
        }
        if (line_end == position) {
            break;
        }
        if (line_end > position + name_length &&
            STRNCASECMP((const char *)headers + position, name, name_length) == 0 &&
            headers[position + name_length] == ':') {
            value_start = position + name_length + 1U;
            while (value_start < line_end &&
                   (headers[value_start] == ' ' || headers[value_start] == '\t')) {
                ++value_start;
            }
            value_end = line_end;
            while (value_end > value_start &&
                   (headers[value_end - 1U] == ' ' || headers[value_end - 1U] == '\t')) {
                --value_end;
            }
            if (value_end - value_start >= output_capacity) {
                return false;
            }
            memcpy(output, headers + value_start, value_end - value_start);
            output[value_end - value_start] = '\0';
            return true;
        }
        position = line_end + 2U;
    }
    return false;
}

static bool decode_chunked_body(const uint8_t *input,
                                size_t input_length,
                                uint8_t **output,
                                size_t *output_length)
{
    download_buffer_t decoded;
    size_t position = 0U;

    memset(&decoded, 0, sizeof(decoded));
    while (position < input_length) {
        size_t line_end = position;
        char size_text[32];
        char *end = NULL;
        unsigned long chunk_size;
        size_t text_length;

        while (line_end + 1U < input_length &&
               !(input[line_end] == '\r' && input[line_end + 1U] == '\n')) {
            ++line_end;
        }
        if (line_end + 1U >= input_length) {
            download_buffer_free(&decoded);
            return false;
        }
        text_length = line_end - position;
        if (text_length == 0U || text_length >= sizeof(size_text)) {
            download_buffer_free(&decoded);
            return false;
        }
        memcpy(size_text, input + position, text_length);
        size_text[text_length] = '\0';
        if (strchr(size_text, ';') != NULL) {
            *strchr(size_text, ';') = '\0';
        }
        errno = 0;
        chunk_size = strtoul(size_text, &end, 16);
        if (errno != 0 || end == size_text || *end != '\0' ||
            chunk_size > XML_FILE_LIMIT) {
            download_buffer_free(&decoded);
            return false;
        }
        position = line_end + 2U;
        if (chunk_size == 0U) {
            *output = decoded.data;
            *output_length = decoded.length;
            return true;
        }
        if ((size_t)chunk_size > input_length - position ||
            input_length - position - (size_t)chunk_size < 2U ||
            input[position + chunk_size] != '\r' ||
            input[position + chunk_size + 1U] != '\n' ||
            !download_buffer_append(&decoded, input + position,
                                    (size_t)chunk_size)) {
            download_buffer_free(&decoded);
            return false;
        }
        position += (size_t)chunk_size + 2U;
    }
    download_buffer_free(&decoded);
    return false;
}

static bool resolve_download_redirect(const char *current_url,
                                      const download_url_t *current,
                                      const char *location,
                                      char *output,
                                      size_t output_capacity)
{
    const char *scheme = current->https ? "https" : "http";
    bool default_port = current->port == (current->https ? 443U : 80U);
    char host[600];

    if (STRNCASECMP(location, "http://", 7) == 0 ||
        STRNCASECMP(location, "https://", 8) == 0) {
        return snprintf(output, output_capacity, "%s", location) >= 0 &&
               strlen(location) < output_capacity;
    }
    if (location[0] == '/' && location[1] == '/') {
        return snprintf(output, output_capacity, "%s:%s", scheme, location) >= 0 &&
               strlen(output) < output_capacity;
    }
    if (strchr(current->host, ':') != NULL) {
        snprintf(host, sizeof(host), "[%s]", current->host);
    } else {
        snprintf(host, sizeof(host), "%s", current->host);
    }
    if (location[0] == '/') {
        if (default_port) {
            return snprintf(output, output_capacity, "%s://%s%s",
                            scheme, host, location) >= 0 &&
                   strlen(output) < output_capacity;
        }
        return snprintf(output, output_capacity, "%s://%s:%u%s",
                        scheme, host, (unsigned)current->port, location) >= 0 &&
               strlen(output) < output_capacity;
    }
    (void)current_url;
    fprintf(stderr, "Relative EPG redirect paths are not supported: %s\n", location);
    return false;
}

static bool fetch_download_url_once(const char *url,
                                    int timeout_ms,
                                    uint8_t **body,
                                    size_t *body_length,
                                    char *redirect,
                                    size_t redirect_capacity)
{
    download_url_t parsed;
    download_buffer_t response;
    socket_t sock = SOCKET_INVALID;
    char request[4096];
    char host_header[600];
    uint8_t receive_buffer[32768];
    size_t header_length = 0U;
    int status_code = 0;
    bool ok = false;
#ifdef SORALINK_USE_OPENSSL
    SSL_CTX *ssl_context = NULL;
    SSL *ssl = NULL;
#endif

    *body = NULL;
    *body_length = 0U;
    if (redirect_capacity > 0U) {
        redirect[0] = '\0';
    }
    memset(&response, 0, sizeof(response));
    if (!parse_download_url(url, &parsed)) {
        return false;
    }
#ifndef SORALINK_USE_OPENSSL
    if (parsed.https) {
        fprintf(stderr,
                "HTTPS EPG updates require an OpenSSL build. Use make tls or an HTTP URL.\n");
        return false;
    }
#endif

    sock = connect_tcp(parsed.host, parsed.port, timeout_ms);
    if (sock == SOCKET_INVALID) {
        return false;
    }

#ifdef SORALINK_USE_OPENSSL
    if (parsed.https) {
        X509_VERIFY_PARAM *verify_param;
        OPENSSL_init_ssl(0, NULL);
        ssl_context = SSL_CTX_new(TLS_client_method());
        if (ssl_context == NULL ||
            SSL_CTX_set_default_verify_paths(ssl_context) != 1) {
            fprintf(stderr, "Could not initialize trusted CA certificates for EPG HTTPS.\n");
            goto cleanup;
        }
        SSL_CTX_set_min_proto_version(ssl_context, TLS1_2_VERSION);
        SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, NULL);
        ssl = SSL_new(ssl_context);
        if (ssl == NULL) {
            goto cleanup;
        }
        if (SSL_set_tlsext_host_name(ssl, parsed.host) != 1) {
            goto cleanup;
        }
        verify_param = SSL_get0_param(ssl);
        if (verify_param == NULL ||
            X509_VERIFY_PARAM_set1_host(verify_param, parsed.host, 0) != 1 ||
            SSL_set_fd(ssl, (int)sock) != 1 ||
            SSL_connect(ssl) != 1) {
            fprintf(stderr, "TLS connection to EPG host failed.\n");
            goto cleanup;
        }
    }
#endif

    if (strchr(parsed.host, ':') != NULL) {
        snprintf(host_header, sizeof(host_header), "[%s]", parsed.host);
    } else {
        snprintf(host_header, sizeof(host_header), "%s", parsed.host);
    }
    if (parsed.port != (parsed.https ? 443U : 80U)) {
        const size_t used = strlen(host_header);
        snprintf(host_header + used, sizeof(host_header) - used,
                 ":%u", (unsigned)parsed.port);
    }
    if (snprintf(request, sizeof(request),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: SORALink-EPG/3.1\r\n"
                 "Accept: application/xml,text/xml,*/*;q=0.5\r\n"
                 "Accept-Encoding: identity\r\n"
                 "Connection: close\r\n\r\n",
                 parsed.path, host_header) < 0 || strlen(request) >= sizeof(request) - 1U) {
        goto cleanup;
    }

#ifdef SORALINK_USE_OPENSSL
    if (parsed.https) {
        size_t sent = 0U;
        while (sent < strlen(request)) {
            const int result = SSL_write(ssl, request + sent,
                                         (int)(strlen(request) - sent));
            if (result <= 0) {
                fprintf(stderr, "TLS send failed while downloading EPG.\n");
                goto cleanup;
            }
            sent += (size_t)result;
        }
    } else
#endif
    if (!send_all(sock, (const uint8_t *)request, strlen(request))) {
        goto cleanup;
    }

    for (;;) {
        socket_io_t received;
#ifdef SORALINK_USE_OPENSSL
        if (parsed.https) {
            received = (socket_io_t)SSL_read(ssl, receive_buffer,
                                             (int)sizeof(receive_buffer));
            if (received <= 0) {
                const int ssl_error = SSL_get_error(ssl, (int)received);
                if (ssl_error == SSL_ERROR_ZERO_RETURN) {
                    break;
                }
                if (ssl_error == SSL_ERROR_WANT_READ ||
                    ssl_error == SSL_ERROR_WANT_WRITE) {
                    continue;
                }
                fprintf(stderr, "TLS receive failed while downloading EPG.\n");
                goto cleanup;
            }
        } else
#endif
        {
            received = socket_recv_bytes(sock, (char *)receive_buffer,
                                         (int)sizeof(receive_buffer), 0);
            if (received == 0) {
                break;
            }
            if (received < 0) {
                fprintf(stderr, "EPG download receive failed or timed out: %d\n",
                        SOCKET_ERROR_CODE());
                goto cleanup;
            }
        }
        if (!download_buffer_append(&response, receive_buffer,
                                    (size_t)received)) {
            goto cleanup;
        }
    }

    if (!find_http_header_end(response.data, response.length, &header_length) ||
        header_length > HTTP_DOWNLOAD_HEADER_LIMIT ||
        sscanf((const char *)response.data, "HTTP/%*u.%*u %d", &status_code) != 1) {
        fprintf(stderr, "Invalid HTTP response while downloading EPG.\n");
        goto cleanup;
    }

    if (status_code == 301 || status_code == 302 || status_code == 303 ||
        status_code == 307 || status_code == 308) {
        char location[2048];
        if (!http_header_value(response.data, header_length, "Location",
                               location, sizeof(location)) ||
            !resolve_download_redirect(url, &parsed, location,
                                       redirect, redirect_capacity)) {
            fprintf(stderr, "EPG download redirect is invalid.\n");
            goto cleanup;
        }
        ok = true;
        goto cleanup;
    }
    if (status_code < 200 || status_code >= 300) {
        fprintf(stderr, "EPG download returned HTTP status %d.\n", status_code);
        goto cleanup;
    }

    {
        const uint8_t *raw_body = response.data + header_length;
        const size_t raw_length = response.length - header_length;
        char transfer_encoding[128];
        char content_encoding[128];

        if (http_header_value(response.data, header_length, "Content-Encoding",
                              content_encoding, sizeof(content_encoding)) &&
            STRNCASECMP(content_encoding, "identity", 8) != 0) {
            fprintf(stderr,
                    "EPG server returned unsupported Content-Encoding '%s'. Configure an uncompressed XMLTV URL.\n",
                    content_encoding);
            goto cleanup;
        }
        if (http_header_value(response.data, header_length, "Transfer-Encoding",
                              transfer_encoding, sizeof(transfer_encoding)) &&
            STRNCASECMP(transfer_encoding, "chunked", 7) == 0) {
            if (!decode_chunked_body(raw_body, raw_length, body, body_length)) {
                fprintf(stderr, "Could not decode chunked EPG response.\n");
                goto cleanup;
            }
        } else {
            if (raw_length > XML_FILE_LIMIT) {
                goto cleanup;
            }
            *body = (uint8_t *)malloc(raw_length + 1U);
            if (*body == NULL) {
                goto cleanup;
            }
            memcpy(*body, raw_body, raw_length);
            (*body)[raw_length] = '\0';
            *body_length = raw_length;
        }
    }
    ok = true;

cleanup:
#ifdef SORALINK_USE_OPENSSL
    if (ssl != NULL) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ssl_context != NULL) {
        SSL_CTX_free(ssl_context);
    }
#endif
    if (sock != SOCKET_INVALID) {
        CLOSESOCKET(sock);
    }
    download_buffer_free(&response);
    if (!ok) {
        free(*body);
        *body = NULL;
        *body_length = 0U;
    }
    return ok;
}

static bool write_downloaded_epg(const char *path,
                                 const uint8_t *body,
                                 size_t body_length)
{
    char temporary[1200];
    FILE *file;
    size_t position = 0U;

    while (position < body_length && isspace((unsigned char)body[position])) {
        ++position;
    }
    if (body_length == 0U || position >= body_length || body[position] != '<') {
        fprintf(stderr, "Downloaded EPG is not XML text.\n");
        return false;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.download", path) < 0 ||
        strlen(temporary) >= sizeof(temporary) - 1U) {
        return false;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        fprintf(stderr, "Cannot create temporary EPG file '%s': %s\n",
                temporary, strerror(errno));
        return false;
    }
    if (fwrite(body, 1, body_length, file) != body_length ||
        fflush(file) != 0 || fclose(file) != 0) {
        fprintf(stderr, "Could not write complete downloaded EPG file.\n");
        remove(temporary);
        return false;
    }
    remove(path);
    if (rename(temporary, path) != 0) {
        fprintf(stderr, "Could not install downloaded EPG file '%s': %s\n",
                path, strerror(errno));
        remove(temporary);
        return false;
    }
    return true;
}

static bool update_epg_on_start(const options_t *opt)
{
    char current_url[4096];
    unsigned redirect_count;

    if (!opt->epg_update) {
        return true;
    }
    if (opt->epg_path == NULL || *opt->epg_path == '\0' ||
        opt->epg_url == NULL || *opt->epg_url == '\0') {
        return false;
    }
    if (snprintf(current_url, sizeof(current_url), "%s", opt->epg_url) < 0 ||
        strlen(opt->epg_url) >= sizeof(current_url)) {
        fprintf(stderr, "EPG URL is too long.\n");
        return false;
    }

    printf("Updating EPG before startup from %s ...\n", current_url);
    for (redirect_count = 0U; redirect_count <= HTTP_REDIRECT_LIMIT;
         ++redirect_count) {
        uint8_t *body = NULL;
        size_t body_length = 0U;
        char redirect[4096];
        bool fetched = fetch_download_url_once(current_url,
                                               opt->epg_update_timeout_ms,
                                               &body, &body_length,
                                               redirect, sizeof(redirect));
        if (!fetched) {
            free(body);
            fprintf(stderr,
                    "EPG update failed; the existing local EPG file will be used if available.\n");
            return false;
        }
        if (redirect[0] != '\0') {
            free(body);
            if (redirect_count == HTTP_REDIRECT_LIMIT) {
                fprintf(stderr, "EPG download exceeded the redirect limit.\n");
                return false;
            }
            snprintf(current_url, sizeof(current_url), "%s", redirect);
            continue;
        }
        if (!write_downloaded_epg(opt->epg_path, body, body_length)) {
            free(body);
            return false;
        }
        free(body);
        printf("EPG update complete: %zu bytes written to %s.\n",
               body_length, opt->epg_path);
        return true;
    }
    return false;
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

static bool tcp_command_variable(socket_t sock,
                                 const uint8_t frame[FRAME_SIZE],
                                 size_t minimum_data_length,
                                 size_t maximum_data_length,
                                 uint8_t *response,
                                 size_t response_capacity,
                                 size_t *actual_data_length,
                                 bool verbose)
{
    const size_t maximum_total = maximum_data_length + CSW_SIZE;
    size_t received = 0U;

    if (minimum_data_length > maximum_data_length ||
        response_capacity < maximum_total || actual_data_length == NULL) {
        fprintf(stderr, "Invalid variable TCP response parameters.\n");
        return false;
    }

    if (verbose) hex_dump("TCP request", frame, FRAME_SIZE);
    if (!send_all(sock, frame, FRAME_SIZE)) return false;

    while (received < maximum_total) {
        const size_t remaining = maximum_total - received;
        const int request_length = remaining > (size_t)INT_MAX
            ? INT_MAX : (int)remaining;
        const socket_io_t result = socket_recv_bytes(
            sock, (char *)response + received, request_length, 0);
        size_t candidate;

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

        for (candidate = minimum_data_length;
             candidate <= maximum_data_length; ++candidate) {
            const size_t total = candidate + CSW_SIZE;
            if (received < total) continue;
            if (memcmp(response + candidate, "USBS", 4) != 0) continue;
            if (!validate_csw(response, total, candidate, frame)) return false;
            if (verbose) hex_dump("TCP response", response, total);
            *actual_data_length = candidate;
            return true;
        }
    }

    fprintf(stderr, "Protocol error: no command-status trailer in response.\n");
    return false;
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

    if (control == SOCKET_INVALID) {
        fprintf(stderr, "TCP heartbeat skipped: control socket is not connected.\n");
        return false;
    }

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

static tune_result_t response_reports_lock(const uint8_t *response,
                                            size_t length,
                                            const char *label)
{
    if (length < 4 || response[0] != 0xF6 || response[1] != 0x03) {
        fprintf(stderr, "%s returned an unexpected response.\n", label);
        return TUNE_RESULT_CONTROL_ERROR;
    }

    printf("%s result: signal lock=%s, service=%s.\n",
           label,
           response[2] == 1 ? "YES" : "NO",
           response[3] == 1 ? "FTA" : "scrambled/unknown");
    return response[2] == 1 ? TUNE_RESULT_OK : TUNE_RESULT_NO_LOCK;
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
        if (response_reports_lock(response, 4, "Program-index tune") !=
            TUNE_RESULT_OK) {
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
        if (response_reports_lock(response, 4, "Service-ID tune") !=
            TUNE_RESULT_OK) {
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

    if (control == NULL) return false;
    if (*control != SOCKET_INVALID) {
        CLOSESOCKET(*control);
        *control = SOCKET_INVALID;
    }

    for (attempt = 1; attempt <= 3U && g_running; ++attempt) {
        socket_t candidate;

        fprintf(stderr, "Reconnecting device control session (attempt %u/3)...\n",
                attempt);
        candidate = connect_tcp(opt->device_ip, opt->control_port,
                                opt->timeout_ms);
        if (candidate != SOCKET_INVALID) {
            if (acquire_permission(candidate, opt->verbose)) {
                *control = candidate;
                fprintf(stderr, "Device control session restored.\n");
                return true;
            }
            CLOSESOCKET(candidate);
        }
        *control = SOCKET_INVALID;
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
    if (!xml_attribute(tag, "epg_id", channel->epg_id,
                       sizeof(channel->epg_id)) &&
        !xml_attribute(tag, "xmltv_id", channel->epg_id,
                       sizeof(channel->epg_id))) {
        snprintf(channel->epg_id, sizeof(channel->epg_id), "%u",
                 (unsigned)channel->lcn);
    }
    xml_decode(channel->epg_id);
    if (xml_attribute(tag, "prog_idx", text, sizeof(text)) &&
        parse_u32(text, &channel->program_index)) {
        channel->have_program_index = true;
    }
    if (xml_attribute(tag, "ts_id", text, sizeof(text))) {
        (void)parse_u32(text, &channel->transport_stream_id);
    }
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

static void free_epg_list(epg_list_t *list)
{
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int xmltv_digit(const char *text, size_t index)
{
    const unsigned char ch = (unsigned char)text[index];
    return isdigit(ch) ? (int)(ch - '0') : -1;
}

static bool xmltv_number(const char *text, size_t offset, size_t count,
                         int *value)
{
    size_t i;
    int result = 0;
    for (i = 0; i < count; ++i) {
        const int digit = xmltv_digit(text, offset + i);
        if (digit < 0) {
            return false;
        }
        result = result * 10 + digit;
    }
    *value = result;
    return true;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    const int adjusted_year = year - (month <= 2U ? 1 : 0);
    const int era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
    const unsigned year_of_era = (unsigned)(adjusted_year - era * 400);
    const int shifted_month = (int)month + (month > 2U ? -3 : 9);
    const unsigned day_of_year =
        (153U * (unsigned)shifted_month + 2U) / 5U + day - 1U;
    const unsigned day_of_era = year_of_era * 365U + year_of_era / 4U -
                                year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

static bool parse_xmltv_time(const char *text, int64_t *utc_seconds)
{
    int year, month, day, hour, minute, second = 0;
    size_t length;
    size_t position;
    int offset_seconds = 0;

    if (text == NULL) {
        return false;
    }
    length = strlen(text);
    if (length < 12U ||
        !xmltv_number(text, 0U, 4U, &year) ||
        !xmltv_number(text, 4U, 2U, &month) ||
        !xmltv_number(text, 6U, 2U, &day) ||
        !xmltv_number(text, 8U, 2U, &hour) ||
        !xmltv_number(text, 10U, 2U, &minute)) {
        return false;
    }
    position = 12U;
    if (length >= 14U && isdigit((unsigned char)text[12]) &&
        isdigit((unsigned char)text[13])) {
        if (!xmltv_number(text, 12U, 2U, &second)) {
            return false;
        }
        position = 14U;
    }
    while (position < length && isspace((unsigned char)text[position])) {
        ++position;
    }
    if (position + 5U <= length &&
        (text[position] == '+' || text[position] == '-')) {
        int offset_hour;
        int offset_minute;
        if (!xmltv_number(text, position + 1U, 2U, &offset_hour) ||
            !xmltv_number(text, position + 3U, 2U, &offset_minute) ||
            offset_hour > 23 || offset_minute > 59) {
            return false;
        }
        offset_seconds = (offset_hour * 60 + offset_minute) * 60;
        if (text[position] == '-') {
            offset_seconds = -offset_seconds;
        }
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60) {
        return false;
    }
    *utc_seconds = days_from_civil(year, (unsigned)month, (unsigned)day) * 86400LL +
                   (int64_t)hour * 3600LL + (int64_t)minute * 60LL + second -
                   offset_seconds;
    return true;
}

static void trim_in_place(char *text)
{
    char *start = text;
    char *end;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
}

static bool xml_element_text(const char *block,
                             const char *element,
                             char *output,
                             size_t output_capacity)
{
    char opening[80];
    char closing[96];
    const char *start;
    const char *content;
    const char *end;
    size_t used = 0U;
    bool in_tag = false;
    int length;

    if (output_capacity == 0U) {
        return false;
    }
    output[0] = '\0';
    length = snprintf(opening, sizeof(opening), "<%s", element);
    if (length <= 0 || (size_t)length >= sizeof(opening)) {
        return false;
    }
    length = snprintf(closing, sizeof(closing), "</%s>", element);
    if (length <= 0 || (size_t)length >= sizeof(closing)) {
        return false;
    }
    start = strstr(block, opening);
    if (start == NULL) {
        return false;
    }
    content = strchr(start, '>');
    if (content == NULL) {
        return false;
    }
    ++content;
    end = strstr(content, closing);
    if (end == NULL) {
        return false;
    }
    while (content < end && used + 1U < output_capacity) {
        if (!in_tag && (size_t)(end - content) >= 9U &&
            memcmp(content, "<![CDATA[", 9U) == 0) {
            const char *cdata_end = strstr(content + 9U, "]]>");
            const char *copy_end = cdata_end != NULL && cdata_end < end ?
                                   cdata_end : end;
            content += 9U;
            while (content < copy_end && used + 1U < output_capacity) {
                output[used++] = *content++;
            }
            content = cdata_end != NULL && cdata_end < end ? cdata_end + 3U : end;
            continue;
        }
        if (*content == '<') {
            in_tag = true;
        } else if (*content == '>') {
            in_tag = false;
            if (used > 0U && !isspace((unsigned char)output[used - 1U]) &&
                used + 1U < output_capacity) {
                output[used++] = ' ';
            }
        } else if (!in_tag) {
            output[used++] = *content;
        }
        ++content;
    }
    output[used] = '\0';
    xml_decode(output);
    trim_in_place(output);
    return output[0] != '\0';
}

static bool append_epg_program(epg_list_t *list,
                               size_t *capacity,
                               const epg_program_t *program)
{
    if (list->count == *capacity) {
        const size_t new_capacity = *capacity == 0U ? 256U : *capacity * 2U;
        epg_program_t *items;
        if (new_capacity < *capacity ||
            new_capacity > SIZE_MAX / sizeof(*items)) {
            return false;
        }
        items = (epg_program_t *)realloc(list->items,
                                         new_capacity * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        list->items = items;
        *capacity = new_capacity;
    }
    list->items[list->count++] = *program;
    return true;
}

static int compare_epg_programs(const void *left, const void *right)
{
    const epg_program_t *a = (const epg_program_t *)left;
    const epg_program_t *b = (const epg_program_t *)right;
    const int channel_compare = strcmp(a->channel_id, b->channel_id);
    if (channel_compare != 0) {
        return channel_compare;
    }
    if (a->start_utc < b->start_utc) {
        return -1;
    }
    if (a->start_utc > b->start_utc) {
        return 1;
    }
    return 0;
}

static bool load_epg_file(const char *path, epg_list_t *list)
{
    FILE *file;
    long file_length;
    char *document;
    char *cursor;
    size_t capacity = 0U;
    size_t malformed = 0U;

    memset(list, 0, sizeof(*list));
    if (path == NULL || *path == '\0') {
        return true;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "EPG file '%s' is not available: %s\n",
                path, strerror(errno));
        return true;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_length = ftell(file)) < 0 ||
        (unsigned long)file_length > XML_FILE_LIMIT ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "EPG XML is unreadable or exceeds %u MiB.\n",
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
        fprintf(stderr, "Could not read complete EPG XML.\n");
        fclose(file);
        free(document);
        return false;
    }
    fclose(file);
    document[file_length] = '\0';

    cursor = document;
    while ((cursor = strstr(cursor, "<programme")) != NULL) {
        char *tag_end = find_xml_tag_end(cursor);
        char *closing;
        epg_program_t program;
        char start_text[64];
        char stop_text[64];
        char *tag;
        size_t tag_length;
        size_t block_length;
        char *block;

        if (tag_end == NULL) {
            ++malformed;
            break;
        }
        closing = strstr(tag_end + 1, "</programme>");
        if (closing == NULL) {
            ++malformed;
            break;
        }
        tag_length = (size_t)(tag_end - cursor + 1);
        block_length = (size_t)(closing - cursor + strlen("</programme>"));
        if (tag_length > 65536U || block_length > 1024U * 1024U) {
            ++malformed;
            cursor = closing + strlen("</programme>");
            continue;
        }
        tag = (char *)malloc(tag_length + 1U);
        block = (char *)malloc(block_length + 1U);
        if (tag == NULL || block == NULL) {
            free(tag);
            free(block);
            free(document);
            free_epg_list(list);
            return false;
        }
        memcpy(tag, cursor, tag_length);
        tag[tag_length] = '\0';
        memcpy(block, cursor, block_length);
        block[block_length] = '\0';
        memset(&program, 0, sizeof(program));

        if (!xml_attribute(tag, "channel", program.channel_id,
                           sizeof(program.channel_id)) ||
            !xml_attribute(tag, "start", start_text, sizeof(start_text)) ||
            !xml_attribute(tag, "stop", stop_text, sizeof(stop_text)) ||
            !parse_xmltv_time(start_text, &program.start_utc) ||
            !parse_xmltv_time(stop_text, &program.stop_utc) ||
            program.stop_utc <= program.start_utc ||
            !xml_element_text(block, "title", program.title,
                              sizeof(program.title))) {
            ++malformed;
        } else {
            xml_decode(program.channel_id);
            (void)xml_element_text(block, "sub-title", program.subtitle,
                                   sizeof(program.subtitle));
            (void)xml_element_text(block, "desc", program.description,
                                   sizeof(program.description));
            (void)xml_element_text(block, "category", program.category,
                                   sizeof(program.category));
            if (!append_epg_program(list, &capacity, &program)) {
                free(tag);
                free(block);
                free(document);
                free_epg_list(list);
                return false;
            }
        }
        free(tag);
        free(block);
        cursor = closing + strlen("</programme>");
    }
    free(document);
    if (list->count > 1U) {
        qsort(list->items, list->count, sizeof(list->items[0]),
              compare_epg_programs);
    }
    list->loaded_utc = (int64_t)time(NULL);
    printf("Loaded %zu EPG programmes from %s.\n", list->count, path);
    if (malformed > 0U) {
        fprintf(stderr, "Warning: skipped %zu malformed EPG programme(s).\n",
                malformed);
    }
    return true;
}


static uint16_t read_u16_be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t read_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static void build_device_channel_length_request(uint8_t frame[FRAME_SIZE])
{
    build_frame(frame, 8, 0xF6, 0x20);
}

static void build_device_channel_data_request(uint8_t frame[FRAME_SIZE],
                                              uint32_t length)
{
    build_frame(frame, length, 0xF6, 0x21);
}

static void build_device_epg_length_request(uint8_t frame[FRAME_SIZE],
                                            uint32_t program_index,
                                            unsigned response_bytes)
{
    build_frame(frame, response_bytes, 0xF6, 0x30);
    put_u32_be(&frame[25], program_index);
}

static void build_device_epg_data_request(uint8_t frame[FRAME_SIZE],
                                          uint32_t program_index,
                                          uint32_t length)
{
    build_frame(frame, length, 0xF6, 0x31);
    put_u32_be(&frame[25], program_index);
}

#define DEVICE_SCAN_AUTOMATIC_ACTION 0U
#define DEVICE_SCAN_UI_FULL_MODE     110U
#define DEVICE_SCAN_STOP_ACTION      3U
#define DEVICE_SCAN_POLL_SECONDS 2.0
#define DEVICE_SCAN_COMMIT_WAIT_MS 5000U
#define DEVICE_SCAN_TP_COMMIT_WAIT_MS 1200U
#define DEVICE_SCAN_TP_TUNE_WAIT_MS 500U
#define DEVICE_SCAN_SATELLITE_APPLY_WAIT_MS 600U
#define DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS 6U
#define DEVICE_CHANNEL_RETRY_BASE_MS 1500U

typedef struct {
    uint16_t frequency_mhz;
    uint16_t symbol_rate_ks;
    char polarization;
} satellite_transponder_t;

static const satellite_transponder_t astra_19e_transponders[] = {
    {10729,23500,'V'}, {10758,22000,'V'}, {10773,22000,'H'},
    {10788,22000,'V'}, {10803,22000,'H'}, {10818,22000,'V'},
    {10832,22000,'H'}, {10847,22000,'V'}, {10876,22000,'V'},
    {10891,22000,'H'}, {10906,22000,'V'}, {10921,22000,'H'},
    {10935,22000,'V'}, {10964,22000,'H'}, {10979,22000,'V'},
    {10995,22000,'H'}, {11009,23500,'V'}, {11038,22000,'V'},
    {11053,22000,'H'}, {11068,22000,'V'}, {11082,22000,'H'},
    {11097,22000,'V'}, {11112,22000,'H'}, {11126,22000,'V'},
    {11156,22000,'V'}, {11186,22000,'V'}, {11229,22000,'V'},
    {11244,22000,'H'}, {11259,22000,'V'}, {11273,22000,'H'},
    {11288,22000,'V'}, {11302,22000,'H'}, {11318,22000,'V'},
    {11347,22000,'V'}, {11362,22000,'H'}, {11376,22000,'V'},
    {11391,22000,'H'}, {11421,22000,'H'}, {11464,22000,'H'},
    {11494,22000,'H'}, {11523,22000,'H'}, {11538,22000,'V'},
    {11553,22000,'H'}, {11582,22000,'H'}, {11671,22000,'H'},
    {11694,  940,'V'}, {11739,27500,'V'}, {11758,27500,'H'},
    {11778,29500,'V'}, {11798,27500,'H'}, {11836,27500,'H'},
    {11856,29700,'V'}, {11895,29700,'V'}, {11914,27500,'H'},
    {11934,29700,'V'}, {11954,27500,'H'}, {11973,27500,'V'},
    {11992,27500,'H'}, {12012,29700,'V'}, {12032,27500,'H'},
    {12051,27500,'V'}, {12090,29700,'V'}, {12110,27500,'H'},
    {12148,27500,'H'}, {12168,29700,'V'}, {12188,27500,'H'},
    {12207,29700,'V'}, {12226,27500,'H'}, {12285,29700,'V'},
    {12304,27500,'H'}, {12324,29700,'V'}, {12344,27500,'H'},
    {12363,27500,'V'}, {12382,27500,'H'}, {12402,29700,'V'},
    {12422,27500,'H'}, {12460,27500,'H'}, {12480,27500,'V'},
    {12515,22000,'H'}, {12545,22000,'H'}, {12552,22000,'V'},
    {12574,22000,'H'}, {12604,22000,'H'}, {12610,22000,'V'},
    {12633,22000,'H'}, {12640,23500,'V'}, {12663,22000,'H'},
    {12669,23500,'V'}
};

static bool device_start_sweep_transponder(socket_t *control,
                                            const options_t *opt,
                                            device_update_state_t *state);

#if DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS < 2U
#error "Channel-list handoff requires retries"
#endif
#if DEVICE_SCAN_COMMIT_WAIT_MS < 1000U
#error "Post-scan receiver commit wait is too short"
#endif

static uint8_t device_scan_flags(const options_t *opt)
{
    uint8_t flags = opt->device_scan_network ? 0x01U : 0x00U;
    flags |= (uint8_t)((opt->device_scan_order_by & 0x02U) << 1U);
    flags |= (uint8_t)((opt->device_scan_search_range & 0x07U) << 2U);
    flags |= (uint8_t)((opt->device_scan_sort_mode & 0x03U) << 6U);
    return flags;
}

static void build_device_scan_action_request(uint8_t frame[FRAME_SIZE],
                                             const options_t *opt,
                                             uint8_t action)
{
    build_frame(frame, 2U, 0xF6, 0x10);
    frame[21] = action;
    frame[22] = device_scan_flags(opt);
}

static void build_device_scan_status_request(uint8_t frame[FRAME_SIZE])
{
    build_frame(frame, 17U, 0xF6, 0x02);
}

static bool device_send_scan_action(socket_t *control,
                                    const options_t *opt,
                                    uint8_t action)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t response[2U + CSW_SIZE];
    unsigned attempt;

    for (attempt = 0U; attempt < 2U; ++attempt) {
        if (*control == SOCKET_INVALID) {
            if (!reconnect_control_session(control, opt)) return false;
        }
        build_device_scan_action_request(frame, opt, action);
        if (tcp_command(*control, frame, 2U, response, sizeof(response),
                        opt->verbose) &&
            response[0] == 0xF6 && response[1] == 0x10) {
            return true;
        }

        if (attempt == 0U) {
            fprintf(stderr,
                    "Device scan-start acknowledgement was not received; "
                    "reconnecting and retrying automatic scan.\n");
            if (*control != SOCKET_INVALID) {
                CLOSESOCKET(*control);
                *control = SOCKET_INVALID;
            }
            SLEEP_MS(750U);
        }
    }
    return false;
}

static bool device_prepare_scan_satellite(socket_t *control,
                                          const options_t *opt)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t response[2U + CSW_SIZE];

    if (!opt->device_scan_apply_satellite) {
        printf("Receiver scan will use the satellite/LNB profile already stored "
               "in the device.\n");
        return *control != SOCKET_INVALID;
    }
    if (*control == SOCKET_INVALID) return false;

    printf("Applying scan satellite profile: %u.%u%c, LNB %u/%u, "
           "switch %u MHz, DiSEqC %u, tone %s.\n",
           (unsigned)(opt->orbital_tenths / 10U),
           (unsigned)(opt->orbital_tenths % 10U),
           opt->west ? 'W' : 'E',
           (unsigned)opt->lnb_low_mhz,
           (unsigned)opt->lnb_high_mhz,
           (unsigned)opt->lnb_switch_mhz,
           (unsigned)opt->diseqc_port,
           opt->tone == TONE_ON ? "on" :
           (opt->tone == TONE_OFF ? "off" : "auto"));

    build_lnb_config(frame, opt);
    if (!tcp_command(*control, frame, 2U, response, sizeof(response),
                     opt->verbose) ||
        response[0] != 0xF6 || response[1] != 0x07) {
        fprintf(stderr, "Receiver rejected the scan LNB profile.\n");
        return false;
    }
    SLEEP_MS(80U);

    build_diseqc_config(frame, opt);
    if (!tcp_command(*control, frame, 2U, response, sizeof(response),
                     opt->verbose) ||
        response[0] != 0xF6 || response[1] != 0x08) {
        fprintf(stderr, "Receiver rejected the scan DiSEqC profile.\n");
        return false;
    }
    SLEEP_MS(80U);

    build_scan_satellite_config(frame, opt);
    if (opt->verbose) hex_dump("TCP scan satellite profile", frame, FRAME_SIZE);
    if (!send_all(*control, frame, FRAME_SIZE)) {
        return false;
    }
    SLEEP_MS(DEVICE_SCAN_SATELLITE_APPLY_WAIT_MS);
    CLOSESOCKET(*control);
    *control = SOCKET_INVALID;
    return reconnect_control_session(control, opt);
}

static bool device_start_sweep_transponder(socket_t *control,
                                            const options_t *opt,
                                            device_update_state_t *state)
{
    const satellite_transponder_t *tp;
    options_t tune_opt;
    uint8_t frame[FRAME_SIZE];
    uint8_t response[2U + CSW_SIZE];
    unsigned attempt;

    if (!state->scan_transponder_sweep ||
        state->scan_sweep_index >= state->scan_sweep_total) {
        return false;
    }
    tp = &astra_19e_transponders[state->scan_sweep_index];
    tune_opt = *opt;
    tune_opt.frequency_mhz = tp->frequency_mhz;
    tune_opt.symbol_rate_ks = tp->symbol_rate_ks;
    tune_opt.polarization = tp->polarization;
    tune_opt.tune_mode = TUNE_DVBS;

    for (attempt = 0U; attempt < 2U; ++attempt) {
        if (*control == SOCKET_INVALID &&
            !reconnect_control_session(control, opt)) {
            continue;
        }

        build_dvbs_tune(frame, &tune_opt);
        if (!tcp_command(*control, frame, 2U, response, sizeof(response),
                         opt->verbose) ||
            response[0] != 0xF6 || response[1] != 0x01) {
            if (*control != SOCKET_INVALID) {
                CLOSESOCKET(*control);
                *control = SOCKET_INVALID;
            }
            continue;
        }
        SLEEP_MS(DEVICE_SCAN_TP_TUNE_WAIT_MS);

        build_device_scan_action_request(frame, opt,
                                         DEVICE_SCAN_AUTOMATIC_ACTION);
        if (tcp_command(*control, frame, 2U, response, sizeof(response),
                        opt->verbose) &&
            response[0] == 0xF6 && response[1] == 0x10) {
            state->scan_frequency_mhz = tp->frequency_mhz;
            state->scan_symbol_rate_ks = tp->symbol_rate_ks;
            state->scan_state = 1U;
            state->scan_next_poll = monotonic_seconds() + 0.5;
            snprintf(state->last_message, sizeof(state->last_message),
                     "Astra sweep %zu/%zu: %u MHz %c %u kSym/s",
                     state->scan_sweep_index + 1U,
                     state->scan_sweep_total,
                     (unsigned)tp->frequency_mhz,
                     tp->polarization,
                     (unsigned)tp->symbol_rate_ks);
            printf("Astra sweep %zu/%zu: tuning %u MHz, %c, %u kSym/s.\n",
                   state->scan_sweep_index + 1U,
                   state->scan_sweep_total,
                   (unsigned)tp->frequency_mhz,
                   tp->polarization,
                   (unsigned)tp->symbol_rate_ks);
            return true;
        }

        if (*control != SOCKET_INVALID) {
            CLOSESOCKET(*control);
            *control = SOCKET_INVALID;
        }
        SLEEP_MS(600U);
    }
    return false;
}

static void device_scan_mark_stopped(device_update_state_t *state,
                                     const char *message)
{
    state->scan_running = false;
    state->scan_cancel_requested = false;
    state->busy = false;
    state->scan_transponder_sweep = false;
    state->scan_sweep_index = 0U;
    state->scan_sweep_total = 0U;
    free_channel_list(&state->scan_sweep_channels);
    snprintf(state->last_action, sizeof(state->last_action), "scan");
    snprintf(state->last_message, sizeof(state->last_message), "%s", message);
}

static bool device_start_scan(socket_t *control,
                              const options_t *opt,
                              device_update_state_t *state)
{
    const double now = monotonic_seconds();
    if (state->busy || state->scan_running) return false;
    state->busy = true;
    state->scan_running = true;
    state->scan_cancel_requested = false;
    state->scan_progress = 0U;
    state->scan_state = 1U;
    state->scan_frequency_mhz = 0U;
    state->scan_symbol_rate_ks = 0U;
    state->scan_mode = (unsigned)opt->device_scan_mode;
    state->scan_tv_count = 0U;
    state->scan_radio_count = 0U;
    state->last_scan_attempt_utc = (int64_t)time(NULL);
    state->scan_started = now;
    state->scan_next_poll = now + 0.5;
    state->scan_deadline = now + (double)opt->device_scan_timeout_minutes * 60.0;
    snprintf(state->last_action, sizeof(state->last_action), "scan");
    snprintf(state->last_message, sizeof(state->last_message),
             "Applying satellite profile for receiver scan");

    if (!device_prepare_scan_satellite(control, opt)) {
        device_scan_mark_stopped(state,
                                 "Could not apply receiver scan satellite profile");
        return false;
    }

    if (opt->device_scan_mode == DEVICE_SCAN_UI_FULL_MODE &&
        opt->orbital_tenths == 192U && !opt->west) {
        state->scan_transponder_sweep = true;
        state->scan_sweep_index = 0U;
        state->scan_sweep_total =
            sizeof(astra_19e_transponders) /
            sizeof(astra_19e_transponders[0]);
        free_channel_list(&state->scan_sweep_channels);
        printf("Starting Astra 19.2E software transponder sweep (%zu frequencies).\n",
               state->scan_sweep_total);
        if (!device_start_sweep_transponder(control, opt, state)) {
            device_scan_mark_stopped(state,
                                     "Could not start Astra transponder sweep");
            return false;
        }
        return true;
    }

    snprintf(state->last_message, sizeof(state->last_message),
             "Starting receiver channel scan");
    if (!device_send_scan_action(control, opt, DEVICE_SCAN_AUTOMATIC_ACTION)) {
        device_scan_mark_stopped(state, "Receiver rejected channel-scan start");
        return false;
    }
    printf("Receiver channel scan started (wire mode=0 automatic, scope=Device, "
           "network=%s, range=%u, order=%u, sort=%u).\n",
           opt->device_scan_network ? "yes" : "no",
           (unsigned)opt->device_scan_search_range,
           (unsigned)opt->device_scan_order_by,
           (unsigned)opt->device_scan_sort_mode);
    return true;
}

static bool device_cancel_scan(socket_t *control,
                               const options_t *opt,
                               device_update_state_t *state)
{
    bool sent;
    if (!state->scan_running) return false;
    sent = device_send_scan_action(control, opt, DEVICE_SCAN_STOP_ACTION);
    device_scan_mark_stopped(state,
                             sent ? "Receiver channel scan cancelled" :
                                    "Scan cancelled locally; receiver did not acknowledge stop");
    printf("Receiver channel scan cancelled.\n");
    return sent;
}



static bool atomic_write_bytes(const char *path,
                               const uint8_t *data,
                               size_t length)
{
    char temporary[1200];
    FILE *file;
    if (path == NULL || *path == '\0' ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 ||
        strlen(path) + 4U >= sizeof(temporary)) {
        return false;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        fprintf(stderr, "Cannot create temporary file '%s': %s\n",
                temporary, strerror(errno));
        return false;
    }
    if (fwrite(data, 1, length, file) != length || fflush(file) != 0 ||
        fclose(file) != 0) {
        fprintf(stderr, "Could not write complete temporary file '%s'.\n",
                temporary);
        remove(temporary);
        return false;
    }
#ifdef _WIN32
    remove(path);
#endif
    if (rename(temporary, path) != 0) {
        fprintf(stderr, "Could not install '%s': %s\n", path, strerror(errno));
        remove(temporary);
        return false;
    }
    return true;
}

static bool device_download_channel_xml(socket_t control,
                                        const options_t *opt,
                                        uint8_t **xml,
                                        size_t *xml_length)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t length_response[8 + CSW_SIZE];
    uint8_t *response = NULL;
    uint32_t transfer_length;
    size_t body_length;

    *xml = NULL;
    *xml_length = 0U;
    build_device_channel_length_request(frame);
    if (!tcp_command(control, frame, 8, length_response,
                     sizeof(length_response), opt->verbose) ||
        length_response[0] != 0xF6 || length_response[1] != 0x20) {
        fprintf(stderr, "Native channel-list length request failed.\n");
        return false;
    }
    transfer_length = read_u32_be(&length_response[2]);
    if (transfer_length < 32U || transfer_length > XML_FILE_LIMIT) {
        fprintf(stderr, "Device reported invalid channel-list size %u.\n",
                (unsigned)transfer_length);
        return false;
    }
    response = (uint8_t *)malloc((size_t)transfer_length + CSW_SIZE);
    if (response == NULL) {
        return false;
    }
    build_device_channel_data_request(frame, transfer_length);
    if (!tcp_command(control, frame, transfer_length, response,
                     (size_t)transfer_length + CSW_SIZE, opt->verbose) ||
        response[0] != 0xF6 || response[1] != 0x21) {
        fprintf(stderr, "Native channel-list download failed.\n");
        free(response);
        return false;
    }
    body_length = (size_t)transfer_length - 2U;
    while (body_length > 0U &&
           (response[2U + body_length - 1U] == 0U ||
            response[2U + body_length - 1U] == 0xFFU)) {
        --body_length;
    }
    if (body_length == 0U || response[2] != '<') {
        fprintf(stderr, "Native channel-list response is not XML.\n");
        free(response);
        return false;
    }
    *xml = (uint8_t *)malloc(body_length);
    if (*xml == NULL) {
        free(response);
        return false;
    }
    memcpy(*xml, response + 2, body_length);
    *xml_length = body_length;
    free(response);
    return true;
}

static void preserve_channel_epg_ids(channel_list_t *replacement,
                                     const channel_list_t *current)
{
    size_t i, j;
    if (current == NULL) return;
    for (i = 0; i < replacement->count; ++i) {
        channel_t *next = &replacement->items[i];
        char default_id[32];
        snprintf(default_id, sizeof(default_id), "%u", (unsigned)next->lcn);
        if (strcmp(next->epg_id, default_id) != 0) continue;
        for (j = 0; j < current->count; ++j) {
            const channel_t *old = &current->items[j];
            if ((old->service_id == next->service_id &&
                 old->frequency_mhz == next->frequency_mhz &&
                 old->symbol_rate_ks == next->symbol_rate_ks &&
                 old->polarization == next->polarization) ||
                old->lcn == next->lcn) {
                snprintf(next->epg_id, sizeof(next->epg_id), "%s", old->epg_id);
                break;
            }
        }
    }
}

static bool device_refresh_channels(socket_t *control,
                                    const options_t *opt,
                                    channel_list_t *channels,
                                    device_update_state_t *state)
{
    uint8_t *xml = NULL;
    size_t xml_length = 0U;
    channel_list_t replacement;
    char candidate_path[1200];
    bool candidate_written = false;
    bool downloaded = false;
    unsigned download_attempt;

    memset(&replacement, 0, sizeof(replacement));
    state->busy = true;
    state->last_channels_attempt_utc = (int64_t)time(NULL);
    snprintf(state->last_action, sizeof(state->last_action), "channels");
    snprintf(state->last_message, sizeof(state->last_message),
             "Downloading channel list from device");

    if (opt->channels_path == NULL ||
        snprintf(candidate_path, sizeof(candidate_path), "%s.device-new",
                 opt->channels_path) < 0 ||
        strlen(opt->channels_path) + strlen(".device-new") >= sizeof(candidate_path)) {
        snprintf(state->last_message, sizeof(state->last_message),
                 "Channel-list path is too long");
        state->busy = false;
        return false;
    }

    (void)remove(candidate_path);

    for (download_attempt = 1U;
         download_attempt <= DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS && g_running;
         ++download_attempt) {
        free(xml);
        xml = NULL;
        xml_length = 0U;

        if (*control == SOCKET_INVALID &&
            !reconnect_control_session(control, opt)) {
            fprintf(stderr,
                    "Channel-list retry %u/%u could not reconnect.\n",
                    download_attempt, DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS);
        } else if (device_download_channel_xml(*control, opt, &xml,
                                               &xml_length)) {
            downloaded = true;
            break;
        }

        if (download_attempt < DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS) {
            const unsigned delay_ms = DEVICE_CHANNEL_RETRY_BASE_MS *
                                      download_attempt;
            fprintf(stderr,
                    "Receiver channel database is not ready "
                    "(attempt %u/%u); waiting %u ms and reconnecting.\n",
                    download_attempt, DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS,
                    delay_ms);
            SLEEP_MS(delay_ms);
            (void)reconnect_control_session(control, opt);
        }
    }

    if (!downloaded ||
        !atomic_write_bytes(candidate_path, xml, xml_length)) {
        free(xml);
        (void)remove(candidate_path);
        if (*control == SOCKET_INVALID &&
            !reconnect_control_session(control, opt)) {
            fprintf(stderr,
                    "Device control recovery after channel update failed.\n");
        }
        snprintf(state->last_message, sizeof(state->last_message),
                 "Device channel update failed after %u attempts",
                 DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS);
        state->busy = false;
        return false;
    }
    candidate_written = true;

    if (!load_channel_list(candidate_path, &replacement) ||
        replacement.count == 0U) {
        free_channel_list(&replacement);
        remove(candidate_path);
        free(xml);
        snprintf(state->last_message, sizeof(state->last_message),
                 "Downloaded channel XML could not be parsed");
        state->busy = false;
        return false;
    }

    if (channels != NULL && channels->count >= 20U &&
        replacement.count < (channels->count + 1U) / 2U) {
        fprintf(stderr,
                "Warning: receiver returned only %zu channels; preserving "
                "the existing %zu-channel lineup.\n",
                replacement.count, channels->count);
        snprintf(state->last_message, sizeof(state->last_message),
                 "Receiver lineup rejected as incomplete: %zu received, %zu kept",
                 replacement.count, channels->count);
        free_channel_list(&replacement);
        (void)remove(candidate_path);
        free(xml);
        state->busy = false;
        return false;
    }

    preserve_channel_epg_ids(&replacement, channels);
    if (!atomic_write_bytes(opt->channels_path, xml, xml_length)) {
        free_channel_list(&replacement);
        if (candidate_written) remove(candidate_path);
        free(xml);
        snprintf(state->last_message, sizeof(state->last_message),
                 "Validated channel XML could not be installed");
        state->busy = false;
        return false;
    }

    if (candidate_written) remove(candidate_path);
    free(xml);
    free_channel_list(channels);
    *channels = replacement;
    state->epg_length_response_bytes = 0U;
    state->last_channels_success_utc = (int64_t)time(NULL);
    snprintf(state->last_message, sizeof(state->last_message),
             "Loaded %zu channels from device", channels->count);
    state->busy = false;
    return true;
}

typedef enum {
    DEVICE_EPG_FETCH_OK = 0,
    DEVICE_EPG_FETCH_EMPTY,
    DEVICE_EPG_FETCH_CONTROL_ERROR
} device_epg_fetch_result_t;

#define DEVICE_EPG_CHANNELS_PER_SESSION 40U
#define DEVICE_EPG_FETCH_RETRIES 1U

static device_epg_fetch_result_t device_download_epg_xml(
    socket_t *control,
    const options_t *opt,
    device_update_state_t *state,
    uint32_t program_index,
    uint8_t **xml,
    size_t *xml_length)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t length_response[6 + CSW_SIZE];
    uint8_t *response = NULL;
    uint32_t transfer_length = 0U;
    size_t response_data_length = 0U;
    size_t body_length;
    unsigned candidates[2];
    size_t candidate_count = 0U;
    size_t attempt;

    *xml = NULL;
    *xml_length = 0U;

    if (state->epg_length_response_bytes == 4U ||
        state->epg_length_response_bytes == 6U) {
        candidates[candidate_count++] = state->epg_length_response_bytes;
    } else {
        candidates[candidate_count++] = 4U;
        candidates[candidate_count++] = 6U;
    }

    for (attempt = 0U; attempt < candidate_count; ++attempt) {
        const unsigned requested = candidates[attempt];
        memset(length_response, 0, sizeof(length_response));
        build_device_epg_length_request(frame, program_index, requested);

        if (tcp_command_variable(*control, frame, 4U, 6U,
                                 length_response, sizeof(length_response),
                                 &response_data_length, opt->verbose) &&
            length_response[0] == 0xF6 && length_response[1] == 0x30) {
            if (response_data_length == 4U) {
                transfer_length = read_u16_be(&length_response[2]);
            } else if (response_data_length == 6U) {
                transfer_length = read_u32_be(&length_response[2]);
            } else {
                return DEVICE_EPG_FETCH_CONTROL_ERROR;
            }
            state->epg_length_response_bytes =
                (unsigned)response_data_length;
            break;
        }

        if (attempt + 1U < candidate_count) {
            fprintf(stderr,
                    "EPG length mode %u failed; reconnecting and trying %u-byte mode.\n",
                    requested, candidates[attempt + 1U]);
            if (!reconnect_control_session(control, opt))
                return DEVICE_EPG_FETCH_CONTROL_ERROR;
        } else {
            return DEVICE_EPG_FETCH_CONTROL_ERROR;
        }
    }

    if (transfer_length < 30U) return DEVICE_EPG_FETCH_EMPTY;
    if (transfer_length > XML_FILE_LIMIT) {
        fprintf(stderr, "Device reported invalid EPG size %u.\n",
                (unsigned)transfer_length);
        return DEVICE_EPG_FETCH_CONTROL_ERROR;
    }

    response = (uint8_t *)malloc((size_t)transfer_length + CSW_SIZE);
    if (response == NULL) return DEVICE_EPG_FETCH_CONTROL_ERROR;
    build_device_epg_data_request(frame, program_index, transfer_length);
    if (!tcp_command(*control, frame, transfer_length, response,
                     (size_t)transfer_length + CSW_SIZE, opt->verbose) ||
        response[0] != 0xF6 || response[1] != 0x31) {
        free(response);
        return DEVICE_EPG_FETCH_CONTROL_ERROR;
    }

    body_length = (size_t)transfer_length - 2U;
    while (body_length > 0U &&
           (response[2U + body_length - 1U] == 0U ||
            response[2U + body_length - 1U] == 0xFFU)) --body_length;
    if (body_length == 0U) {
        free(response);
        return DEVICE_EPG_FETCH_EMPTY;
    }
    if (response[2] != '<') {
        fprintf(stderr, "Native EPG response is not XML.\n");
        free(response);
        return DEVICE_EPG_FETCH_CONTROL_ERROR;
    }

    *xml = (uint8_t *)malloc(body_length + 1U);
    if (*xml == NULL) {
        free(response);
        return DEVICE_EPG_FETCH_CONTROL_ERROR;
    }
    memcpy(*xml, response + 2, body_length);
    (*xml)[body_length] = 0;
    *xml_length = body_length;
    free(response);
    return DEVICE_EPG_FETCH_OK;
}

static bool parse_hhmm_seconds(const char *text, int *seconds)
{
    int h = 0, m = 0, s = 0;
    if (sscanf(text, "%d:%d:%d", &h, &m, &s) < 2 ||
        h < 0 || m < 0 || m > 59 || s < 0 || s > 59) return false;
    *seconds = h * 3600 + m * 60 + s;
    return true;
}

static bool parse_native_epg_xml(const uint8_t *xml,
                                 const channel_t *channel,
                                 epg_list_t *list,
                                 size_t *capacity)
{
    char *document = (char *)malloc(strlen((const char *)xml) + 1U);
    char *cursor;
    if (document == NULL) return false;
    strcpy(document, (const char *)xml);
    cursor = document;
    while ((cursor = strstr(cursor, "<evt")) != NULL) {
        char *end = find_xml_tag_end(cursor);
        char saved;
        char text[512];
        char date_text[64], time_text[64], duration_text[64];
        uint32_t mjd;
        int start_seconds, duration_seconds;
        epg_program_t program;
        if (end == NULL) break;
        saved = end[1];
        end[1] = '\0';
        memset(&program, 0, sizeof(program));
        if (xml_attribute(cursor, "des", text, sizeof(text)) &&
            xml_attribute(cursor, "date", date_text, sizeof(date_text)) &&
            xml_attribute(cursor, "time", time_text, sizeof(time_text)) &&
            xml_attribute(cursor, "dur", duration_text, sizeof(duration_text)) &&
            parse_u32(date_text, &mjd) &&
            parse_hhmm_seconds(time_text, &start_seconds) &&
            parse_hhmm_seconds(duration_text, &duration_seconds) &&
            duration_seconds > 0) {
            const int64_t mjd_epoch = days_from_civil(1858, 11U, 17U);
            snprintf(program.channel_id, sizeof(program.channel_id), "%s",
                     channel->epg_id);
            xml_decode(text);
            trim_in_place(text);
            snprintf(program.title, sizeof(program.title), "%.255s",
                     *text != '\0' ? text : "Untitled programme");
            program.start_utc = (mjd_epoch + (int64_t)mjd) * 86400LL +
                                (int64_t)start_seconds;
            program.stop_utc = program.start_utc + duration_seconds;
            if (!append_epg_program(list, capacity, &program)) {
                end[1] = saved;
                free(document);
                return false;
            }
        }
        end[1] = saved;
        cursor = end + 1;
    }
    free(document);
    return true;
}


static bool xml_write_escaped(FILE *file, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        const char *entity = NULL;
        switch (*p) {
            case '&': entity = "&amp;"; break;
            case '<': entity = "&lt;"; break;
            case '>': entity = "&gt;"; break;
            case '\"': entity = "&quot;"; break;
            case '\'': entity = "&apos;"; break;
            default: break;
        }
        if (entity != NULL) {
            if (fputs(entity, file) == EOF) return false;
        } else if (fputc(*p, file) == EOF) return false;
        ++p;
    }
    return true;
}

static void format_xmltv_utc(int64_t utc, char output[32])
{
    const int64_t days = utc / 86400LL;
    int64_t seconds = utc % 86400LL;
    int year;
    unsigned month, day;
    int z, era, doe, yoe, doy, mp;
    if (seconds < 0) seconds += 86400LL;
    z = (int)(days + 719468LL);
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    day = (unsigned)(doy - (153 * mp + 2) / 5 + 1);
    month = (unsigned)(mp + (mp < 10 ? 3 : -9));
    year += month <= 2U;
    snprintf(output, 32, "%04d%02u%02u%02lld%02lld%02lld +0000",
             year, month, day,
             (long long)(seconds / 3600LL),
             (long long)((seconds / 60LL) % 60LL),
             (long long)(seconds % 60LL));
}

static bool write_epg_xmltv(const char *path,
                            const channel_list_t *channels,
                            const epg_list_t *epg)
{
    char temporary[1200];
    FILE *file;
    size_t i;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 ||
        strlen(path) + 4U >= sizeof(temporary)) return false;
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    if (fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<tv generator-info-name=\"SORALink\">\n", file) == EOF)
        goto fail;
    for (i = 0; i < channels->count; ++i) {
        if (fputs("  <channel id=\"", file) == EOF ||
            !xml_write_escaped(file, channels->items[i].epg_id) ||
            fputs("\"><display-name>", file) == EOF ||
            !xml_write_escaped(file, channels->items[i].name) ||
            fputs("</display-name></channel>\n", file) == EOF) goto fail;
    }
    for (i = 0; i < epg->count; ++i) {
        char start[32], stop[32];
        const epg_program_t *program = &epg->items[i];
        format_xmltv_utc(program->start_utc, start);
        format_xmltv_utc(program->stop_utc, stop);
        if (fprintf(file, "  <programme start=\"%s\" stop=\"%s\" channel=\"",
                    start, stop) < 0 ||
            !xml_write_escaped(file, program->channel_id) ||
            fputs("\"><title>", file) == EOF ||
            !xml_write_escaped(file, program->title) ||
            fputs("</title></programme>\n", file) == EOF) goto fail;
    }
    if (fputs("</tv>\n", file) == EOF || fflush(file) != 0 || fclose(file) != 0)
        goto fail_closed;
#ifdef _WIN32
    remove(path);
#endif
    if (rename(temporary, path) != 0) { remove(temporary); return false; }
    return true;
fail:
    fclose(file);
fail_closed:
    remove(temporary);
    return false;
}

static bool channel_is_replaced(const channel_list_t *channels,
                                const bool *replace,
                                const char *epg_id)
{
    size_t i;
    for (i = 0; i < channels->count; ++i)
        if (replace[i] && strcmp(channels->items[i].epg_id, epg_id) == 0)
            return true;
    return false;
}

static bool device_refresh_epg(socket_t *control,
                               const options_t *opt,
                               const channel_list_t *channels,
                               epg_list_t *epg,
                               device_update_state_t *state,
                               bool force)
{
    epg_list_t merged;
    size_t capacity = 0U, i;
    bool *replace = (bool *)calloc(channels->count, sizeof(bool));
    size_t updated = 0U, empty = 0U, failed = 0U;
    size_t session_channels = 0U;
    size_t proactive_reconnects = 0U;
    size_t recovery_reconnects = 0U;
    bool completed = true;
    bool control_restored = true;

    (void)force;
    if (replace == NULL && channels->count != 0U) return false;
    memset(&merged, 0, sizeof(merged));
    state->busy = true;
    state->last_epg_attempt_utc = (int64_t)time(NULL);
    snprintf(state->last_action, sizeof(state->last_action), "epg");
    snprintf(state->last_message, sizeof(state->last_message),
             "Refreshing native device EPG");

    for (i = 0; i < channels->count; ++i) {
        const channel_t *channel = &channels->items[i];
        uint8_t *xml = NULL;
        size_t xml_length = 0U;
        size_t before;
        unsigned retry;
        device_epg_fetch_result_t fetch = DEVICE_EPG_FETCH_CONTROL_ERROR;

        if (!channel->have_program_index) {
            ++failed;
            continue;
        }

        if (session_channels >= DEVICE_EPG_CHANNELS_PER_SESSION) {
            printf("Native EPG: rotating receiver control session after %zu channels.\n",
                   session_channels);
            if (!reconnect_control_session(control, opt)) {
                completed = false;
                control_restored = false;
                ++failed;
                break;
            }
            ++proactive_reconnects;
            session_channels = 0U;
        }

        for (retry = 0U; retry <= DEVICE_EPG_FETCH_RETRIES; ++retry) {
            fetch = device_download_epg_xml(control, opt, state,
                                            channel->program_index,
                                            &xml, &xml_length);
            if (fetch != DEVICE_EPG_FETCH_CONTROL_ERROR) break;

            free(xml);
            xml = NULL;
            xml_length = 0U;

            if (retry >= DEVICE_EPG_FETCH_RETRIES) {
                completed = false;
                ++failed;
                break;
            }

            fprintf(stderr,
                    "Native EPG: receiver session ended at channel %zu/%zu; "
                    "reconnecting and retrying this channel.\n",
                    i + 1U, channels->count);
            if (!reconnect_control_session(control, opt)) {
                completed = false;
                control_restored = false;
                ++failed;
                break;
            }
            ++recovery_reconnects;
            session_channels = 0U;
        }

        if (!completed && fetch == DEVICE_EPG_FETCH_CONTROL_ERROR) break;
        ++session_channels;

        if (fetch == DEVICE_EPG_FETCH_EMPTY) {
            ++empty;
            if (((i + 1U) % 50U) == 0U || i + 1U == channels->count) {
                printf("Native EPG: checked %zu/%zu channels "
                       "(%zu updated, %zu empty, %zu failed).\n",
                       i + 1U, channels->count, updated, empty, failed);
            }
            continue;
        }
        if (fetch != DEVICE_EPG_FETCH_OK) {
            ++failed;
            continue;
        }

        before = merged.count;
        if (!parse_native_epg_xml(xml, channel, &merged, &capacity)) {
            free(xml);
            free(replace);
            free_epg_list(&merged);
            state->busy = false;
            return false;
        }
        free(xml);
        if (merged.count > before) {
            replace[i] = true;
            ++updated;
        } else {
            ++empty;
        }
        if (((i + 1U) % 50U) == 0U || i + 1U == channels->count) {
            printf("Native EPG: checked %zu/%zu channels "
                   "(%zu updated, %zu empty, %zu failed).\n",
                   i + 1U, channels->count, updated, empty, failed);
        }
    }

    for (i = 0; i < epg->count; ++i) {
        if (!channel_is_replaced(channels, replace, epg->items[i].channel_id) &&
            !append_epg_program(&merged, &capacity, &epg->items[i])) {
            free(replace);
            free_epg_list(&merged);
            state->busy = false;
            return false;
        }
    }
    free(replace);
    if (merged.count > 1U)
        qsort(merged.items, merged.count, sizeof(merged.items[0]), compare_epg_programs);
    merged.loaded_utc = (int64_t)time(NULL);
    if (!write_epg_xmltv(opt->epg_path, channels, &merged)) {
        free_epg_list(&merged);
        snprintf(state->last_message, sizeof(state->last_message),
                 "Could not write native XMLTV file");
        state->busy = false;
        return false;
    }
    free_epg_list(epg);
    *epg = merged;

    if (*control == SOCKET_INVALID) {
        control_restored = reconnect_control_session(control, opt);
    }

    state->last_epg_updated_channels = updated;
    state->last_epg_skipped_channels = empty;
    state->last_epg_failed_channels = failed;
    if (completed && control_restored) {
        state->last_epg_success_utc = (int64_t)time(NULL);
        if (updated == 0U && empty > 0U && failed == 0U) {
            snprintf(state->last_message, sizeof(state->last_message),
                     "Receiver EPG cache is empty: 0 updated, %zu empty; "
                     "%zu planned and %zu recovery reconnects",
                     empty, proactive_reconnects, recovery_reconnects);
        } else {
            snprintf(state->last_message, sizeof(state->last_message),
                     "EPG refresh complete: %zu updated, %zu empty, %zu failed; "
                     "%zu planned and %zu recovery reconnects",
                     updated, empty, failed,
                     proactive_reconnects, recovery_reconnects);
        }
    } else {
        snprintf(state->last_message, sizeof(state->last_message),
                 "EPG refresh stopped at %zu/%zu channels; control %s",
                 i < channels->count ? i + 1U : channels->count,
                 channels->count,
                 control_restored ? "was restored" : "could not be restored");
    }
    state->busy = false;

    return completed && control_restored;
}


static bool sweep_channel_same(const channel_t *a, const channel_t *b)
{
    if (a->have_program_index && b->have_program_index &&
        a->program_index == b->program_index) return true;
    return a->service_id == b->service_id &&
           a->transport_stream_id == b->transport_stream_id &&
           a->frequency_mhz == b->frequency_mhz &&
           a->polarization == b->polarization;
}

static bool sweep_channel_matches_tp(const channel_t *channel,
                                     const satellite_transponder_t *tp)
{
    const uint32_t a = channel->frequency_mhz;
    const uint32_t b = tp->frequency_mhz;
    const uint32_t difference = a > b ? a - b : b - a;
    return difference <= 2U &&
           channel->polarization == tp->polarization &&
           channel->symbol_rate_ks == tp->symbol_rate_ks;
}

static bool sweep_append_channel(channel_list_t *list,
                                 const channel_t *channel)
{
    size_t i;
    channel_t *items;
    for (i = 0U; i < list->count; ++i) {
        if (sweep_channel_same(&list->items[i], channel)) return true;
    }
    if (list->count == SIZE_MAX / sizeof(*items)) return false;
    items = (channel_t *)realloc(list->items,
                                (list->count + 1U) * sizeof(*items));
    if (items == NULL) return false;
    list->items = items;
    list->items[list->count] = *channel;
    ++list->count;
    return true;
}

static bool device_sweep_collect_current(socket_t *control,
                                         const options_t *opt,
                                         device_update_state_t *state,
                                         size_t *added)
{
    const satellite_transponder_t *tp =
        &astra_19e_transponders[state->scan_sweep_index];
    uint8_t *xml = NULL;
    size_t xml_length = 0U;
    char path[1200];
    channel_list_t snapshot;
    size_t i;
    unsigned attempt;
    bool downloaded = false;

    *added = 0U;
    memset(&snapshot, 0, sizeof(snapshot));
    if (snprintf(path, sizeof(path), "%s.sweep-current",
                 opt->channels_path) < 0 ||
        strlen(opt->channels_path) + strlen(".sweep-current") >= sizeof(path)) {
        return false;
    }
    (void)remove(path);

    for (attempt = 1U;
         attempt <= DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS && g_running;
         ++attempt) {
        free(xml);
        xml = NULL;
        xml_length = 0U;
        if (*control == SOCKET_INVALID &&
            !reconnect_control_session(control, opt)) {
            downloaded = false;
        } else if (device_download_channel_xml(*control, opt,
                                               &xml, &xml_length)) {
            downloaded = true;
            break;
        }

        if (attempt < DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS) {
            const unsigned delay_ms = 500U * attempt;
            fprintf(stderr,
                    "Astra sweep %zu/%zu: Device database not ready "
                    "(attempt %u/%u); retrying in %u ms.\n",
                    state->scan_sweep_index + 1U,
                    state->scan_sweep_total,
                    attempt, DEVICE_CHANNEL_DOWNLOAD_ATTEMPTS,
                    delay_ms);
            SLEEP_MS(delay_ms);
            if (*control != SOCKET_INVALID) {
                CLOSESOCKET(*control);
                *control = SOCKET_INVALID;
            }
        }
    }
    if (!downloaded) {
        free(xml);
        return false;
    }

    if (!atomic_write_bytes(path, xml, xml_length)) {
        free(xml);
        return false;
    }
    free(xml);
    if (!load_channel_list(path, &snapshot)) {
        (void)remove(path);
        return false;
    }
    (void)remove(path);

    for (i = 0U; i < snapshot.count; ++i) {
        if (sweep_channel_matches_tp(&snapshot.items[i], tp)) {
            const size_t before = state->scan_sweep_channels.count;
            if (!sweep_append_channel(&state->scan_sweep_channels,
                                      &snapshot.items[i])) {
                free_channel_list(&snapshot);
                return false;
            }
            if (state->scan_sweep_channels.count > before) ++*added;
        }
    }
    free_channel_list(&snapshot);
    return true;
}

static bool write_sweep_channel_list(const char *path,
                                     channel_list_t *list,
                                     const channel_list_t *previous)
{
    char temporary[1200];
    FILE *file;
    size_t i;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 ||
        strlen(path) + 4U >= sizeof(temporary)) return false;
    preserve_channel_epg_ids(list, previous);
    for (i = 0U; i < list->count; ++i) {
        list->items[i].lcn = (uint32_t)(i + 1U);
        if (list->items[i].epg_id[0] == '\0') {
            snprintf(list->items[i].epg_id,
                     sizeof(list->items[i].epg_id), "%u",
                     (unsigned)list->items[i].lcn);
        }
    }

    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    if (fputs("<ch_List>\n", file) == EOF) goto fail;
    for (i = 0U; i < list->count; ++i) {
        const channel_t *ch = &list->items[i];
        if (fprintf(file,
                    "<ch type=\"%s\" pol=\"%c\" sym=\"%u\" "
                    "s_id=\"%u\" ts_id=\"%u\" freq=\"%u\" ",
                    ch->type, ch->polarization,
                    (unsigned)ch->symbol_rate_ks,
                    (unsigned)ch->service_id,
                    (unsigned)ch->transport_stream_id,
                    (unsigned)ch->frequency_mhz) < 0) goto fail;
        if (ch->have_program_index &&
            fprintf(file, "prog_idx=\"%u\" ",
                    (unsigned)ch->program_index) < 0) goto fail;
        if (fprintf(file, "lcn=\"%u\" s_name=\"",
                    (unsigned)ch->lcn) < 0 ||
            !xml_write_escaped(file, ch->name) ||
            fprintf(file, "\" fta=\"%u\" epg_id=\"",
                    ch->fta ? 1U : 0U) < 0 ||
            !xml_write_escaped(file, ch->epg_id) ||
            fputs("\" />\n", file) == EOF) goto fail;
    }
    if (fputs("</ch_List>\n", file) == EOF ||
        fflush(file) != 0 || fclose(file) != 0) goto fail_closed;
#ifdef _WIN32
    if (!MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(temporary);
        return false;
    }
#else
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return false;
    }
#endif
    return true;
fail:
    fclose(file);
fail_closed:
    remove(temporary);
    return false;
}

static void sweep_count_channel_types(const channel_list_t *list,
                                      unsigned *tv_count,
                                      unsigned *radio_count)
{
    size_t i;
    unsigned tv = 0U;
    unsigned radio = 0U;
    for (i = 0U; i < list->count; ++i) {
        if (strcmp(list->items[i].type, "RADIO") == 0) ++radio;
        else ++tv;
    }
    *tv_count = tv;
    *radio_count = radio;
}

static bool device_finish_transponder_sweep(socket_t *control,
                                            const options_t *opt,
                                            channel_list_t *channels,
                                            epg_list_t *epg,
                                            device_update_state_t *state,
                                            tuning_state_t *tuning)
{
    channel_list_t completed = state->scan_sweep_channels;
    state->scan_sweep_channels.items = NULL;
    state->scan_sweep_channels.count = 0U;

    if (completed.count == 0U) {
        free_channel_list(&completed);
        device_scan_mark_stopped(state,
                                 "Astra transponder sweep found no channels");
        return false;
    }
    if (!write_sweep_channel_list(opt->channels_path, &completed, channels)) {
        free_channel_list(&completed);
        device_scan_mark_stopped(state,
                                 "Could not install Astra sweep channel list");
        return false;
    }

    free_channel_list(channels);
    *channels = completed;
    state->scan_running = false;
    state->scan_transponder_sweep = false;
    state->busy = false;
    state->scan_progress = 100U;
    state->last_channels_success_utc = (int64_t)time(NULL);
    printf("Astra 19.2E sweep complete: %zu unique services installed.\n",
           channels->count);

    if (opt->device_scan_epg_after &&
        !device_refresh_epg(control, opt, channels, epg, state, true)) {
        memset(tuning, 0, sizeof(*tuning));
        return false;
    }
    state->last_scan_success_utc = (int64_t)time(NULL);
    snprintf(state->last_action, sizeof(state->last_action), "scan");
    snprintf(state->last_message, sizeof(state->last_message),
             "Astra sweep complete: %zu services; %zu EPG channels updated",
             channels->count, state->last_epg_updated_channels);
    state->next_channels_due = monotonic_seconds() +
        (double)opt->device_channels_refresh_minutes * 60.0;
    state->next_epg_due = monotonic_seconds() +
        (double)opt->device_epg_refresh_minutes * 60.0;
    state->next_scan_due = monotonic_seconds() +
        (double)opt->device_scan_refresh_minutes * 60.0;
    memset(tuning, 0, sizeof(*tuning));
    return true;
}


static bool device_scan_step(socket_t *control,
                             const options_t *opt,
                             channel_list_t *channels,
                             epg_list_t *epg,
                             device_update_state_t *state,
                             tuning_state_t *tuning)
{
    uint8_t frame[FRAME_SIZE];
    uint8_t response[17U + CSW_SIZE];
    const double now = monotonic_seconds();
    unsigned raw_progress;
    unsigned raw_tv_count;
    unsigned raw_radio_count;

    if (!state->scan_running) return true;
    if (state->scan_cancel_requested) {
        (void)device_cancel_scan(control, opt, state);
        memset(tuning, 0, sizeof(*tuning));
        return true;
    }
    if (now >= state->scan_deadline) {
        (void)device_send_scan_action(control, opt, DEVICE_SCAN_STOP_ACTION);
        device_scan_mark_stopped(state,
            state->scan_transponder_sweep
                ? "Astra transponder sweep timed out"
                : "Receiver channel scan timed out");
        memset(tuning, 0, sizeof(*tuning));
        return false;
    }
    if (now < state->scan_next_poll) return true;

    build_device_scan_status_request(frame);
    if (*control == SOCKET_INVALID ||
        !tcp_command(*control, frame, 17U, response, sizeof(response),
                     opt->verbose) ||
        response[0] != 0xF6 || response[1] != 0x02) {
        fprintf(stderr, "%s scan-status poll failed; reconnecting.\n",
                state->scan_transponder_sweep ? "Astra sweep" : "Receiver");
        if (!reconnect_control_session(control, opt)) {
            device_scan_mark_stopped(state,
                state->scan_transponder_sweep
                    ? "Astra sweep stopped: control reconnect failed"
                    : "Channel scan stopped: control reconnect failed");
            return false;
        }
        state->scan_next_poll = monotonic_seconds() + DEVICE_SCAN_POLL_SECONDS;
        return true;
    }

    state->scan_state = response[5];
    raw_progress = response[6] > 100U ? 100U : response[6];
    raw_tv_count = read_u16_be(&response[13]);
    raw_radio_count = read_u16_be(&response[15]);
    state->scan_mode = response[12];
    state->scan_next_poll = now + DEVICE_SCAN_POLL_SECONDS;

    if (state->scan_transponder_sweep) {
        const satellite_transponder_t *tp =
            &astra_19e_transponders[state->scan_sweep_index];
        unsigned accumulated_tv;
        unsigned accumulated_radio;
        const size_t numerator = state->scan_sweep_index * 100U + raw_progress;

        sweep_count_channel_types(&state->scan_sweep_channels,
                                  &accumulated_tv, &accumulated_radio);
        state->scan_progress = state->scan_sweep_total > 0U
            ? (unsigned)(numerator / state->scan_sweep_total) : 0U;
        if (state->scan_progress > 99U &&
            state->scan_sweep_index + 1U < state->scan_sweep_total) {
            state->scan_progress = 99U;
        }
        state->scan_frequency_mhz = tp->frequency_mhz;
        state->scan_symbol_rate_ks = tp->symbol_rate_ks;
        state->scan_tv_count = accumulated_tv;
        state->scan_radio_count = accumulated_radio;

        if (state->scan_state == 1U) {
            snprintf(state->last_message, sizeof(state->last_message),
                     "Astra sweep %zu/%zu: %u MHz %c, TP %u%%; %u services merged",
                     state->scan_sweep_index + 1U,
                     state->scan_sweep_total,
                     (unsigned)tp->frequency_mhz,
                     tp->polarization,
                     raw_progress,
                     accumulated_tv + accumulated_radio);
            printf("Astra sweep %zu/%zu: %u MHz %c %u kSym/s, "
                   "TP %u%%, reported TV %u/radio %u, merged %u.\n",
                   state->scan_sweep_index + 1U,
                   state->scan_sweep_total,
                   (unsigned)tp->frequency_mhz,
                   tp->polarization,
                   (unsigned)tp->symbol_rate_ks,
                   raw_progress,
                   raw_tv_count,
                   raw_radio_count,
                   accumulated_tv + accumulated_radio);
            return true;
        }

        if (state->scan_state != 0U) {
            device_scan_mark_stopped(state,
                                     "Device reported an invalid Astra sweep state");
            memset(tuning, 0, sizeof(*tuning));
            return false;
        }

        {
            size_t added = 0U;
            bool collected;

            printf("Astra sweep %zu/%zu: transponder scan complete; "
                   "waiting for Device database commit.\n",
                   state->scan_sweep_index + 1U,
                   state->scan_sweep_total);
            SLEEP_MS(DEVICE_SCAN_TP_COMMIT_WAIT_MS);

            if (*control != SOCKET_INVALID) {
                CLOSESOCKET(*control);
                *control = SOCKET_INVALID;
            }
            collected = reconnect_control_session(control, opt) &&
                        device_sweep_collect_current(control, opt, state, &added);
            if (!collected) {
                fprintf(stderr,
                        "Warning: Astra sweep %zu/%zu could not download the "
                        "current transponder result; continuing.\n",
                        state->scan_sweep_index + 1U,
                        state->scan_sweep_total);
            } else {
                sweep_count_channel_types(&state->scan_sweep_channels,
                                          &accumulated_tv, &accumulated_radio);
                state->scan_tv_count = accumulated_tv;
                state->scan_radio_count = accumulated_radio;
                printf("Astra sweep %zu/%zu: added %zu service%s; "
                       "%zu unique services merged.\n",
                       state->scan_sweep_index + 1U,
                       state->scan_sweep_total,
                       added, added == 1U ? "" : "s",
                       state->scan_sweep_channels.count);
            }
        }

        ++state->scan_sweep_index;
        if (state->scan_sweep_index < state->scan_sweep_total) {
            state->scan_progress = (unsigned)
                ((state->scan_sweep_index * 100U) / state->scan_sweep_total);
            if (!device_start_sweep_transponder(control, opt, state)) {
                device_scan_mark_stopped(state,
                                         "Could not start the next Astra transponder");
                memset(tuning, 0, sizeof(*tuning));
                return false;
            }
            return true;
        }

        return device_finish_transponder_sweep(control, opt, channels, epg,
                                               state, tuning);
    }

    state->scan_progress = raw_progress;
    state->scan_frequency_mhz = ((uint32_t)response[7] << 16) |
                                ((uint32_t)response[8] << 8) |
                                (uint32_t)response[9];
    state->scan_symbol_rate_ks = read_u16_be(&response[10]);
    state->scan_tv_count = raw_tv_count;
    state->scan_radio_count = raw_radio_count;

    if (state->scan_state == 1U) {
        snprintf(state->last_message, sizeof(state->last_message),
                 "Scanning: %u%%, %u MHz, %u kSym/s, TV %u, radio %u",
                 state->scan_progress,
                 (unsigned)state->scan_frequency_mhz,
                 (unsigned)state->scan_symbol_rate_ks,
                 state->scan_tv_count, state->scan_radio_count);
        printf("Receiver scan: %u%%, %u MHz, %u kSym/s, TV %u, radio %u.\n",
               state->scan_progress,
               (unsigned)state->scan_frequency_mhz,
               (unsigned)state->scan_symbol_rate_ks,
               state->scan_tv_count, state->scan_radio_count);
        return true;
    }

    if (state->scan_state != 0U) {
        device_scan_mark_stopped(state, "Receiver reported an invalid scan state");
        memset(tuning, 0, sizeof(*tuning));
        return false;
    }

    printf("Receiver scan completed: TV %u, radio %u. "
           "Waiting for receiver database commit.\n",
           state->scan_tv_count, state->scan_radio_count);
    state->scan_running = false;
    state->busy = true;
    snprintf(state->last_message, sizeof(state->last_message),
             "Scan complete; waiting for receiver database commit");

    SLEEP_MS(DEVICE_SCAN_COMMIT_WAIT_MS);
    if (!reconnect_control_session(control, opt)) {
        device_scan_mark_stopped(
            state, "Scan completed, but post-scan reconnect failed");
        memset(tuning, 0, sizeof(*tuning));
        return false;
    }

    snprintf(state->last_message, sizeof(state->last_message),
             "Receiver database committed; downloading channel list");

    state->busy = false;
    if (!device_refresh_channels(control, opt, channels, state)) {
        state->last_message[sizeof(state->last_message) - 1U] = '\0';
        memset(tuning, 0, sizeof(*tuning));
        return false;
    }
    if (opt->device_scan_epg_after &&
        !device_refresh_epg(control, opt, channels, epg, state, true)) {
        memset(tuning, 0, sizeof(*tuning));
        return false;
    }

    state->last_scan_success_utc = (int64_t)time(NULL);
    state->scan_progress = 100U;
    state->busy = false;
    snprintf(state->last_action, sizeof(state->last_action), "scan");
    snprintf(state->last_message, sizeof(state->last_message),
             "Receiver scan complete: TV %u, radio %u; %zu EPG channels updated",
             state->scan_tv_count, state->scan_radio_count,
             state->last_epg_updated_channels);
    state->next_channels_due = monotonic_seconds() +
        (double)opt->device_channels_refresh_minutes * 60.0;
    state->next_epg_due = monotonic_seconds() +
        (double)opt->device_epg_refresh_minutes * 60.0;
    state->next_scan_due = monotonic_seconds() +
        (double)opt->device_scan_refresh_minutes * 60.0;
    memset(tuning, 0, sizeof(*tuning));
    return true;
}

static void device_update_state_free(device_update_state_t *state)
{
    free_channel_list(&state->scan_sweep_channels);
    memset(state, 0, sizeof(*state));
}

static void find_epg_now_next(const epg_list_t *epg,
                              const char *channel_id,
                              int64_t now,
                              const epg_program_t **current,
                              const epg_program_t **next)
{
    size_t i;
    *current = NULL;
    *next = NULL;
    if (channel_id == NULL || *channel_id == '\0') {
        return;
    }
    for (i = 0; i < epg->count; ++i) {
        const epg_program_t *program = &epg->items[i];
        if (strcmp(program->channel_id, channel_id) != 0) {
            continue;
        }
        if (program->start_utc <= now && now < program->stop_utc) {
            *current = program;
        } else if (program->start_utc > now) {
            *next = program;
            if (*current != NULL || program->start_utc >= now) {
                break;
            }
        }
    }
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

static tune_result_t tune_channel(socket_t control,
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
        if (base_opt->verbose && base_opt->tone == TONE_AUTO) {
            const tone_mode_t resolved = resolve_tone_mode(&tune_opt);
            printf("LNB tone auto resolved to %s for this transponder.\n",
                   resolved == TONE_ON ? "ON" :
                   resolved == TONE_OFF ? "OFF" : "AUTO");
        }
        build_dvbs_tune(frame, &tune_opt);
        if (!tcp_command(control, frame, 2,
                         response, sizeof(response), base_opt->verbose)) {
            state->valid = false;
            return TUNE_RESULT_CONTROL_ERROR;
        }
        SLEEP_MS(300);
    }

    build_service_id_tune(frame, channel->service_id);
    if (!tcp_command(control, frame, 4,
                     response, sizeof(response), base_opt->verbose)) {
        state->valid = false;
        return TUNE_RESULT_CONTROL_ERROR;
    }
    {
        const tune_result_t lock_result =
            response_reports_lock(response, 4, "Channel tune");
        if (lock_result != TUNE_RESULT_OK) {
            state->valid = false;
            return lock_result;
        }
    }

    state->valid = true;
    state->frequency_mhz = channel->frequency_mhz;
    state->symbol_rate_ks = channel->symbol_rate_ks;
    state->polarization = channel->polarization;
    state->service_id = channel->service_id;

    SLEEP_MS(base_opt->wait_ms);
    return TUNE_RESULT_OK;
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
    uint64_t id;
    char peer_host[128];
    char peer_port[16];
    double connected_at;
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
    HTTP_METHOD_POST,
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
    uint64_t total_bytes_forwarded;
    double last_mbps;
    double server_started;
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
                                   uint64_t connection_id,
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
    connection->id = connection_id;
    connection->connected_at = monotonic_seconds();
    if (getnameinfo((const struct sockaddr *)&peer, peer_length,
                    connection->peer_host, sizeof(connection->peer_host),
                    connection->peer_port, sizeof(connection->peer_port),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        snprintf(connection->peer_host, sizeof(connection->peer_host), "unknown");
        connection->peer_port[0] = '\0';
    }

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

static bool http_get_header(const char *request,
                            const char *name,
                            char *value,
                            size_t value_capacity);

static const char *http_request_body(const char *request)
{
    const char *body = strstr(request, "\r\n\r\n");
    if (body != NULL) {
        return body + 4;
    }
    body = strstr(request, "\n\n");
    return body != NULL ? body + 2 : NULL;
}

static bool read_http_request(http_connection_t *connection,
                              char *request,
                              size_t capacity)
{
    size_t used = 0;
    size_t required = 0;
    bool headers_complete = false;

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

        if (!headers_complete) {
            const char *body = http_request_body(request);
            if (body != NULL) {
                char length_text[64];
                char transfer_encoding[64];
                uint32_t content_length = 0;
                const size_t header_bytes = (size_t)(body - request);
                headers_complete = true;
                if (http_get_header(request, "Transfer-Encoding",
                                    transfer_encoding,
                                    sizeof(transfer_encoding))) {
                    return false;
                }
                if (http_get_header(request, "Content-Length",
                                    length_text, sizeof(length_text))) {
                    if (!parse_u32(length_text, &content_length) ||
                        content_length > 8192U) {
                        return false;
                    }
                }
                if ((size_t)content_length > capacity - header_bytes - 1U) {
                    return false;
                }
                required = header_bytes + (size_t)content_length;
            }
        }
        if (headers_complete && used >= required) {
            request[required] = '\0';
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
    } else if (method_length == 4U && memcmp(request, "POST", 4) == 0) {
        line->method = HTTP_METHOD_POST;
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

static bool http_authorized_credentials(const char *request,
                                        const char *user,
                                        const char *password)
{
    char credentials[1024];
    char encoded[1400];
    char expected[1410];
    char supplied[1500];
    int length;

    if (user == NULL) {
        return true;
    }
    length = snprintf(credentials, sizeof(credentials), "%s:%s",
                      user, password);
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

static bool http_viewer_authorized(const char *request, const options_t *opt)
{
    return http_authorized_credentials(request,
                                       opt->http_user,
                                       opt->http_password);
}

static bool http_admin_authorized(const char *request, const options_t *opt)
{
    if (opt->admin_user != NULL) {
        return http_authorized_credentials(request,
                                           opt->admin_user,
                                           opt->admin_password);
    }
    return http_viewer_authorized(request, opt);
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
                            bool head_only);

static bool build_web_path(const options_t *opt,
                           const char *filename,
                           char *path,
                           size_t path_capacity)
{
    const size_t root_length = strlen(opt->web_root != NULL ? opt->web_root : "web");
    const bool has_separator = root_length > 0U &&
        (opt->web_root[root_length - 1U] == '/' ||
         opt->web_root[root_length - 1U] == '\\');
    const int length = snprintf(path, path_capacity, "%s%s%s",
                                opt->web_root != NULL ? opt->web_root : "web",
                                has_separator ? "" : "/", filename);
    return length > 0 && (size_t)length < path_capacity;
}

static void send_http_file(http_connection_t *connection,
                           const char *path,
                           const char *content_type,
                           bool head_only,
                           bool cacheable)
{
    FILE *file = fopen(path, "rb");
    long file_length;
    char header[1600];
    int header_length;
    uint8_t buffer[16384];

    if (file == NULL) {
        send_http_error(connection, 404, "Not Found",
                        "The requested Web UI asset is missing. Check --web-root.\n",
                        head_only);
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        send_http_error(connection, 500, "Internal Server Error",
                        "The requested Web UI asset could not be read.\n",
                        head_only);
        return;
    }
    header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Cache-Control: %s\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Permissions-Policy: camera=(), microphone=(), geolocation=()\r\n"
        "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "Connection: close\r\n\r\n",
        content_type, file_length,
        cacheable ? "public, max-age=3600" : "no-store");
    if (header_length <= 0 || (size_t)header_length >= sizeof(header) ||
        !http_connection_send_all(connection,
                                  (const uint8_t *)header,
                                  (size_t)header_length)) {
        fclose(file);
        return;
    }
    if (!head_only) {
        size_t amount;
        while ((amount = fread(buffer, 1U, sizeof(buffer), file)) > 0U) {
            if (!http_connection_send_all(connection, buffer, amount)) {
                break;
            }
        }
    }
    fclose(file);
}

static void serve_web_asset(http_connection_t *connection,
                            const options_t *opt,
                            const char *filename,
                            const char *content_type,
                            bool head_only,
                            bool cacheable)
{
    char path[2048];
    if (!build_web_path(opt, filename, path, sizeof(path))) {
        send_http_error(connection, 500, "Internal Server Error",
                        "The Web UI path is too long.\n", head_only);
        return;
    }
    send_http_file(connection, path, content_type, head_only, cacheable);
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
                                   const char *realm,
                                   bool head_only)
{
    char header[256];
    snprintf(header, sizeof(header),
             "WWW-Authenticate: Basic realm=\"%s\", charset=\"UTF-8\"\r\n",
             realm);
    send_http_response(connection, 401, "Unauthorized",
                       "text/plain; charset=utf-8", header,
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


static bool string_buffer_appendf(string_buffer_t *buffer,
                                  const char *format,
                                  ...)
{
    va_list args;
    va_list copy;
    int needed;

    va_start(args, format);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0 || !string_buffer_reserve(buffer, (size_t)needed)) {
        va_end(args);
        return false;
    }
    vsnprintf(buffer->data + buffer->length,
              buffer->capacity - buffer->length, format, args);
    va_end(args);
    buffer->length += (size_t)needed;
    return true;
}

static int url_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool form_url_decode(const char *input,
                            size_t input_length,
                            char *output,
                            size_t output_capacity)
{
    size_t in_pos = 0;
    size_t out_pos = 0;
    while (in_pos < input_length) {
        unsigned char ch = (unsigned char)input[in_pos++];
        if (ch == '+') {
            ch = ' ';
        } else if (ch == '%') {
            int high;
            int low;
            if (in_pos + 1U >= input_length) {
                return false;
            }
            high = url_hex_value(input[in_pos++]);
            low = url_hex_value(input[in_pos++]);
            if (high < 0 || low < 0) {
                return false;
            }
            ch = (unsigned char)((high << 4) | low);
        }
        if (ch == '\0' || out_pos + 1U >= output_capacity) {
            return false;
        }
        output[out_pos++] = (char)ch;
    }
    output[out_pos] = '\0';
    return true;
}

static bool form_get_value(const char *body,
                           const char *name,
                           char *value,
                           size_t value_capacity)
{
    const char *item = body;
    const size_t name_length = strlen(name);
    while (item != NULL && *item != '\0') {
        const char *amp = strchr(item, '&');
        const char *end = amp != NULL ? amp : item + strlen(item);
        const char *equals = memchr(item, '=', (size_t)(end - item));
        if (equals != NULL && (size_t)(equals - item) == name_length &&
            memcmp(item, name, name_length) == 0) {
            return form_url_decode(equals + 1, (size_t)(end - equals - 1),
                                   value, value_capacity);
        }
        item = amp != NULL ? amp + 1 : NULL;
    }
    return false;
}

static void get_http_host(const char *request,
                          const options_t *opt,
                          char *host,
                          size_t host_capacity);

static bool admin_post_origin_allowed(const char *request,
                                      const options_t *opt,
                                      const tls_server_t *tls)
{
    char origin[1024];
    char host[512];
    char expected[1100];
    char content_type[256];

    if (!http_get_header(request, "Content-Type",
                         content_type, sizeof(content_type)) ||
        STRNCASECMP(content_type,
                    "application/x-www-form-urlencoded", 33) != 0) {
        return false;
    }
    if (!http_get_header(request, "Origin", origin, sizeof(origin))) {
        return true;
    }
    get_http_host(request, opt, host, sizeof(host));
    snprintf(expected, sizeof(expected), "%s://%s",
             tls->enabled ? "https" : "http", host);
    return strcmp(origin, expected) == 0;
}

static void send_http_redirect(http_connection_t *connection,
                               const char *location)
{
    char headers[1200];
    snprintf(headers, sizeof(headers), "Location: %s\r\n", location);
    send_http_response(connection, 303, "See Other",
                       "text/plain; charset=utf-8", headers,
                       "Operation completed.\n", false);
}

static bool live_config_key(const char *key)
{
    return strcmp(key, "max_clients") == 0 ||
           strcmp(key, "wait_ms") == 0 ||
           strcmp(key, "timeout_ms") == 0 ||
           strcmp(key, "missing_key") == 0 ||
           strcmp(key, "device_channels_update") == 0 ||
           strcmp(key, "device_epg_update") == 0 ||
           strcmp(key, "device_channels_refresh_minutes") == 0 ||
           strcmp(key, "device_epg_refresh_minutes") == 0 ||
           strcmp(key, "device_scan_refresh_minutes") == 0 ||
           strcmp(key, "device_scan_timeout_minutes") == 0 ||
           strcmp(key, "device_scan_search_range") == 0 ||
           strcmp(key, "device_scan_order_by") == 0 ||
           strcmp(key, "device_scan_sort_mode") == 0 ||
           strcmp(key, "device_scan_mode") == 0 ||
           strcmp(key, "device_scan_network") == 0 ||
           strcmp(key, "device_scan_epg_after") == 0 ||
           strcmp(key, "device_scan_apply_satellite") == 0 ||
           strcmp(key, "orbital") == 0 ||
           strcmp(key, "west") == 0 ||
           strcmp(key, "tone") == 0 ||
           strcmp(key, "lnb_low") == 0 ||
           strcmp(key, "lnb_high") == 0 ||
           strcmp(key, "lnb_switch") == 0 ||
           strcmp(key, "diseqc") == 0 ||
           strcmp(key, "device_update_on_start") == 0;
}

static bool update_live_config_file(const options_t *opt)
{
    FILE *input;
    FILE *output;
    char temporary[1400];
    char line[4096];
    bool wrote_max = false;
    bool wrote_wait = false;
    bool wrote_timeout = false;
    bool wrote_missing = false;
    bool wrote_device_channels = false;
    bool wrote_device_epg = false;
    bool wrote_device_channels_minutes = false;
    bool wrote_device_epg_minutes = false;
    bool wrote_device_scan_minutes = false;
    bool wrote_device_scan_timeout = false;
    bool wrote_device_scan_range = false;
    bool wrote_device_scan_order = false;
    bool wrote_device_scan_sort = false;
    bool wrote_device_scan_mode = false;
    bool wrote_device_scan_network = false;
    bool wrote_device_scan_epg = false;
    bool wrote_device_scan_apply_satellite = false;
    bool wrote_orbital = false;
    bool wrote_west = false;
    bool wrote_tone = false;
    bool wrote_lnb_low = false;
    bool wrote_lnb_high = false;
    bool wrote_lnb_switch = false;
    bool wrote_diseqc = false;
    bool wrote_device_start = false;

    if (opt->config_path == NULL ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", opt->config_path) < 0 ||
        strlen(temporary) + 1U >= sizeof(temporary)) {
        return false;
    }
    input = fopen(opt->config_path, "rb");
    if (input == NULL) {
        return false;
    }
    output = fopen(temporary, "wb");
    if (output == NULL) {
        fclose(input);
        return false;
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        char copy[4096];
        char *text;
        char *equals;
        char *key;
        snprintf(copy, sizeof(copy), "%s", line);
        text = trim_config_text(copy);
        equals = strchr(text, '=');
        if (*text != '\0' && *text != '#' && *text != ';' &&
            *text != '[' && equals != NULL) {
            *equals = '\0';
            key = trim_config_text(text);
            normalize_config_key(key);
            if (live_config_key(key)) {
                if (strcmp(key, "max_clients") == 0) {
                    fprintf(output, "max_clients=%u\n",
                            (unsigned)opt->max_http_clients);
                    wrote_max = true;
                } else if (strcmp(key, "wait_ms") == 0) {
                    fprintf(output, "wait_ms=%d\n", opt->wait_ms);
                    wrote_wait = true;
                } else if (strcmp(key, "timeout_ms") == 0) {
                    fprintf(output, "timeout_ms=%d\n", opt->timeout_ms);
                    wrote_timeout = true;
                } else if (strcmp(key, "missing_key") == 0) {
                    fprintf(output, "missing_key=%s\n",
                            opt->missing_key_policy == MISSING_KEY_DROP ?
                            "drop" : "pass");
                    wrote_missing = true;
                } else if (strcmp(key, "device_channels_update") == 0) {
                    fprintf(output, "device_channels_update=%s\n",
                            opt->device_channels_update ? "true" : "false");
                    wrote_device_channels = true;
                } else if (strcmp(key, "device_epg_update") == 0) {
                    fprintf(output, "device_epg_update=%s\n",
                            opt->device_epg_update ? "true" : "false");
                    wrote_device_epg = true;
                } else if (strcmp(key, "device_channels_refresh_minutes") == 0) {
                    fprintf(output, "device_channels_refresh_minutes=%u\n",
                            (unsigned)opt->device_channels_refresh_minutes);
                    wrote_device_channels_minutes = true;
                } else if (strcmp(key, "device_epg_refresh_minutes") == 0) {
                    fprintf(output, "device_epg_refresh_minutes=%u\n",
                            (unsigned)opt->device_epg_refresh_minutes);
                    wrote_device_epg_minutes = true;
                } else if (strcmp(key, "device_scan_refresh_minutes") == 0) {
                    fprintf(output, "device_scan_refresh_minutes=%u\n",
                            (unsigned)opt->device_scan_refresh_minutes);
                    wrote_device_scan_minutes = true;
                } else if (strcmp(key, "device_scan_timeout_minutes") == 0) {
                    fprintf(output, "device_scan_timeout_minutes=%u\n",
                            (unsigned)opt->device_scan_timeout_minutes);
                    wrote_device_scan_timeout = true;
                } else if (strcmp(key, "device_scan_search_range") == 0) {
                    fprintf(output, "device_scan_search_range=%u\n",
                            (unsigned)opt->device_scan_search_range);
                    wrote_device_scan_range = true;
                } else if (strcmp(key, "device_scan_order_by") == 0) {
                    fprintf(output, "device_scan_order_by=%u\n",
                            (unsigned)opt->device_scan_order_by);
                    wrote_device_scan_order = true;
                } else if (strcmp(key, "device_scan_sort_mode") == 0) {
                    fprintf(output, "device_scan_sort_mode=%u\n",
                            (unsigned)opt->device_scan_sort_mode);
                    wrote_device_scan_sort = true;
                } else if (strcmp(key, "device_scan_mode") == 0) {
                    fprintf(output, "device_scan_mode=%u\n",
                            (unsigned)opt->device_scan_mode);
                    wrote_device_scan_mode = true;
                } else if (strcmp(key, "device_scan_network") == 0) {
                    fprintf(output, "device_scan_network=%s\n",
                            opt->device_scan_network ? "true" : "false");
                    wrote_device_scan_network = true;
                } else if (strcmp(key, "device_scan_epg_after") == 0) {
                    fprintf(output, "device_scan_epg_after=%s\n",
                            opt->device_scan_epg_after ? "true" : "false");
                    wrote_device_scan_epg = true;
                } else if (strcmp(key, "device_scan_apply_satellite") == 0) {
                    fprintf(output, "device_scan_apply_satellite=%s\n",
                            opt->device_scan_apply_satellite ? "true" : "false");
                    wrote_device_scan_apply_satellite = true;
                } else if (strcmp(key, "orbital") == 0) {
                    fprintf(output, "orbital=%u\n",
                            (unsigned)opt->orbital_tenths);
                    wrote_orbital = true;
                } else if (strcmp(key, "west") == 0) {
                    fprintf(output, "west=%s\n", opt->west ? "true" : "false");
                    wrote_west = true;
                } else if (strcmp(key, "tone") == 0) {
                    fprintf(output, "tone=%s\n",
                            opt->tone == TONE_ON ? "on" :
                            (opt->tone == TONE_OFF ? "off" : "auto"));
                    wrote_tone = true;
                } else if (strcmp(key, "lnb_low") == 0) {
                    fprintf(output, "lnb_low=%u\n", (unsigned)opt->lnb_low_mhz);
                    wrote_lnb_low = true;
                } else if (strcmp(key, "lnb_high") == 0) {
                    fprintf(output, "lnb_high=%u\n", (unsigned)opt->lnb_high_mhz);
                    wrote_lnb_high = true;
                } else if (strcmp(key, "lnb_switch") == 0) {
                    fprintf(output, "lnb_switch=%u\n",
                            (unsigned)opt->lnb_switch_mhz);
                    wrote_lnb_switch = true;
                } else if (strcmp(key, "diseqc") == 0) {
                    fprintf(output, "diseqc=%u\n", (unsigned)opt->diseqc_port);
                    wrote_diseqc = true;
                } else {
                    fprintf(output, "device_update_on_start=%s\n",
                            opt->device_update_on_start ? "true" : "false");
                    wrote_device_start = true;
                }
                continue;
            }
        }
        fputs(line, output);
    }
    if (!wrote_max) {
        fprintf(output, "max_clients=%u\n", (unsigned)opt->max_http_clients);
    }
    if (!wrote_wait) {
        fprintf(output, "wait_ms=%d\n", opt->wait_ms);
    }
    if (!wrote_timeout) {
        fprintf(output, "timeout_ms=%d\n", opt->timeout_ms);
    }
    if (!wrote_missing) {
        fprintf(output, "missing_key=%s\n",
                opt->missing_key_policy == MISSING_KEY_DROP ? "drop" : "pass");
    }
    if (!wrote_device_channels)
        fprintf(output, "device_channels_update=%s\n",
                opt->device_channels_update ? "true" : "false");
    if (!wrote_device_epg)
        fprintf(output, "device_epg_update=%s\n",
                opt->device_epg_update ? "true" : "false");
    if (!wrote_device_channels_minutes)
        fprintf(output, "device_channels_refresh_minutes=%u\n",
                (unsigned)opt->device_channels_refresh_minutes);
    if (!wrote_device_epg_minutes)
        fprintf(output, "device_epg_refresh_minutes=%u\n",
                (unsigned)opt->device_epg_refresh_minutes);
    if (!wrote_device_scan_minutes)
        fprintf(output, "device_scan_refresh_minutes=%u\n",
                (unsigned)opt->device_scan_refresh_minutes);
    if (!wrote_device_scan_timeout)
        fprintf(output, "device_scan_timeout_minutes=%u\n",
                (unsigned)opt->device_scan_timeout_minutes);
    if (!wrote_device_scan_range)
        fprintf(output, "device_scan_search_range=%u\n",
                (unsigned)opt->device_scan_search_range);
    if (!wrote_device_scan_order)
        fprintf(output, "device_scan_order_by=%u\n",
                (unsigned)opt->device_scan_order_by);
    if (!wrote_device_scan_sort)
        fprintf(output, "device_scan_sort_mode=%u\n",
                (unsigned)opt->device_scan_sort_mode);
    if (!wrote_device_scan_mode)
        fprintf(output, "device_scan_mode=%u\n",
                (unsigned)opt->device_scan_mode);
    if (!wrote_device_scan_network)
        fprintf(output, "device_scan_network=%s\n",
                opt->device_scan_network ? "true" : "false");
    if (!wrote_device_scan_epg)
        fprintf(output, "device_scan_epg_after=%s\n",
                opt->device_scan_epg_after ? "true" : "false");
    if (!wrote_device_scan_apply_satellite)
        fprintf(output, "device_scan_apply_satellite=%s\n",
                opt->device_scan_apply_satellite ? "true" : "false");
    if (!wrote_orbital)
        fprintf(output, "orbital=%u\n", (unsigned)opt->orbital_tenths);
    if (!wrote_west)
        fprintf(output, "west=%s\n", opt->west ? "true" : "false");
    if (!wrote_tone)
        fprintf(output, "tone=%s\n",
                opt->tone == TONE_ON ? "on" :
                (opt->tone == TONE_OFF ? "off" : "auto"));
    if (!wrote_lnb_low)
        fprintf(output, "lnb_low=%u\n", (unsigned)opt->lnb_low_mhz);
    if (!wrote_lnb_high)
        fprintf(output, "lnb_high=%u\n", (unsigned)opt->lnb_high_mhz);
    if (!wrote_lnb_switch)
        fprintf(output, "lnb_switch=%u\n", (unsigned)opt->lnb_switch_mhz);
    if (!wrote_diseqc)
        fprintf(output, "diseqc=%u\n", (unsigned)opt->diseqc_port);
    if (!wrote_device_start)
        fprintf(output, "device_update_on_start=%s\n",
                opt->device_update_on_start ? "true" : "false");

    if (ferror(input) || fflush(output) != 0 || fclose(output) != 0) {
        fclose(input);
        remove(temporary);
        return false;
    }
    fclose(input);
#ifdef _WIN32
    remove(opt->config_path);
#endif
    if (rename(temporary, opt->config_path) != 0) {
        remove(temporary);
        return false;
    }
    return true;
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
    for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
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
    for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
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
    for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
        http_connection_close(&stream->clients[i]);
    }
}


static bool http_stream_kick_client(http_stream_t *stream,
                                    uint64_t client_id,
                                    char *peer,
                                    size_t peer_capacity)
{
    size_t i;
    for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
        http_connection_t *client = &stream->clients[i];
        if (client->socket != SOCKET_INVALID && client->id == client_id) {
            if (peer != NULL && peer_capacity > 0U) {
                if (client->peer_port[0] != '\0') {
                    snprintf(peer, peer_capacity, "%s:%s",
                             client->peer_host, client->peer_port);
                } else {
                    snprintf(peer, peer_capacity, "%s", client->peer_host);
                }
            }
            http_connection_close(client);
            return true;
        }
    }
    return false;
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
    for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
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
    stream->total_bytes_forwarded += forwarded;
    ++stream->blocks;

    if (monotonic_seconds() - stream->report_start >= 2.0) {
        const double now = monotonic_seconds();
        const double elapsed = now - stream->report_start;
        const double mbps = elapsed > 0.0
            ? (double)stream->bytes_forwarded * 8.0 / elapsed / 1000000.0
            : 0.0;
        stream->last_mbps = mbps;
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

static bool string_buffer_append_json_string(string_buffer_t *buffer,
                                             const char *text)
{
    const size_t input_length = strlen(text != NULL ? text : "");
    const size_t capacity = input_length > (SIZE_MAX - 16U) / 2U ?
                            0U : input_length * 2U + 16U;
    char *escaped;
    bool result;
    if (capacity == 0U) {
        return false;
    }
    escaped = (char *)malloc(capacity);
    if (escaped == NULL) {
        return false;
    }
    json_escape(text != NULL ? text : "", escaped, capacity);
    result = string_buffer_append(buffer, "\"") &&
             string_buffer_append(buffer, escaped) &&
             string_buffer_append(buffer, "\"");
    free(escaped);
    return result;
}

static bool append_epg_summary_json(string_buffer_t *buffer,
                                    const epg_program_t *program)
{
    if (program == NULL) {
        return string_buffer_append(buffer, "null");
    }
    if (!string_buffer_appendf(buffer,
            "{\"start\":%lld,\"stop\":%lld,\"title\":",
            (long long)program->start_utc, (long long)program->stop_utc) ||
        !string_buffer_append_json_string(buffer, program->title) ||
        !string_buffer_append(buffer, ",\"subtitle\":") ||
        !string_buffer_append_json_string(buffer, program->subtitle) ||
        !string_buffer_append(buffer, ",\"category\":") ||
        !string_buffer_append_json_string(buffer, program->category) ||
        !string_buffer_append(buffer, "}")) {
        return false;
    }
    return true;
}

static void serve_admin_api_status(http_connection_t *connection,
                                   const http_stream_t *stream,
                                   const channel_list_t *channels,
                                   const epg_list_t *epg,
                                   const options_t *opt,
                                   const device_update_state_t *updates,
                                   bool head_only)
{
    string_buffer_t body;
    size_t i;
    const int64_t now_utc = (int64_t)time(NULL);
    const double monotonic_now = monotonic_seconds();
    const uint64_t bad_packets = stream->bad_blocks +
        stream->stats.invalid_packets + stream->stats.reserved_scrambling;

    memset(&body, 0, sizeof(body));
    if (!string_buffer_appendf(&body,
        "{\"server_time\":%lld,\"uptime_seconds\":%llu,"
        "\"online\":true,\"streaming\":%s,\"clients\":%zu,"
        "\"max_clients\":%zu,\"data_rate_mbps\":%.3f,"
        "\"total_bytes\":%llu,\"blocks\":%llu,\"timeouts\":%llu,"
        "\"bad_blocks\":%llu,\"signal_metrics_available\":false,"
        "\"current_channel\":",
        (long long)now_utc,
        (unsigned long long)(monotonic_now > stream->server_started ?
            monotonic_now - stream->server_started : 0.0),
        stream->channel != NULL ? "true" : "false",
        http_stream_client_count(stream), stream->max_clients,
        stream->last_mbps,
        (unsigned long long)stream->total_bytes_forwarded,
        (unsigned long long)stream->blocks,
        (unsigned long long)stream->timeouts,
        (unsigned long long)bad_packets)) {
        goto overflow;
    }

    if (stream->channel == NULL) {
        if (!string_buffer_append(&body, "null")) {
            goto overflow;
        }
    } else {
        if (!string_buffer_appendf(&body,
            "{\"lcn\":%u,\"name\":",
            (unsigned)stream->channel->lcn) ||
            !string_buffer_append_json_string(&body, stream->channel->name) ||
            !string_buffer_appendf(&body,
            ",\"type\":\"%s\",\"frequency_mhz\":%u,"
            "\"symbol_rate_ks\":%u,\"polarization\":\"%c\","
            "\"service_id\":%u}",
            stream->channel->type,
            (unsigned)stream->channel->frequency_mhz,
            (unsigned)stream->channel->symbol_rate_ks,
            stream->channel->polarization,
            (unsigned)stream->channel->service_id)) {
            goto overflow;
        }
    }

    if (!string_buffer_append(&body, ",\"channels\":[")) {
        goto overflow;
    }
    for (i = 0; i < channels->count; ++i) {
        const channel_t *channel = &channels->items[i];
        const epg_program_t *current;
        const epg_program_t *next;
        find_epg_now_next(epg, channel->epg_id, now_utc, &current, &next);
        if (i > 0U && !string_buffer_append(&body, ",")) {
            goto overflow;
        }
        if (!string_buffer_appendf(&body, "{\"lcn\":%u,\"name\":",
                                   (unsigned)channel->lcn) ||
            !string_buffer_append_json_string(&body, channel->name) ||
            !string_buffer_append(&body, ",\"type\":") ||
            !string_buffer_append_json_string(&body, channel->type) ||
            !string_buffer_appendf(&body,
                ",\"service_id\":%u,\"frequency_mhz\":%u,"
                "\"symbol_rate_ks\":%u,\"polarization\":\"%c\","
                "\"prog_idx\":%u,\"ts_id\":%u,"
                "\"fta\":%s,\"epg_id\":",
                (unsigned)channel->service_id,
                (unsigned)channel->frequency_mhz,
                (unsigned)channel->symbol_rate_ks,
                channel->polarization,
                (unsigned)channel->program_index,
                (unsigned)channel->transport_stream_id,
                channel->fta ? "true" : "false") ||
            !string_buffer_append_json_string(&body, channel->epg_id) ||
            !string_buffer_appendf(&body, ",\"stream_url\":\"/channel/%u\",\"now\":",
                                   (unsigned)channel->lcn) ||
            !append_epg_summary_json(&body, current) ||
            !string_buffer_append(&body, ",\"next\":") ||
            !append_epg_summary_json(&body, next) ||
            !string_buffer_append(&body, "}")) {
            goto overflow;
        }
    }

    if (!string_buffer_append(&body, "],\"viewers\":[")) {
        goto overflow;
    }
    {
        bool first = true;
        for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
            const http_connection_t *client = &stream->clients[i];
            const uint64_t connected_seconds =
                client->socket != SOCKET_INVALID &&
                monotonic_now > client->connected_at ?
                (uint64_t)(monotonic_now - client->connected_at) : 0U;
            if (client->socket == SOCKET_INVALID) {
                continue;
            }
            if (!first && !string_buffer_append(&body, ",")) {
                goto overflow;
            }
            first = false;
            if (!string_buffer_appendf(&body, "{\"id\":%llu,\"host\":",
                                       (unsigned long long)client->id) ||
                !string_buffer_append_json_string(&body, client->peer_host) ||
                !string_buffer_append(&body, ",\"port\":") ||
                !string_buffer_append_json_string(&body, client->peer_port) ||
                !string_buffer_appendf(&body,
                    ",\"connected_seconds\":%llu}",
                    (unsigned long long)connected_seconds)) {
                goto overflow;
            }
        }
    }

    if (!string_buffer_appendf(&body,
        "],\"config\":{\"wait_ms\":%d,\"timeout_ms\":%d,"
        "\"missing_key\":\"%s\",\"config_active\":%s,"
        "\"viewer_auth\":%s,\"admin_auth\":%s,\"channels_path\":",
        opt->wait_ms, opt->timeout_ms,
        opt->missing_key_policy == MISSING_KEY_DROP ? "drop" : "pass",
        opt->config_path != NULL ? "true" : "false",
        opt->http_user != NULL ? "true" : "false",
        opt->admin_user != NULL ? "true" : "false") ||
        !string_buffer_append_json_string(&body, opt->channels_path) ||
        !string_buffer_append(&body, ",\"config_path\":") ||
        !string_buffer_append_json_string(&body,
            opt->config_path != NULL ? opt->config_path : "") ||
        !string_buffer_append(&body, ",\"web_root\":") ||
        !string_buffer_append_json_string(&body,
            opt->web_root != NULL ? opt->web_root : "") ||
        !string_buffer_append(&body, ",\"epg_path\":") ||
        !string_buffer_append_json_string(&body,
            opt->epg_path != NULL ? opt->epg_path : "") ||
        !string_buffer_appendf(&body,
            ",\"epg_update\":%s,\"epg_update_timeout_ms\":%d,\"epg_url\":",
            opt->epg_update ? "true" : "false",
            opt->epg_update_timeout_ms) ||
        !string_buffer_append_json_string(&body,
            opt->epg_url != NULL ? opt->epg_url : "") ||
        !string_buffer_appendf(&body,
            ",\"device_update_on_start\":%s,"
            "\"device_scan_network\":%s,"
            "\"device_scan_epg_after\":%s,"
            "\"device_scan_apply_satellite\":%s,"
            "\"device_scan_timeout_minutes\":%u,"
            "\"device_scan_search_range\":%u,"
            "\"device_scan_order_by\":%u,"
            "\"device_scan_sort_mode\":%u,"
            "\"device_scan_mode\":%u,"
            "\"orbital_tenths\":%u,"
            "\"west\":%s,"
            "\"tone\":\"%s\","
            "\"lnb_low_mhz\":%u,"
            "\"lnb_high_mhz\":%u,"
            "\"lnb_switch_mhz\":%u,"
            "\"diseqc_port\":%u",
            opt->device_update_on_start ? "true" : "false",
            opt->device_scan_network ? "true" : "false",
            opt->device_scan_epg_after ? "true" : "false",
            opt->device_scan_apply_satellite ? "true" : "false",
            (unsigned)opt->device_scan_timeout_minutes,
            (unsigned)opt->device_scan_search_range,
            (unsigned)opt->device_scan_order_by,
            (unsigned)opt->device_scan_sort_mode,
            (unsigned)opt->device_scan_mode,
            (unsigned)opt->orbital_tenths,
            opt->west ? "true" : "false",
            opt->tone == TONE_ON ? "on" :
            (opt->tone == TONE_OFF ? "off" : "auto"),
            (unsigned)opt->lnb_low_mhz,
            (unsigned)opt->lnb_high_mhz,
            (unsigned)opt->lnb_switch_mhz,
            (unsigned)opt->diseqc_port) ||
        !string_buffer_appendf(&body,
            "},\"epg\":{\"programmes\":%zu,\"loaded_utc\":%lld},"
            "\"device_updates\":{\"busy\":%s,"
            "\"channels_enabled\":%s,\"epg_enabled\":%s,"
            "\"channels_refresh_minutes\":%u,\"epg_refresh_minutes\":%u,"
            "\"last_channels_attempt_utc\":%lld,\"last_channels_success_utc\":%lld,"
            "\"last_epg_attempt_utc\":%lld,\"last_epg_success_utc\":%lld,"
            "\"epg_updated_channels\":%zu,\"epg_skipped_channels\":%zu,"
            "\"epg_failed_channels\":%zu,"
            "\"scan_running\":%s,\"scan_progress\":%u,"
            "\"scan_state\":%u,\"scan_frequency_mhz\":%u,"
            "\"scan_symbol_rate_ks\":%u,\"scan_mode\":%u,"
            "\"scan_tv_count\":%u,\"scan_radio_count\":%u,"
            "\"scan_refresh_minutes\":%u,"
            "\"last_scan_attempt_utc\":%lld,"
            "\"last_scan_success_utc\":%lld,\"last_action\":",
            epg->count, (long long)epg->loaded_utc,
            updates->busy ? "true" : "false",
            opt->device_channels_update ? "true" : "false",
            opt->device_epg_update ? "true" : "false",
            (unsigned)opt->device_channels_refresh_minutes,
            (unsigned)opt->device_epg_refresh_minutes,
            (long long)updates->last_channels_attempt_utc,
            (long long)updates->last_channels_success_utc,
            (long long)updates->last_epg_attempt_utc,
            (long long)updates->last_epg_success_utc,
            updates->last_epg_updated_channels,
            updates->last_epg_skipped_channels,
            updates->last_epg_failed_channels,
            updates->scan_running ? "true" : "false",
            updates->scan_progress,
            updates->scan_state,
            (unsigned)updates->scan_frequency_mhz,
            (unsigned)updates->scan_symbol_rate_ks,
            updates->scan_mode,
            updates->scan_tv_count,
            updates->scan_radio_count,
            (unsigned)opt->device_scan_refresh_minutes,
            (long long)updates->last_scan_attempt_utc,
            (long long)updates->last_scan_success_utc) ||
        !string_buffer_append_json_string(&body, updates->last_action) ||
        !string_buffer_append(&body, ",\"last_message\":") ||
        !string_buffer_append_json_string(&body, updates->last_message) ||
        !string_buffer_append(&body, "}}\n")) {
        goto overflow;
    }

    send_http_response(connection, 200, "OK",
                       "application/json; charset=utf-8",
                       "X-Content-Type-Options: nosniff\r\n",
                       body.data != NULL ? body.data : "{}", head_only);
    string_buffer_free(&body);
    return;

overflow:
    string_buffer_free(&body);
    send_http_error(connection, 500, "Internal Server Error",
                    "Could not build dashboard status.\n", head_only);
}

static void serve_admin_epg_api(http_connection_t *connection,
                                const channel_list_t *channels,
                                const epg_list_t *epg,
                                uint32_t lcn,
                                bool head_only)
{
    const channel_t *channel = find_channel_by_lcn(channels, lcn);
    string_buffer_t body;
    const int64_t now_utc = (int64_t)time(NULL);
    size_t i;
    size_t count = 0U;

    if (channel == NULL) {
        send_http_error(connection, 404, "Not Found",
                        "Unknown channel number.\n", head_only);
        return;
    }
    memset(&body, 0, sizeof(body));
    if (!string_buffer_append(&body, "{\"lcn\":") ||
        !string_buffer_appendf(&body, "%u,\"channel\":",
                               (unsigned)channel->lcn) ||
        !string_buffer_append_json_string(&body, channel->name) ||
        !string_buffer_append(&body, ",\"programmes\":[")) {
        goto overflow;
    }
    for (i = 0; i < epg->count && count < 100U; ++i) {
        const epg_program_t *program = &epg->items[i];
        if (strcmp(program->channel_id, channel->epg_id) != 0 ||
            program->stop_utc < now_utc - 21600LL ||
            program->start_utc > now_utc + 172800LL) {
            continue;
        }
        if (count > 0U && !string_buffer_append(&body, ",")) {
            goto overflow;
        }
        if (!string_buffer_appendf(&body,
                "{\"start\":%lld,\"stop\":%lld,\"title\":",
                (long long)program->start_utc,
                (long long)program->stop_utc) ||
            !string_buffer_append_json_string(&body, program->title) ||
            !string_buffer_append(&body, ",\"subtitle\":") ||
            !string_buffer_append_json_string(&body, program->subtitle) ||
            !string_buffer_append(&body, ",\"description\":") ||
            !string_buffer_append_json_string(&body, program->description) ||
            !string_buffer_append(&body, ",\"category\":") ||
            !string_buffer_append_json_string(&body, program->category) ||
            !string_buffer_append(&body, "}")) {
            goto overflow;
        }
        ++count;
    }
    if (!string_buffer_append(&body, "]}\n")) {
        goto overflow;
    }
    send_http_response(connection, 200, "OK",
                       "application/json; charset=utf-8",
                       "X-Content-Type-Options: nosniff\r\n",
                       body.data != NULL ? body.data : "{}", head_only);
    string_buffer_free(&body);
    return;

overflow:
    string_buffer_free(&body);
    send_http_error(connection, 500, "Internal Server Error",
                    "Could not build EPG response.\n", head_only);
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

static tune_result_t recover_http_control(socket_t *control,
                                          const options_t *opt,
                                          const channel_t *channel,
                                          tuning_state_t *tuning)
{
    tune_result_t result = TUNE_RESULT_OK;

    if (!reconnect_control_session(control, opt)) {
        return TUNE_RESULT_CONTROL_ERROR;
    }
    memset(tuning, 0, sizeof(*tuning));
    if (!configure_satellite(*control, opt)) {
        if (*control != SOCKET_INVALID) {
            CLOSESOCKET(*control);
            *control = SOCKET_INVALID;
        }
        return TUNE_RESULT_CONTROL_ERROR;
    }
    if (channel != NULL) {
        result = tune_channel(*control, opt, channel, tuning);
        if (result == TUNE_RESULT_CONTROL_ERROR &&
            *control != SOCKET_INVALID) {
            CLOSESOCKET(*control);
            *control = SOCKET_INVALID;
        }
    }
    return result;
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


static bool parse_u64_decimal(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;
    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

static void serve_admin_page(http_connection_t *connection,
                             const http_stream_t *stream,
                             const channel_list_t *channels,
                             const options_t *opt,
                             const tls_server_t *tls,
                             bool head_only)
{
    (void)stream;
    (void)channels;
    (void)tls;
    serve_web_asset(connection, opt, "index.html",
                    "text/html; charset=utf-8", head_only, false);
}

static void handle_admin_post(http_connection_t *connection,
                              const char *path,
                              const char *request,
                              http_stream_t *stream,
                              channel_list_t *channels,
                              epg_list_t *epg,
                              options_t *opt,
                              socket_t *control,
                              tuning_state_t *tuning,
                              device_update_state_t *updates,
                              const tls_server_t *tls)
{
    const char *body = http_request_body(request);
    if (body == NULL || !admin_post_origin_allowed(request, opt, tls)) {
        send_http_error(connection, 400, "Bad Request",
                        "Invalid administration form request.\n", false);
        return;
    }

    if (strcmp(path, "/admin/kick") == 0) {
        char value[64];
        char peer[256] = "viewer";
        uint64_t id;
        if (!form_get_value(body, "id", value, sizeof(value)) ||
            !parse_u64_decimal(value, &id)) {
            send_http_error(connection, 400, "Bad Request",
                            "A valid viewer ID is required.\n", false);
            return;
        }
        if (!http_stream_kick_client(stream, id, peer, sizeof(peer))) {
            send_http_error(connection, 404, "Not Found",
                            "That viewer is no longer connected.\n", false);
            return;
        }
        printf("Administrator disconnected client %llu (%s).\n",
               (unsigned long long)id, peer);
        send_http_redirect(connection, "/admin/");
        return;
    }

    if (strcmp(path, "/admin/kick-all") == 0) {
        http_stream_close_clients(stream);
        http_stream_stop_if_idle(stream);
        printf("Administrator disconnected all viewers.\n");
        send_http_redirect(connection, "/admin/");
        return;
    }

    if (strcmp(path, "/admin/reload") == 0) {
        channel_list_t replacement;
        epg_list_t epg_replacement;
        if (http_stream_client_count(stream) != 0U) {
            send_http_error(connection, 409, "Conflict",
                            "Disconnect viewers before reloading the channel list.\n",
                            false);
            return;
        }
        if (!load_channel_list(opt->channels_path, &replacement)) {
            send_http_error(connection, 500, "Internal Server Error",
                            "The channel list could not be reloaded.\n", false);
            return;
        }
        if (opt->epg_update && !update_epg_on_start(opt)) {
            free_channel_list(&replacement);
            send_http_error(connection, 502, "Bad Gateway",
                            "The configured EPG download failed.\n", false);
            return;
        }
        if (!load_epg_file(opt->epg_path, &epg_replacement)) {
            free_channel_list(&replacement);
            send_http_error(connection, 500, "Internal Server Error",
                            "The EPG could not be reloaded.\n", false);
            return;
        }
        free_channel_list(channels);
        free_epg_list(epg);
        *channels = replacement;
        *epg = epg_replacement;
        send_http_redirect(connection, "/admin/");
        return;
    }

    if (strcmp(path, "/admin/reload-epg") == 0) {
        epg_list_t replacement;
        if (opt->epg_update && !update_epg_on_start(opt)) {
            send_http_error(connection, 502, "Bad Gateway",
                            "The configured EPG download failed.\n", false);
            return;
        }
        if (!load_epg_file(opt->epg_path, &replacement)) {
            send_http_error(connection, 500, "Internal Server Error",
                            "The EPG could not be reloaded.\n", false);
            return;
        }
        free_epg_list(epg);
        *epg = replacement;
        send_http_redirect(connection, "/admin/");
        return;
    }

    if (strcmp(path, "/admin/device/update-channels") == 0 ||
        strcmp(path, "/admin/device/update-epg") == 0 ||
        strcmp(path, "/admin/device/update-all") == 0) {
        const bool do_channels = strcmp(path, "/admin/device/update-epg") != 0;
        const bool do_epg = strcmp(path, "/admin/device/update-channels") != 0;
        bool ok = true;
        if (http_stream_client_count(stream) != 0U) {
            send_http_error(connection, 409, "Conflict",
                            "Device data can only be refreshed while no viewers are connected.\n",
                            false);
            return;
        }
        http_stream_stop_if_idle(stream);
        if (do_channels)
            ok = device_refresh_channels(control, opt, channels, updates);
        if (ok && do_epg)
            ok = device_refresh_epg(control, opt, channels, epg, updates, true);
        memset(tuning, 0, sizeof(*tuning));
        if (!ok) {
            send_http_error(connection, 502, "Bad Gateway",
                            updates->last_message, false);
            return;
        }
        send_http_redirect(connection, "/admin/");
        return;
    }

    if (strcmp(path, "/admin/device/scan") == 0) {
        if (http_stream_client_count(stream) != 0U) {
            send_http_error(connection, 409, "Conflict",
                            "Disconnect viewers before starting a receiver scan.\\n",
                            false);
            return;
        }
        if (updates->busy || updates->scan_running) {
            send_http_error(connection, 409, "Conflict",
                            "Receiver maintenance is already running.\\n", false);
            return;
        }
        http_stream_stop_if_idle(stream);
        memset(tuning, 0, sizeof(*tuning));
        if (!device_start_scan(control, opt, updates)) {
            send_http_error(connection, 502, "Bad Gateway",
                            updates->last_message, false);
            return;
        }
        updates->next_scan_due = monotonic_seconds() +
            (double)opt->device_scan_refresh_minutes * 60.0;
        send_http_redirect(connection, "/admin/#settings");
        return;
    }

    if (strcmp(path, "/admin/device/scan-cancel") == 0) {
        if (!updates->scan_running) {
            send_http_error(connection, 409, "Conflict",
                            "No receiver scan is running.\\n", false);
            return;
        }
        (void)device_cancel_scan(control, opt, updates);
        memset(tuning, 0, sizeof(*tuning));
        send_http_redirect(connection, "/admin/#settings");
        return;
    }

    if (strcmp(path, "/admin/settings") == 0) {
        char max_text[32];
        char wait_text[32];
        char timeout_text[32];
        char missing_text[32];
        char persist_text[8];
        char device_channels_text[8];
        char device_epg_text[8];
        char device_start_text[8];
        char device_channels_minutes_text[32];
        char device_epg_minutes_text[32];
        char device_scan_minutes_text[32];
        char device_scan_timeout_text[32];
        char device_scan_range_text[32];
        char device_scan_order_text[32];
        char device_scan_sort_text[32];
        char device_scan_mode_text[32];
        char device_scan_network_text[8];
        char device_scan_epg_text[8];
        char device_scan_apply_satellite_text[8];
        char orbital_text[32];
        char west_text[8];
        char tone_text[16];
        char lnb_low_text[32];
        char lnb_high_text[32];
        char lnb_switch_text[32];
        char diseqc_text[32];
        uint32_t max_clients;
        uint32_t wait_ms;
        uint32_t timeout_ms;
        uint32_t device_channels_minutes;
        uint32_t device_epg_minutes;
        uint32_t device_scan_minutes;
        uint32_t device_scan_timeout;
        uint32_t device_scan_range;
        uint32_t device_scan_order;
        uint32_t device_scan_sort;
        uint32_t device_scan_mode;
        uint32_t orbital;
        uint32_t lnb_low;
        uint32_t lnb_high;
        uint32_t lnb_switch;
        uint32_t diseqc;
        const size_t active = http_stream_client_count(stream);
        const bool persist = form_get_value(body, "persist", persist_text,
                                             sizeof(persist_text)) &&
                             strcmp(persist_text, "1") == 0;

        if (!form_get_value(body, "max_clients", max_text, sizeof(max_text)) ||
            !form_get_value(body, "wait_ms", wait_text, sizeof(wait_text)) ||
            !form_get_value(body, "timeout_ms", timeout_text, sizeof(timeout_text)) ||
            !form_get_value(body, "missing_key", missing_text, sizeof(missing_text)) ||
            !form_get_value(body, "device_channels_update", device_channels_text, sizeof(device_channels_text)) ||
            !form_get_value(body, "device_epg_update", device_epg_text, sizeof(device_epg_text)) ||
            !form_get_value(body, "device_update_on_start", device_start_text, sizeof(device_start_text)) ||
            !form_get_value(body, "device_channels_refresh_minutes", device_channels_minutes_text, sizeof(device_channels_minutes_text)) ||
            !form_get_value(body, "device_epg_refresh_minutes", device_epg_minutes_text, sizeof(device_epg_minutes_text)) ||
            !form_get_value(body, "device_scan_refresh_minutes", device_scan_minutes_text, sizeof(device_scan_minutes_text)) ||
            !form_get_value(body, "device_scan_timeout_minutes", device_scan_timeout_text, sizeof(device_scan_timeout_text)) ||
            !form_get_value(body, "device_scan_search_range", device_scan_range_text, sizeof(device_scan_range_text)) ||
            !form_get_value(body, "device_scan_order_by", device_scan_order_text, sizeof(device_scan_order_text)) ||
            !form_get_value(body, "device_scan_sort_mode", device_scan_sort_text, sizeof(device_scan_sort_text)) ||
            !form_get_value(body, "device_scan_mode", device_scan_mode_text, sizeof(device_scan_mode_text)) ||
            !form_get_value(body, "device_scan_network", device_scan_network_text, sizeof(device_scan_network_text)) ||
            !form_get_value(body, "device_scan_epg_after", device_scan_epg_text, sizeof(device_scan_epg_text)) ||
            !form_get_value(body, "device_scan_apply_satellite", device_scan_apply_satellite_text, sizeof(device_scan_apply_satellite_text)) ||
            !form_get_value(body, "orbital_tenths", orbital_text, sizeof(orbital_text)) ||
            !form_get_value(body, "west", west_text, sizeof(west_text)) ||
            !form_get_value(body, "tone", tone_text, sizeof(tone_text)) ||
            !form_get_value(body, "lnb_low_mhz", lnb_low_text, sizeof(lnb_low_text)) ||
            !form_get_value(body, "lnb_high_mhz", lnb_high_text, sizeof(lnb_high_text)) ||
            !form_get_value(body, "lnb_switch_mhz", lnb_switch_text, sizeof(lnb_switch_text)) ||
            !form_get_value(body, "diseqc_port", diseqc_text, sizeof(diseqc_text)) ||
            !parse_u32(max_text, &max_clients) || max_clients == 0U ||
            max_clients > HTTP_CLIENT_LIMIT || max_clients < active ||
            !parse_u32(wait_text, &wait_ms) || wait_ms > 60000U ||
            !parse_u32(timeout_text, &timeout_ms) || timeout_ms < 100U ||
            timeout_ms > 60000U ||
            !parse_u32(device_channels_minutes_text, &device_channels_minutes) ||
            device_channels_minutes > 10080U ||
            !parse_u32(device_epg_minutes_text, &device_epg_minutes) ||
            device_epg_minutes > 10080U ||
            !parse_u32(device_scan_minutes_text, &device_scan_minutes) ||
            device_scan_minutes > 10080U ||
            !parse_u32(device_scan_timeout_text, &device_scan_timeout) ||
            device_scan_timeout < 1U || device_scan_timeout > 120U ||
            !parse_u32(device_scan_range_text, &device_scan_range) ||
            device_scan_range > 7U ||
            !parse_u32(device_scan_order_text, &device_scan_order) ||
            device_scan_order > 3U ||
            !parse_u32(device_scan_sort_text, &device_scan_sort) ||
            device_scan_sort > 3U ||
            !parse_u32(device_scan_mode_text, &device_scan_mode) ||
            !normalize_device_scan_mode(&device_scan_mode) ||
            !parse_u32(orbital_text, &orbital) || orbital > 32767U ||
            !parse_u32(lnb_low_text, &lnb_low) || lnb_low > 65535U ||
            !parse_u32(lnb_high_text, &lnb_high) || lnb_high > 65535U ||
            !parse_u32(lnb_switch_text, &lnb_switch) || lnb_switch > 65535U ||
            !parse_u32(diseqc_text, &diseqc) || diseqc > 255U ||
            (strcmp(device_scan_network_text, "0") != 0 && strcmp(device_scan_network_text, "1") != 0) ||
            (strcmp(device_scan_epg_text, "0") != 0 && strcmp(device_scan_epg_text, "1") != 0) ||
            (strcmp(device_scan_apply_satellite_text, "0") != 0 && strcmp(device_scan_apply_satellite_text, "1") != 0) ||
            (strcmp(west_text, "0") != 0 && strcmp(west_text, "1") != 0) ||
            (strcmp(tone_text, "auto") != 0 && strcmp(tone_text, "on") != 0 && strcmp(tone_text, "off") != 0) ||
            (strcmp(device_channels_text, "0") != 0 && strcmp(device_channels_text, "1") != 0) ||
            (strcmp(device_epg_text, "0") != 0 && strcmp(device_epg_text, "1") != 0) ||
            (strcmp(device_start_text, "0") != 0 && strcmp(device_start_text, "1") != 0) ||
            (strcmp(missing_text, "pass") != 0 &&
             strcmp(missing_text, "drop") != 0)) {
            send_http_error(connection, 400, "Bad Request",
                            "One or more live settings are invalid. The client limit cannot be below the number of connected viewers.\n",
                            false);
            return;
        }

        if (persist && opt->config_path == NULL) {
            send_http_error(connection, 400, "Bad Request",
                            "No --config file is active, so the settings cannot be persisted.\n",
                            false);
            return;
        }

        opt->max_http_clients = max_clients;
        opt->wait_ms = (int)wait_ms;
        opt->timeout_ms = (int)timeout_ms;
        opt->missing_key_policy = strcmp(missing_text, "drop") == 0 ?
                                  MISSING_KEY_DROP : MISSING_KEY_PASS;
        opt->device_channels_update = strcmp(device_channels_text, "1") == 0;
        opt->device_epg_update = strcmp(device_epg_text, "1") == 0;
        opt->device_update_on_start = strcmp(device_start_text, "1") == 0;
        opt->device_channels_refresh_minutes = device_channels_minutes;
        opt->device_epg_refresh_minutes = device_epg_minutes;
        opt->device_scan_refresh_minutes = device_scan_minutes;
        opt->device_scan_timeout_minutes = device_scan_timeout;
        opt->device_scan_search_range = device_scan_range;
        opt->device_scan_order_by = device_scan_order;
        opt->device_scan_sort_mode = device_scan_sort;
        opt->device_scan_mode = device_scan_mode;
        opt->device_scan_network = strcmp(device_scan_network_text, "1") == 0;
        opt->device_scan_epg_after = strcmp(device_scan_epg_text, "1") == 0;
        opt->device_scan_apply_satellite =
            strcmp(device_scan_apply_satellite_text, "1") == 0;
        opt->orbital_tenths = orbital;
        opt->west = strcmp(west_text, "1") == 0;
        opt->tone = strcmp(tone_text, "on") == 0 ? TONE_ON :
                    (strcmp(tone_text, "off") == 0 ? TONE_OFF : TONE_AUTO);
        opt->lnb_low_mhz = lnb_low;
        opt->lnb_high_mhz = lnb_high;
        opt->lnb_switch_mhz = lnb_switch;
        opt->diseqc_port = diseqc;
        updates->next_channels_due = monotonic_seconds() +
            (double)device_channels_minutes * 60.0;
        updates->next_epg_due = monotonic_seconds() +
            (double)device_epg_minutes * 60.0;
        updates->next_scan_due = monotonic_seconds() +
            (double)device_scan_minutes * 60.0;
        stream->max_clients = (size_t)max_clients;
        stream->keys.missing_policy = opt->missing_key_policy;

        if (persist) {
            if (!update_live_config_file(opt)) {
                send_http_error(connection, 500, "Internal Server Error",
                                "The settings were applied in memory but could not be written to the config file.\n",
                                false);
                return;
            }
        }
        send_http_redirect(connection, "/admin/");
        return;
    }

    send_http_error(connection, 404, "Not Found",
                    "Unknown administration action.\n", false);
}

static int run_http_server(socket_t *control, options_t *opt)
{
    channel_list_t channels;
    epg_list_t epg;
    tuning_state_t tuning;
    socket_t listener = SOCKET_INVALID;
    endpoint_t bound_endpoint;
    tls_server_t tls;
    http_stream_t stream;
    device_update_state_t updates;
    double last_heartbeat = monotonic_seconds();
    uint64_t next_connection_id = 1U;
    int result = EXIT_SUCCESS;
    size_t i;

    memset(&tuning, 0, sizeof(tuning));
    memset(&epg, 0, sizeof(epg));
    memset(&stream, 0, sizeof(stream));
    memset(&updates, 0, sizeof(updates));
    snprintf(updates.last_message, sizeof(updates.last_message), "No device update has run yet");
    stream.dongle_udp = SOCKET_INVALID;
    stream.max_clients = (size_t)opt->max_http_clients;
    stream.report_start = monotonic_seconds();
    stream.server_started = stream.report_start;
    for (i = 0; i < HTTP_CLIENT_LIMIT; ++i) {
        http_connection_init(&stream.clients[i]);
    }
    stream_keys_init(&stream.keys, opt);

    memset(&channels, 0, sizeof(channels));
    if (!load_channel_list(opt->channels_path, &channels)) {
        if (!(opt->device_channels_update && opt->device_update_on_start) ||
            !device_refresh_channels(control, opt, &channels, &updates)) {
            device_update_state_free(&updates);
            return EXIT_FAILURE;
        }
    } else if (opt->device_channels_update && opt->device_update_on_start) {
        if (!device_refresh_channels(control, opt, &channels, &updates)) {
            fprintf(stderr,
                    "Warning: native channel update failed during startup; "
                    "continuing with the existing local channel list.\n");
        }
    }
    if (opt->epg_update && !update_epg_on_start(opt)) {
        fprintf(stderr,
                "Warning: startup EPG update did not complete; continuing with the existing local file.\n");
    }
    if (!load_epg_file(opt->epg_path, &epg)) {
        free_epg_list(&epg);
        free_channel_list(&channels);
        device_update_state_free(&updates);
        return EXIT_FAILURE;
    }
    if (opt->device_epg_update && opt->device_update_on_start &&
        !device_refresh_epg(control, opt, &channels, &epg, &updates, true)) {
        fprintf(stderr, "Warning: native EPG update failed during startup.\n");
    }
    updates.next_channels_due = monotonic_seconds() +
        (double)opt->device_channels_refresh_minutes * 60.0;
    updates.next_epg_due = monotonic_seconds() +
        (double)opt->device_epg_refresh_minutes * 60.0;
    updates.next_scan_due = monotonic_seconds() +
        (double)opt->device_scan_refresh_minutes * 60.0;

    memset(&tuning, 0, sizeof(tuning));

    if (*control == SOCKET_INVALID &&
        !reconnect_control_session(control, opt)) {
        fprintf(stderr,
                "Could not restore device control after startup maintenance.\n");
        free_epg_list(&epg);
        free_channel_list(&channels);
        device_update_state_free(&updates);
        return EXIT_FAILURE;
    }

    if (!resolve_endpoint(opt->device_ip, opt->stream_port,
                          SOCK_DGRAM, false, &stream.dongle_endpoint)) {
        free_epg_list(&epg);
        free_channel_list(&channels);
        device_update_state_free(&updates);
        return EXIT_FAILURE;
    }
    if (!configure_satellite(*control, opt)) {
        free_epg_list(&epg);
        free_channel_list(&channels);
        device_update_state_free(&updates);
        return EXIT_FAILURE;
    }
    if (!tls_server_init(&tls, opt)) {
        free_epg_list(&epg);
        free_channel_list(&channels);
        device_update_state_free(&updates);
        return EXIT_FAILURE;
    }

    listener = create_http_listener(opt->http_bind_ip, opt->http_port,
                                    &bound_endpoint);
    if (listener == SOCKET_INVALID) {
        tls_server_cleanup(&tls);
        free_epg_list(&epg);
        free_channel_list(&channels);
        device_update_state_free(&updates);
        return EXIT_FAILURE;
    }

    if (opt->web_ui && !endpoint_is_loopback(&bound_endpoint) &&
        opt->admin_user == NULL && opt->http_user == NULL) {
        fprintf(stderr,
                "The administration Web UI may not be exposed beyond loopback "
                "without --admin-user/--admin-password or viewer HTTP credentials.\n");
        CLOSESOCKET(listener);
        tls_server_cleanup(&tls);
        free_epg_list(&epg);
        free_channel_list(&channels);
        device_update_state_free(&updates);
        return EXIT_FAILURE;
    }

    printf("\nSORALink VLC channel server is ready.\n");
    printf("Open this URL in VLC:\n");
    print_server_url(opt, &tls, "/playlist.m3u");
    printf("Other endpoints:\n");
    print_server_url(opt, &tls, "/tv.m3u");
    print_server_url(opt, &tls, "/radio.m3u");
    print_server_url(opt, &tls, "/status.json");
    if (opt->web_ui) {
        printf("Administration Web UI:\n");
        print_server_url(opt, &tls, "/admin/");
        printf("Web assets: %s\n", opt->web_root);
        printf("EPG source: %s (%zu programmes)\n",
               opt->epg_path != NULL ? opt->epg_path : "disabled",
               epg.count);
        if (opt->epg_update) {
            printf("EPG startup update: enabled (%s, timeout %d ms)\n",
                   opt->epg_url != NULL ? opt->epg_url : "no URL",
                   opt->epg_update_timeout_ms);
        }
    }
    printf("Up to %u clients may share the same tuned channel.\n",
           (unsigned)opt->max_http_clients);
    if (opt->http_user != NULL) {
        printf("Viewer HTTP Basic authentication is enabled.\n");
    }
    if (opt->admin_user != NULL) {
        printf("Dedicated administrator credentials are enabled.\n");
    } else if (opt->web_ui && opt->http_user != NULL) {
        printf("The Web UI uses the viewer HTTP credentials.\n");
    }
    if (opt->config_path != NULL) {
        printf("Configuration loaded from %s; command-line values took precedence.\n",
               opt->config_path);
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
                if (recover_http_control(control, opt, stream.channel, &tuning) !=
                    TUNE_RESULT_OK) {
                    result = EXIT_FAILURE;
                    break;
                }
            }
            last_heartbeat = monotonic_seconds();
        }

        if (updates.scan_running) {
            if (!device_scan_step(control, opt, &channels, &epg,
                                  &updates, &tuning)) {
                fprintf(stderr, "Receiver scan failed: %s\n",
                        updates.last_message);
            }
            last_heartbeat = monotonic_seconds();
        }

        if (active_clients == 0U && !updates.busy) {
            bool scan_due = opt->device_scan_refresh_minutes > 0U &&
                now >= updates.next_scan_due;
            bool channel_due = opt->device_channels_update &&
                opt->device_channels_refresh_minutes > 0U &&
                now >= updates.next_channels_due;
            bool epg_due = opt->device_epg_update &&
                opt->device_epg_refresh_minutes > 0U &&
                now >= updates.next_epg_due;
            if (scan_due) {
                http_stream_stop_if_idle(&stream);
                memset(&tuning, 0, sizeof(tuning));
                if (!device_start_scan(control, opt, &updates)) {
                    fprintf(stderr, "Scheduled receiver scan failed: %s\n",
                            updates.last_message);
                    updates.next_scan_due = monotonic_seconds() +
                        (double)opt->device_scan_refresh_minutes * 60.0;
                }
                last_heartbeat = monotonic_seconds();
            }
            if (!updates.busy && channel_due) {
                http_stream_stop_if_idle(&stream);
                if (!device_refresh_channels(control, opt, &channels, &updates))
                    fprintf(stderr, "Scheduled channel update failed: %s\n",
                            updates.last_message);
                memset(&tuning, 0, sizeof(tuning));
                updates.next_channels_due = monotonic_seconds() +
                    (double)opt->device_channels_refresh_minutes * 60.0;
                if (opt->device_epg_update) epg_due = true;
                last_heartbeat = monotonic_seconds();
            }
            if (!updates.busy && epg_due) {
                if (!device_refresh_epg(control, opt, &channels, &epg,
                                        &updates, false))
                    fprintf(stderr, "Scheduled EPG update failed: %s\n",
                            updates.last_message);
                memset(&tuning, 0, sizeof(tuning));
                updates.next_epg_due = monotonic_seconds() +
                    (double)opt->device_epg_refresh_minutes * 60.0;
                last_heartbeat = monotonic_seconds();
            }
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
            const uint64_t connection_id = next_connection_id++;
            if (next_connection_id == 0U) {
                next_connection_id = 1U;
            }

            if (http_accept_connection(listener, &tls, opt->timeout_ms,
                                       connection_id, &connection)) {
                if (!read_http_request(&connection, request, sizeof(request)) ||
                    !parse_http_request_line(request, &request_line)) {
                    send_http_error(&connection, 400, "Bad Request",
                                    "Bad or oversized HTTP request.\n", false);
                } else {
                    char *query = strchr(request_line.path, '?');
                    bool admin_path;
                    if (query != NULL) {
                        *query = '\0';
                    }
                    admin_path = strcmp(request_line.path, "/admin") == 0 ||
                                 strncmp(request_line.path, "/admin/", 7) == 0;
                    head_only = request_line.method == HTTP_METHOD_HEAD;

                    if (admin_path) {
                        if (!opt->web_ui) {
                            send_http_error(&connection, 404, "Not Found",
                                            "The administration Web UI is disabled.\n",
                                            head_only);
                        } else if (!http_admin_authorized(request, opt)) {
                            send_http_unauthorized(&connection,
                                                   "SORALink Administration",
                                                   head_only);
                        } else if (request_line.method == HTTP_METHOD_OPTIONS) {
                            send_http_response(
                                &connection, 204, "No Content", "text/plain",
                                "Allow: GET, HEAD, POST, OPTIONS\r\n", "", true);
                        } else if (request_line.method == HTTP_METHOD_GET ||
                                   request_line.method == HTTP_METHOD_HEAD) {
                            if (strcmp(request_line.path, "/admin") == 0 ||
                                strcmp(request_line.path, "/admin/") == 0) {
                                serve_admin_page(&connection, &stream, &channels,
                                                 opt, &tls, head_only);
                            } else if (strcmp(request_line.path,
                                              "/admin/assets/styles.css") == 0) {
                                serve_web_asset(&connection, opt, "styles.css",
                                                "text/css; charset=utf-8",
                                                head_only, false);
                            } else if (strcmp(request_line.path,
                                              "/admin/assets/app.js") == 0) {
                                serve_web_asset(&connection, opt, "app.js",
                                                "text/javascript; charset=utf-8",
                                                head_only, false);
                            } else if (strcmp(request_line.path,
                                              "/admin/assets/soralink-mascot.png") == 0) {
                                serve_web_asset(&connection, opt,
                                                "soralink-mascot.png",
                                                "image/png", head_only, true);
                            } else if (strcmp(request_line.path,
                                              "/admin/assets/soralink-logo.png") == 0) {
                                serve_web_asset(&connection, opt,
                                                "soralink-logo.png",
                                                "image/png", head_only, true);
                            } else if (strcmp(request_line.path,
                                              "/admin/assets/soralink-avatar.png") == 0) {
                                serve_web_asset(&connection, opt,
                                                "soralink-avatar.png",
                                                "image/png", head_only, true);
                            } else if (strcmp(request_line.path,
                                              "/admin/assets/soralink-satellite.png") == 0) {
                                serve_web_asset(&connection, opt,
                                                "soralink-satellite.png",
                                                "image/png", head_only, true);
                            } else if (strcmp(request_line.path,
                                              "/admin/api/status") == 0) {
                                serve_admin_api_status(&connection, &stream,
                                                       &channels, &epg, opt,
                                                       &updates, head_only);
                            } else if (strncmp(request_line.path,
                                               "/admin/api/epg/", 15) == 0) {
                                uint32_t lcn;
                                if (!parse_u32(request_line.path + 15, &lcn)) {
                                    send_http_error(&connection, 400,
                                                    "Bad Request",
                                                    "A numeric channel number is required.\n",
                                                    head_only);
                                } else {
                                    serve_admin_epg_api(&connection, &channels,
                                                        &epg, lcn, head_only);
                                }
                            } else {
                                send_http_error(&connection, 404, "Not Found",
                                                "Unknown administration resource.\n",
                                                head_only);
                            }
                        } else if (request_line.method == HTTP_METHOD_POST) {
                            handle_admin_post(&connection, request_line.path,
                                              request, &stream, &channels,
                                              &epg, opt, control, &tuning,
                                              &updates, &tls);
                        } else {
                            send_http_response(
                                &connection, 405, "Method Not Allowed",
                                "text/plain; charset=utf-8",
                                "Allow: GET, HEAD, POST, OPTIONS\r\n",
                                "Use GET for the dashboard and POST for administration actions.\n",
                                false);
                        }
                    } else if (!http_viewer_authorized(request, opt)) {
                        send_http_unauthorized(&connection, "SORALink",
                                               head_only);
                    } else if (request_line.method == HTTP_METHOD_OPTIONS) {
                        send_http_response(
                            &connection, 204, "No Content", "text/plain",
                            "Allow: GET, HEAD, OPTIONS\r\n", "", true);
                    } else if (request_line.method == HTTP_METHOD_POST ||
                               request_line.method == HTTP_METHOD_OTHER) {
                        send_http_response(
                            &connection, 405, "Method Not Allowed",
                            "text/plain; charset=utf-8",
                            "Allow: GET, HEAD, OPTIONS\r\n",
                            "Only GET, HEAD, and OPTIONS are supported on media endpoints.\n",
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
                        } else if (updates.busy || updates.scan_running) {
                            send_http_error(&connection, 503,
                                            "Service Unavailable",
                                            "Receiver maintenance is running. Try again when the scan/update finishes.\n",
                                            false);
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
                                tune_result_t tune_result =
                                    tune_channel(*control, opt, channel, &tuning);
                                if (tune_result == TUNE_RESULT_CONTROL_ERROR) {
                                    fprintf(stderr,
                                            "Channel control command failed; reconnecting and retrying.\n");
                                    tune_result = recover_http_control(
                                        control, opt, channel, &tuning);
                                    last_heartbeat = monotonic_seconds();
                                }
                                if (tune_result != TUNE_RESULT_OK ||
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
                                        printf("\nClient %llu (%s) joined channel %u (%s).\n",
                                               (unsigned long long)connection_id,
                                               stream.clients[0].peer_host,
                                               (unsigned)channel->lcn,
                                               channel->name);
                                    }
                                }
                            } else if (send_stream_headers(&connection) &&
                                       http_stream_add_client(&stream,
                                                              &connection)) {
                                keep_connection = true;
                                printf("\nClient %llu joined existing channel %u (%s).\n",
                                       (unsigned long long)connection_id,
                                       (unsigned)channel->lcn, channel->name);
                            }
                        }
                    } else {
                        send_http_error(&connection, 404, "Not Found",
                                        "Use /playlist.m3u, /tv.m3u, /radio.m3u, "
                                        "/status.json, /admin/, or /channel/<number>.\n",
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
                {
                    const tune_result_t recover_result =
                        recover_http_control(control, opt, stream.channel, &tuning);
                    if (recover_result != TUNE_RESULT_OK) {
                        http_stream_close_clients(&stream);
                        http_stream_stop_if_idle(&stream);
                        if (recover_result == TUNE_RESULT_CONTROL_ERROR) {
                            result = EXIT_FAILURE;
                            break;
                        }
                        fprintf(stderr,
                                "Receiver reconnected but the channel no longer locks; viewers were disconnected.\n");
                        continue;
                    }
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
    free_epg_list(&epg);
    free_channel_list(&channels);
    device_update_state_free(&updates);
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
    int64_t parsed_time = -1;
    char epg_text[256];
    download_url_t parsed_url;
    options_t tone_test_opt;
    uint8_t tone_frame[FRAME_SIZE];
    uint8_t *decoded_chunked = NULL;
    size_t decoded_chunked_length = 0U;
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
        " freq='11500' lcn='7' s_name='News &amp; Sport &#x2605;' "
        "fta='1' epg_id='news.example' prog_idx='1085734912' ts_id='1011'>",
        &channel) && channel.polarization == 'F' &&
        strcmp(channel.type, "TV") == 0 &&
        strcmp(channel.epg_id, "news.example") == 0 &&
        channel.have_program_index && channel.program_index == 1085734912U &&
        channel.transport_stream_id == 1011U &&
        strstr(channel.name, "News & Sport") != NULL && channel.fta,
        "Multiline channel XML, EPG ID, and named/numeric entities");

    TEST_CHECK(parse_xmltv_time("19700101010000 +0100", &parsed_time) &&
               parsed_time == 0,
               "XMLTV timestamp and UTC offset parsing");
    TEST_CHECK(xml_element_text(
        "<programme><title lang='en'>News &amp; Weather</title></programme>",
        "title", epg_text, sizeof(epg_text)) &&
        strcmp(epg_text, "News & Weather") == 0,
        "XMLTV programme text extraction and entity decoding");

    {
        epg_list_t native_epg;
        size_t native_capacity = 0U;
        memset(&native_epg, 0, sizeof(native_epg));
        snprintf(channel.epg_id, sizeof(channel.epg_id), "zdf.de");
        TEST_CHECK(parse_native_epg_xml(
            (const uint8_t *)"<epg_Data><ch s_id='11110' ts_id='1011' prog_idx='1085734912'><evt id='1' des='Heute &amp; Wetter' dur='00:30' date='40587' time='01:00'/></ch></epg_Data>",
            &channel, &native_epg, &native_capacity) &&
            native_epg.count == 1U &&
            native_epg.items[0].start_utc == 3600LL &&
            native_epg.items[0].stop_utc == 5400LL &&
            strcmp(native_epg.items[0].title, "Heute & Wetter") == 0,
            "Native Device EPG XML and MJD parsing");
        free_epg_list(&native_epg);
    }

    TEST_CHECK(base64_encode((const uint8_t *)"user:pass", 9,
                             encoded, sizeof(encoded)) > 0U &&
               strcmp(encoded, "dXNlcjpwYXNz") == 0,
               "HTTP Basic-auth Base64 encoding");

    TEST_CHECK(parse_download_url(
                   "https://example.test:8443/guide.xml?region=eu",
                   &parsed_url) && parsed_url.https &&
               strcmp(parsed_url.host, "example.test") == 0 &&
               parsed_url.port == 8443U &&
               strcmp(parsed_url.path, "/guide.xml?region=eu") == 0,
               "EPG update URL parser handles HTTPS ports and queries");

    {
        static const uint8_t chunked_sample[] =
            "4\r\n<tv>\r\n5\r\n</tv>\r\n0\r\n\r\n";
        TEST_CHECK(decode_chunked_body(chunked_sample,
                    sizeof(chunked_sample) - 1U,
                    &decoded_chunked, &decoded_chunked_length) &&
                   decoded_chunked_length == 9U &&
                   memcmp(decoded_chunked, "<tv></tv>", 9U) == 0,
                   "EPG HTTP downloader decodes chunked bodies");
        free(decoded_chunked);
        decoded_chunked = NULL;
    }

    memset(&tone_test_opt, 0, sizeof(tone_test_opt));
    tone_test_opt.device_scan_network = true;
    tone_test_opt.device_scan_order_by = 3U;
    tone_test_opt.device_scan_search_range = 5U;
    tone_test_opt.device_scan_sort_mode = 2U;
    tone_test_opt.device_scan_mode = DEVICE_SCAN_UI_FULL_MODE;
    build_device_scan_action_request(frame, &tone_test_opt,
                                     DEVICE_SCAN_AUTOMATIC_ACTION);
    TEST_CHECK(frame[20] == 0x10 && frame[21] == 0x00U,
               "Device automatic satellite scan uses APIX 0x10/wire mode 0");
    TEST_CHECK(frame[22] == 0x95U,
               "Device scan flags match the APK bit layout");
    tone_test_opt.orbital_tenths = 192U;
    tone_test_opt.west = false;
    tone_test_opt.tone = TONE_AUTO;
    build_scan_satellite_config(frame, &tone_test_opt);
    TEST_CHECK(frame[20] == 0x09 && frame[21] == 0x00 &&
               frame[22] == 0xC0 && frame[23] == 0x04U,
               "Automatic scan satellite profile uses POL_ON with automatic tone");
    build_device_scan_status_request(frame);
    TEST_CHECK(frame[20] == 0x02 && frame[11] == 17,
               "Device scan-status request asks for 17 data bytes");
    TEST_CHECK(sizeof(astra_19e_transponders) /
               sizeof(astra_19e_transponders[0]) == 88U,
               "Astra 19.2E sweep contains 88 transponders");
    TEST_CHECK(astra_19e_transponders[0].frequency_mhz == 10729U &&
               astra_19e_transponders[0].polarization == 'V' &&
               astra_19e_transponders[87].frequency_mhz == 12669U &&
               astra_19e_transponders[87].polarization == 'V',
               "Astra transponder sweep boundaries are intact");

    build_permission_claim(frame);
    memset(response, 0, sizeof(response));
    memcpy(response + 4, "USBS", 4);
    memcpy(response + 8, frame + 4, 4);
    response[16] = 0x02;
    TEST_CHECK(validate_csw(response, sizeof(response), 4, frame),
               "Device CSW accepts firmware state 0x02 with valid signature/tag/residue");

    memset(&tone_test_opt, 0, sizeof(tone_test_opt));
    tone_test_opt.tone = TONE_AUTO;
    tone_test_opt.lnb_switch_mhz = 11700U;
    tone_test_opt.frequency_mhz = 11362U;
    tone_test_opt.symbol_rate_ks = 22000U;
    tone_test_opt.polarization = 'H';
    tone_test_opt.orbital_tenths = 192U;
    build_dvbs_tune(tone_frame, &tone_test_opt);
    TEST_CHECK((tone_frame[21] & 0xC0U) == 0x80U,
               "Auto LNB tone resolves OFF below switch frequency");
    tone_test_opt.frequency_mhz = 12226U;
    build_dvbs_tune(tone_frame, &tone_test_opt);
    TEST_CHECK((tone_frame[21] & 0xC0U) == 0x40U,
               "Auto LNB tone resolves ON above switch frequency");

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

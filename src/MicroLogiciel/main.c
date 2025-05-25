#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <hardware/watchdog.h>

#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "../Tools/SimpleFSBuilder/SimpleFS.h"

#include "dhcpserver/dhcpserver.h"
#include "dnsserver/dnsserver.h"
#include "httpserver.h"
#include "server_settings.h"
#include "debug_printf.h"
#include "json_parser.h"
#include "timer.h"

#define TEST_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

struct SimpleFSContext
{
    GlobalFSHeader *header;
    StoredFileEntry *entries;
    char *names, *data;
} s_SimpleFS;

bool simplefs_init(struct SimpleFSContext *ctx, void *data)
{
    ctx->header = (GlobalFSHeader *)data;
    if (ctx->header->Magic != kSimpleFSHeaderMagic) {
        return false;
    }
    ctx->entries = (StoredFileEntry *)(ctx->header + 1);
    ctx->names = (char *)(ctx->entries + ctx->header->EntryCount);
    ctx->data = (char *)(ctx->names + ctx->header->NameBlockSize);
    return true;
}

static void set_secondary_ip_address(int address)
{
    extern int ip4_secondary_ip_address;
    ip4_secondary_ip_address = address;
}

static bool do_retrieve_file(http_connection conn, enum http_request_type type, char *path, void *context)
{
    for (int i = 0; i < s_SimpleFS.header->EntryCount; i++) {
        if (!strcmp(s_SimpleFS.names + s_SimpleFS.entries[i].NameOffset, path)) {
            http_server_send_reply(conn,
                "200 OK",
                s_SimpleFS.names + s_SimpleFS.entries[i].ContentTypeOffset,
                s_SimpleFS.data + s_SimpleFS.entries[i].DataOffset,
                "close",
                s_SimpleFS.entries[i].FileSize);
            return true;
        }
    }
    
    return false;
}

static char *parse_server_settings(http_connection conn, pico_server_settings *settings)
{
    bool has_password = false, use_domain = false, use_second_ip = false;
    bool bad_password = false, bad_domain = false;
    
    for (;;) {
        char *line = http_server_read_post_line(conn);
        if (!line) {
            break;
        }
                
        char *p = strchr(line, '=');
        if (!p) {
            continue;
        }
        *p++ = 0;
        if (!strcasecmp(line, "has_password")) {
            has_password = !strcasecmp(p, "true") || p[0] == '1';
        } else if (!strcasecmp(line, "use_domain")) {
            use_domain = !strcasecmp(p, "true") || p[0] == '1';
        } else if (!strcasecmp(line, "use_second_ip")) {
            use_second_ip = !strcasecmp(p, "true") || p[0] == '1';
        } else if (!strcasecmp(line, "dns_ignores_network_suffix")) {
            settings->dns_ignores_network_suffix = !strcasecmp(p, "true") || p[0] == '1';
        } else if (!strcasecmp(line, "ssid")) {
            if (strlen(p) >= sizeof(settings->network_name)) {
                return "SSID too long";
            }
            if (!p[0]) {
                return "missing SSID";
            }
            strcpy(settings->network_name, p);
        } else if (!strcasecmp(line, "password")) {
            if (strlen(p) >= sizeof(settings->network_password)) {
                bad_password = true;
            } else {
                strcpy(settings->network_password, p);
            }
        } else if (!strcasecmp(line, "hostname")) {
            if (strlen(p) >= sizeof(settings->hostname)) {
                return "hostname too long";
            }
            if (!p[0]) {
                return "missing hostname";
            }
            strcpy(settings->hostname, p);
        } else if (!strcasecmp(line, "domain")) {
            if (strlen(p) >= sizeof(settings->domain_name)) {
                bad_domain = true;
            } else {
                strcpy(settings->domain_name, p);
            }
        } else if (!strcasecmp(line, "ipaddr")) {
            settings->ip_address = ipaddr_addr(p);
            if (!settings->ip_address || settings->ip_address == -1) {
                return "invalid IP address";
            }
        } else if (!strcasecmp(line, "netmask")) {
            settings->network_mask = ipaddr_addr(p);
            if (!settings->network_mask || settings->network_mask == -1) {
                return "invalid network mask";
            }
        } else if (!strcasecmp(line, "ipaddr2")) {
            settings->secondary_address = ipaddr_addr(p);
        }
    }
    
    if (!has_password) {
        memset(settings->network_password, 0, sizeof(settings->network_password));
    } else if (bad_password) {
        return "password too long";
    }
    
    if (!use_domain) {
        memset(settings->domain_name, 0, sizeof(settings->domain_name));
    } else if (bad_domain) {
        return "domain too long";
    }
    
    if (!use_second_ip) {
        settings->secondary_address = 0;
    } else if (!settings->secondary_address || settings->secondary_address == -1) {
        return "invalid secondary IP address";
    }
    
    return NULL;
}
static char buffer_serverSettings[405]; // set to the maximal size of the server settings string
static char *format_server_settings(char *buffer, const pico_server_settings *settings)
{
    debug_printf("\tformat_server_settings:");
    int n = sprintf(buffer, "{\"ssid\":\"%s\", \"has_password\":%d, \"password\":\"%s\", \"hostname\":\"%s\", \"use_domain\":%d, \"domain\":\"%s\", \"ipaddr\":\"%d.%d.%d.%d\", \"netmask\":\"%d.%d.%d.%d\", \"use_second_ip\":%d, \"ipaddr2\":\"%d.%d.%d.%d\", \"dns_ignores_network_suffix\":%d}\n",
                    settings->network_name, // "ssid"
                    settings->network_password[0] != 0, // "has_password"
                    settings->network_password, // "password"
                    settings->hostname, // "hostname"
                    settings->domain_name[0] != 0, // "use_domain"
                    settings->domain_name, // "domain"
                    // "ipaddr"
                    (settings->ip_address >> 0) & 0xFF, (settings->ip_address >> 8) & 0xFF, (settings->ip_address >> 16) & 0xFF, (settings->ip_address >> 24) & 0xFF,
                    // "netmask"
                    (settings->network_mask >> 0) & 0xFF, (settings->network_mask >> 8) & 0xFF, (settings->network_mask >> 16) & 0xFF, (settings->network_mask >> 24) & 0xFF,
                    settings->secondary_address != 0, // "use_second_ip"
                    // "ipaddr2"
                    (settings->secondary_address >> 0) & 0xFF, (settings->secondary_address >> 8) & 0xFF, (settings->secondary_address >> 16) & 0xFF, (settings->secondary_address >> 24) & 0xFF,
                    !!settings->dns_ignores_network_suffix); // "dns_ignores_network_suffix"
    if (!n){
        debug_printf("\tUnable to format data :'(\n");
        return "Unable to format data";
    }
    debug_printf(buffer);
    debug_printf("\n");
    return NULL;
}

bool do_handle_settings_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    if (type == HTTP_POST) {
        static pico_server_settings settings;
        settings = *get_pico_server_settings();

        char *err = parse_server_settings(conn, &settings);
        if (err) {
            http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
            return true;
        }
        
        debug_printf("/!\\--- write_pico_server_settings() ---/!\\... ");
        write_pico_server_settings(&settings);
        debug_printf("Done\n");
        http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
        watchdog_reboot(0, SRAM_END, 500);
         
        return true;
    } else {
        const pico_server_settings *settings = get_pico_server_settings();
        format_server_settings(buffer_serverSettings, settings);
        http_server_send_reply(conn, "200 OK", "text/json", buffer_serverSettings, "close", -1);
        
        return true;
    }
    return false;
}

static void main_task(__unused void *params)
{
    
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return;
    }
    
    extern void *_binary_www_fs_start;
    if (!simplefs_init(&s_SimpleFS, &_binary_www_fs_start)) {
        printf("missing/corrupt FS image");
        return;
    }
    extern SemaphoreHandle_t s_StartTimerSemaphore;
    extern SemaphoreHandle_t s_StopTimerSemaphore;
    extern SemaphoreHandle_t s_UpdateTimerSemaphore;
    
    xSemaphoreGive(s_StartTimerSemaphore);
    xSemaphoreGive(s_StopTimerSemaphore);
    xSemaphoreGive(s_UpdateTimerSemaphore);
    
    const pico_server_settings *settings = get_pico_server_settings();

    cyw43_arch_enable_ap_mode(settings->network_name, settings->network_password, settings->network_password[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);

    struct netif *netif = netif_default;
    ip4_addr_t addr = { .addr = settings->ip_address }, mask = { .addr = settings->network_mask };
    
    netif_set_addr(netif, &addr, &mask, &addr);
    
    // Start the dhcp server
    static dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &netif->ip_addr, &netif->netmask, settings->domain_name);
    dns_server_init(netif->ip_addr.addr, settings->secondary_address, settings->hostname, settings->domain_name, settings->dns_ignores_network_suffix);
    set_secondary_ip_address(settings->secondary_address);
    http_server_instance server = http_server_create(settings->hostname, settings->domain_name, 4, 4096);
    // TODO: simplify http server zone with one master API zone and callback function
    static http_zone zone1, zone2, zone3;
    http_server_add_zone(server, &zone1, "", do_retrieve_file, NULL);
    http_server_add_zone(server, &zone2, "/api/timer", do_handle_timer_api_call, NULL);
    // TODO: handle server and timer settings separatly from a common API settings function
    http_server_add_zone(server, &zone3, "/api/settings", do_handle_settings_api_call, NULL);
    vTaskDelete(NULL);
}

xSemaphoreHandle s_PrintfSemaphore;

void debug_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);
    vprintf(format, args);
    va_end(args);
    xSemaphoreGive(s_PrintfSemaphore);
}

void debug_write(const void *data, int size)
{
    xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);
    //write(1, data, size);
    xSemaphoreGive(s_PrintfSemaphore);
}

void increase_timer_settings(TimerData_t *timerData)
{
    debug_printf("\tincrease_timer_settings\n");
    timerData->numberPicture = (timerData->numberPicture % 5) + 1;
    timerData->exposureTime = timerData->exposureTime + 500;
    timerData->delayTime = timerData->delayTime + 250;
}

void key_pressed_func() {
    char key = getchar_timeout_us(0); // get any pending key press but don't wait
    debug_printf("-> %c\n", key);
    extern TimerData_t timerData;
    increase_timer_settings(&timerData); // TODO: only for test purposes
}

int main(void)
{
    stdio_init_all();
    
    TaskHandle_t task;
    
    // Semaphore declaration
    extern SemaphoreHandle_t s_StartTimerSemaphore;
    extern SemaphoreHandle_t s_StopTimerSemaphore;
    extern SemaphoreHandle_t s_UpdateTimerSemaphore;
    
    s_StartTimerSemaphore = xSemaphoreCreateBinary();
    s_StopTimerSemaphore = xSemaphoreCreateBinary();
    s_UpdateTimerSemaphore = xSemaphoreCreateBinary();
    
    s_PrintfSemaphore = xSemaphoreCreateMutex();
    
    // Get notified if the user presses a key
    stdio_set_chars_available_callback(key_pressed_func, NULL);
    
    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, TEST_TASK_PRIORITY, &task);
    vTaskStartScheduler();
    
    // Never get here
    for (;;)
    return 0;
}

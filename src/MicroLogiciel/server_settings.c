#include "server_settings.h"

#include <portmacro.h>

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <hardware/flash.h>
#include <hardware/watchdog.h>

#include "debug_printf.h"

static char buffer_serverSettings[405]; // set to the maximal size of the server settings string

const union 
{
    pico_server_settings settings;
    char padding[FLASH_SECTOR_SIZE];
} __attribute__((aligned(FLASH_SECTOR_SIZE))) s_Settings = {
    .settings = {
        .ip_address = 0x017BA8C0, // 192.168.123.1
        .network_mask = 0x00FFFFFF, // 255.255.255.0
        .secondary_address = 0x0,//06433c6, // 198.51.100.0 // See the comment before 'secondary_address' definition in 'server_settings.h' for details. // TODO: put back secondary IP address
        .network_name = WIFI_SSID,
        .network_password = WIFI_PASSWORD,
        .hostname = "AstroTimer",
        .domain_name = "piconet.local",
        .dns_ignores_network_suffix = true,
    }
};

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

const pico_server_settings *get_pico_server_settings()
{
    return &s_Settings.settings;
}

void write_pico_server_settings(const pico_server_settings *new_settings)
{
    portENTER_CRITICAL();
    flash_range_erase((uint32_t)&s_Settings - XIP_BASE, FLASH_SECTOR_SIZE);
    flash_range_program((uint32_t)&s_Settings - XIP_BASE, (const uint8_t *)new_settings, sizeof(*new_settings));
    portEXIT_CRITICAL();
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

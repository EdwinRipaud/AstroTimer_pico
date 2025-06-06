#include "server_settings.h"

#include <portmacro.h>

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <hardware/flash.h>
#include <hardware/watchdog.h>

#include "json_parser.h"
#include "debug_printf.h"

static char buffer_server_settings[405]; // set to the maximal size of the server settings string

const union 
{
    pico_server_settings settings;
    char padding[FLASH_SECTOR_SIZE-sizeof(pico_server_settings)]; // padding to get FLASH_SECTOR_SIZE size
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

static JsonStatus parse_server_settings(http_connection conn, pico_server_settings *settings)
{
    char buffer[32];
    char dest[32];
    int count = 0;
    bool has_password = false, use_domain = false, use_second_ip = false;
    
    debug_printf("\tparse_timer:\n");
    for(;;) {
        char *line = http_server_read_post_line(conn);
        if (!line)
            break;
        count++;
        debug_printf("\trecieve JSON: %s\n", line);
        JsonStatus status;
        
        // SSID (char*)
        status = getString(line, "ssid", settings->network_name, sizeof(settings->network_name));
        if (status != JSON_OK) {
            return status;
        }
        
        // has_password (bool)
        status = getBoolean(line, "has_password", &has_password);
        if (status != JSON_OK) {
            return status;
        }
        
        // password (char*)
        status = getString(line, "password", settings->network_password, sizeof(settings->network_password));
        if (status != JSON_OK) {
            return status;
        }
        
        // hostname (char*)
        status = getString(line, "hostname", settings->hostname, sizeof(settings->hostname));
        if (status != JSON_OK) {
            return status;
        }
        
        // use_domain (bool)
        status = getBoolean(line, "use_domain", &use_domain);
        if (status != JSON_OK) {
            return status;
        }
        
        // domain (char*)
        // ???: WTF is going on here?! JSON key change after passing it to 'getString()' for no reason! Anyway, it doesn't matter, because the key is used before its value is modified.
        status = getString(line, "domain", settings->domain_name, sizeof(settings->domain_name));
        if (status != JSON_OK) {
            return status;
        }
        
        // ipaddr (IP address)
        status = getIPAddress(line, "ipaddr", &settings->ip_address);
        if (status != JSON_OK) {
            return status;
        }
        
        // netmask (IP address)
        status = getIPAddress(line, "netmask", &settings->network_mask);
        if (status != JSON_OK) {
            return status;
        }
        
        // use_second_ip (bool)
        status = getBoolean(line, "use_second_ip", &use_second_ip);
        if (status != JSON_OK) {
            return status;
        }
        
        // ipaddr2 (IP address)
        status = getIPAddress(line, "ipaddr2", &settings->secondary_address);
        if (status != JSON_OK) {
            return status;
        }
        
        // dns_ignores_network_suffix (bool)
        status = getBoolean(line, "dns_ignores_network_suffix", &settings->dns_ignores_network_suffix);
        if (status != JSON_OK) {
            return status;
        }
    }
    if (count<=0) {
        debug_printf("\tNo data received\n");
        return JSON_KO;
    } else {
        if (!has_password){
            memset(settings->network_password, 0, sizeof(settings->network_password));
            debug_printf("Set settings->network_password to '0'\n");
        }
        if (!use_domain) {
            memset(settings->domain_name, 0, sizeof(settings->domain_name));
            debug_printf("Set settings->domain_name to '0'\n");
        }
        if (!use_second_ip) {
            settings->secondary_address = 0;
            debug_printf("Set secondary_address to '0.0.0.0'\n");
        } else if (!settings->secondary_address || settings->secondary_address == -1) {
            debug_printf("Invalide IP: ipaddr2 -> %s\n", dest);
            return JSON_INVALID_TYPE;
        }
    }
    return JSON_OK;
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
        
        JsonStatus status = parse_server_settings(conn, &settings);
        if (status != JSON_OK) {
            char *err = JSON_status_message(status);
            debug_printf("Error: %s\n", err);
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
        format_server_settings(buffer_server_settings, settings);
        http_server_send_reply(conn, "200 OK", "text/json", buffer_server_settings, "close", -1);
        
        return true;
    }
    return false;
}

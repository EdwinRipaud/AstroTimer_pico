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

// TODO: create high level JSON function like: char *getString(char *key), bool getBool(char *key), char *getIP(char *key)
static JsonStatus parse_server_settings(http_connection conn, pico_server_settings *settings)
{
    char buffer[32];
    char dest[32];
    int count = 0;
    bool has_password = false, use_domain = false, use_second_ip = false;
    bool bad_ssid = false, bad_password = false, bad_hostname = false, bad_domain = false;
    
    debug_printf("\tparse_timer:\n");
    for(;;) {
        char *line = http_server_read_post_line(conn);
        if (!line)
            break;
        count++;
        debug_printf("\trecieve JSON: %s\n", line);
        // SSID (char*)
        if (!extract_value(line, "\"ssid\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'ssid'\n");
            return JSON_MISSING_KEY;
        } else if (!is_strict_string(buffer)) {
            debug_printf("Not a string: ssid -> %s\n", buffer);
            return JSON_INVALID_STRING;
        } else if (!copy_strip_quote(buffer, settings->network_name, sizeof(settings->network_name))) {
            bad_ssid = true;
        }
        
        // has_password (bool)
        if (!extract_value(line, "\"has_password\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'has_password'\n");
            return JSON_MISSING_KEY;
        } else {
            if (!is_strict_boolean(buffer)) {
                debug_printf("Not a boolean: has_password\n");
                return JSON_INVALID_BOOL;
            } else {
                has_password = !strcasecmp(buffer, "true") || buffer[0] == '1';
            }
        }
        
        // password (char*)
        if (!extract_value(line, "\"password\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'password'\n");
            return JSON_MISSING_KEY;
        } else if (!is_strict_string(buffer)) {
            debug_printf("Not a string: password -> %s\n", buffer);
            return JSON_INVALID_STRING;
        } else if (!copy_strip_quote(buffer, settings->network_password, sizeof(settings->network_password))) {
            bad_password = true;
        }
        
        // hostname (char*)
        if (!extract_value(line, "\"hostname\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'hostname'\n");
            return JSON_MISSING_KEY;
        } else if (!is_strict_string(buffer)) {
            debug_printf("Not a string: hostname -> %s\n", buffer);
            return JSON_INVALID_STRING;
        } else if (!copy_strip_quote(buffer, settings->hostname, sizeof(settings->hostname))) {
            bad_hostname = true;
        }
        
        // use_domain (bool)
        if (!extract_value(line, "\"use_domain\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'use_domain'\n");
            return JSON_MISSING_KEY;
        } else {
            if (!is_strict_boolean(buffer)) {
                debug_printf("Not a boolean: use_domain\n");
                return JSON_INVALID_BOOL;
            } else {
                use_domain = !strcasecmp(buffer, "true") || buffer[0] == '1';
            }
        }
        
        // domain (char*)
        if (!extract_value(line, "\"domain\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'domain'\n");
            return JSON_MISSING_KEY;
        } else if (!is_strict_string(buffer)) {
            debug_printf("Not a string: domain -> %s\n", buffer);
            return JSON_INVALID_STRING;
        } else if (!copy_strip_quote(buffer, settings->domain_name, sizeof(settings->domain_name))) {
            bad_domain = true;
        }
        
        // ipaddr (char*)
        if (!extract_value(line, "\"ipaddr\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'ipaddr'\n");
            return JSON_MISSING_KEY;
        } else if (!is_valid_ip(buffer)) {
            debug_printf("Not an IP address: ipaddr -> %s\n", buffer);
            return JSON_INVALID_IP;
        }
        copy_strip_quote(buffer, dest, sizeof(dest));
        settings->ip_address = ipaddr_addr(dest);
        if (!settings->ip_address || settings->ip_address == -1) {
            debug_printf("Invalide IP: ipaddr -> %s\n", dest);
            return JSON_INVALID_TYPE;
        }
        
        // netmask (char*)
        if (!extract_value(line, "\"netmask\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'netmask'\n");
            return JSON_MISSING_KEY;
        } else if (!is_valid_ip(buffer)) {
            debug_printf("Not an IP address: netmask -> %s\n", buffer);
            return JSON_INVALID_IP;
        }
        copy_strip_quote(buffer, dest, sizeof(dest));
        settings->network_mask = ipaddr_addr(dest);
        if (!settings->network_mask || settings->network_mask == -1) {
            debug_printf("Invalide IP: netmask -> %s\n", dest);
            return JSON_INVALID_TYPE;
        }
        
        // use_second_ip (bool)
        if (!extract_value(line, "\"use_second_ip\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'use_second_ip'\n");
            return JSON_MISSING_KEY;
        } else {
            if (!is_strict_boolean(buffer)) {
                debug_printf("Not a boolean: use_second_ip\n");
                return JSON_INVALID_BOOL;
            } else {
                use_second_ip = !strcasecmp(buffer, "true") || buffer[0] == '1';
            }
        }
        
        // ipaddr2 (char*)
        if (!extract_value(line, "\"ipaddr2\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'ipaddr2'\n");
            return JSON_MISSING_KEY;
        } else if (!is_valid_ip(buffer)) {
            debug_printf("Not an IP address: ipaddr2 -> %s\n", buffer);
            return JSON_INVALID_IP;
        }
        copy_strip_quote(buffer, dest, sizeof(dest));
        settings->secondary_address = ipaddr_addr(dest);
        
        // dns_ignores_network_suffix (bool)
        if (!extract_value(line, "\"dns_ignores_network_suffix\"", buffer, sizeof(buffer))) {
            debug_printf("Missing key: 'dns_ignores_network_suffix'\n");
            return JSON_MISSING_KEY;
        } else {
            if (!is_strict_boolean(buffer)) {
                debug_printf("Not a boolean: dns_ignores_network_suffix\n");
                return JSON_INVALID_BOOL;
            } else {
                settings->dns_ignores_network_suffix = !strcasecmp(buffer, "true") || buffer [0] == '1';
            }
        }
    }
    if (count<=0) {
        debug_printf("\tNo data received\n");
        return JSON_KO;
    } else {
        if (bad_ssid) {
            debug_printf("Bad SSID\n");
            return JSON_KO;
        }
        if (bad_password) {
            debug_printf("Bad password\n");
            return JSON_KO;
        }
        if (bad_hostname) {
            debug_printf("Bad hostname\n");
            return JSON_KO;
        }
        if (bad_domain) {
            debug_printf("Bad domain\n");
            return JSON_KO;
        }
        
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
        //write_pico_server_settings(&settings);
        debug_printf("Done\n");
        http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
        //watchdog_reboot(0, SRAM_END, 500);
         
        return true;
    } else {
        const pico_server_settings *settings = get_pico_server_settings();
        format_server_settings(buffer_server_settings, settings);
        http_server_send_reply(conn, "200 OK", "text/json", buffer_server_settings, "close", -1);
        
        return true;
    }
    return false;
}

#include "server_settings.h"

#include <string.h>
#include <portmacro.h>
#include "hardware/flash.h"

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


const char *get_next_domain_name_component(const char *domain_name, int *position, int *length)
{
    if (!domain_name || !position || !length)
        return NULL;
    
    int pos = *position;
    const char *p = strchr(domain_name + pos, '.');
    if (p) {
        *position = p + 1 - domain_name;
        *length = p - domain_name - pos;
        return domain_name + pos;
    } else if (domain_name[pos]) {
        *length = strlen(domain_name + pos);
        *position = pos + *length;
        return domain_name + pos;
    } else
        return NULL;
}


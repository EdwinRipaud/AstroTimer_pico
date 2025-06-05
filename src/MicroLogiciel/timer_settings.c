#include "timer_settings.h"

#include <pico/cyw43_arch.h>

#include <portmacro.h>

#include <hardware/flash.h>

#include "json_parser.h"
#include "debug_printf.h"

const union
{
    timer_settings timer_data;
    char padding[FLASH_SECTOR_SIZE-sizeof(timer_settings)]; // padding to get FLASH_SECTOR_SIZE size
} __attribute__((aligned(FLASH_SECTOR_SIZE))) s_TimerSettings = {
    .timer_data = {
        .picture_number = 3,
        .exposure_time = 2000,
        .delay_time = 1000,
    }
};

JsonStatus parse_timer(http_connection conn, timer_settings *dest)
{
    char buffer[128];
    int count = 0;
    
    debug_printf("\tparse_timer:\n");
    for(;;) {
        char *line = http_server_read_post_line(conn);
        if (!line)
            break;
        count++;
        debug_printf("\trecieve JSON: %s\n", line);
        JsonStatus status;
        
        // picture (int)
        status = getInteger(line, "picture", &dest->picture_number);
        if (status != JSON_OK) {
            return status;
        }
        
        // exposure (float)
        status = getFloatInt(line, "exposure", &dest->exposure_time);
        if (status != JSON_OK) {
            return status;
        }
        
        // delay (float)
        status = getFloatInt(line, "delay", &dest->delay_time);
        if (status != JSON_OK) {
            return status;
        }
    }
    if (count<=0) {
        debug_printf("\tNo data received\n");
        return JSON_KO;
    }
    return JSON_OK;
}

char *format_timer_settings(char *buffer, timer_settings *timer_data)
{
    debug_printf("\tformat_timer_settings:");
    int n = sprintf(buffer, "{\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}",
                    timer_data->picture_number,
                    (float)(timer_data->exposure_time)/1000,
                    (float)(timer_data->delay_time)/1000);
    if (!n){
        debug_printf("\tUnable to format data :'(\n");
        return "Unable to format data";
    }
    debug_printf(buffer);
    debug_printf("\n");
    return NULL;
}

const timer_settings *get_timer_settings()
{
    return &s_TimerSettings.timer_data;
}

void write_timer_settings(const timer_settings *new_settings)
{
    portENTER_CRITICAL();
    flash_range_erase((uint32_t)&s_TimerSettings - XIP_BASE, FLASH_SECTOR_SIZE);
    flash_range_program((uint32_t)&s_TimerSettings - XIP_BASE, (const uint8_t *)new_settings, sizeof(*new_settings));
    portEXIT_CRITICAL();
}

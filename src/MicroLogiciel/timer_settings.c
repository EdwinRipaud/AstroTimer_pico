#include "timer_settings.h"

#include <pico/cyw43_arch.h>

#include <portmacro.h>

#include <hardware/flash.h>

#include "json_parser.h"
#include "debug_printf.h"

const union
{
    timer_settings settings;
    char padding[FLASH_SECTOR_SIZE-sizeof(timer_settings)]; // padding to get FLASH_SECTOR_SIZE size
} __attribute__((aligned(FLASH_SECTOR_SIZE))) s_TimerSettings = {
    .settings = {
        .picture_number = 3,
        .exposure_time = 2000,
        .delay_time = 1000,
    }
};

static timer_settings dyn_timer_settings = {
    s_TimerSettings.settings.picture_number,
    s_TimerSettings.settings.exposure_time,
    s_TimerSettings.settings.delay_time,
};

extern SemaphoreHandle_t s_UpdateTimerSemaphore;
SemaphoreHandle_t s_IncreaseTimerSemaphore = NULL;
SemaphoreHandle_t s_TimerSettingsSemaphore = NULL;

timer_settings *get_timer_settings()
{
    if (xSemaphoreTake(s_TimerSettingsSemaphore, pdMS_TO_TICKS(25)) == pdTRUE) {
        debug_printf("get_timer_settings() -> GET access\n");
        return &dyn_timer_settings;
    } else {
        debug_printf("get_timer_settings() -> FAILED to get access\n");
    }
    return NULL;
}

timer_settings copy_timer_settings(timer_settings *ptr_settings)
{
    timer_settings out = {
        (uint32_t)(ptr_settings->picture_number),
        (uint32_t)(ptr_settings->exposure_time),
        (uint32_t)(ptr_settings->delay_time),
    };
    return out;
}

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

char *format_timer_settings(char *buffer, timer_settings *settings)
{
    debug_printf("\tformat_timer_settings:");
    int n = sprintf(buffer, "{\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}",
                    settings->picture_number,
                    (float)(settings->exposure_time)/1000,
                    (float)(settings->delay_time)/1000);
    if (!n){
        debug_printf("\tUnable to format data :'(\n");
        return "Unable to format data";
    }
    debug_printf(buffer);
    debug_printf("\n");
    return NULL;
}

// TODO: move this function to 'stream.c'
void interrupt_timer_settings(void *arg)
{
    timer_interrupt_t *interrupt = arg;
    http_connection conn = *interrupt->conn;
    char buffer[128];
    char buffer_param[64];
    
    // set threshold as general parameters
    uint32_t thresholdPicture = 1;
    uint32_t thresholdExposure = 500;
    uint32_t thresholdDelay = 250;
    debug_printf("start -> interrupt_timer_settings: \n");
    for (;;) {
        if ((xSemaphoreTake(s_IncreaseTimerSemaphore, portMAX_DELAY) == pdTRUE) && (xSemaphoreTake(s_UpdateTimerSemaphore, portMAX_DELAY) == pdTRUE)) { // TODO: move 's_UpdateTimerSemaphore' to 'key_pressed_func()' to leave it available for other task
            
            debug_printf("\t-> interrupt_timer_settings(");
            timer_settings *ptr_settings = get_timer_settings();
            
            if (*interrupt->signal > 0x60) {
                debug_printf("+ increase)\n");
                ptr_settings->picture_number = (uint32_t)((ptr_settings->picture_number % 10) + 1);
                ptr_settings->exposure_time = (uint32_t)(ptr_settings->exposure_time + 500);
                ptr_settings->delay_time = (uint32_t)(ptr_settings->delay_time + 250);
            } else {
                debug_printf("- decrease)\n");
                if ((uint32_t)((ptr_settings->picture_number % 10) - 1) < thresholdPicture) {
                    ptr_settings->picture_number = thresholdPicture;
                } else {
                    ptr_settings->picture_number = (uint32_t)((ptr_settings->picture_number % 10) - 1);
                }
                if ((uint32_t)(ptr_settings->exposure_time - 500) < thresholdExposure) {
                    ptr_settings->exposure_time = thresholdExposure;
                } else {
                    ptr_settings->exposure_time = (uint32_t)(ptr_settings->exposure_time - 500);
                }
                if ((uint32_t)(ptr_settings->delay_time - 250) < thresholdDelay) {
                    ptr_settings->delay_time = thresholdDelay;
                } else {
                    ptr_settings->delay_time = (uint32_t)(ptr_settings->delay_time - 250);
                }
            }
            debug_printf("\t%d - %.2fs - %.2fs\n",
                         ptr_settings->picture_number,
                         (float)ptr_settings->exposure_time/1000,
                         (float)ptr_settings->delay_time/1000);
            
            debug_printf("/!\\--- write_timer_settings() ---/!\\... ");
            timer_settings new_settings = copy_timer_settings(ptr_settings);
            //write_timer_settings(&new_settings);
            debug_printf("Done\n");
            
            format_timer_settings(buffer_param, ptr_settings);
            int n = sprintf(buffer, "event: TimerUpdate\ndata: %s\n\n", buffer_param);
            debug_printf("stream : TimerUpdate -> \n");
            // !!!: seem not to be recieved by client...
            if (!http_server_write_reply(conn, buffer)){
                debug_printf("-> Client connection lost\n");
                break; // lost client connection
            }
            
            xSemaphoreGive(s_TimerSettingsSemaphore);
            xSemaphoreGive(s_UpdateTimerSemaphore);
        }
    }
    return;
}

void write_timer_settings(const timer_settings *new_settings)
{
    portENTER_CRITICAL();
    flash_range_erase((uint32_t)&s_TimerSettings - XIP_BASE, FLASH_SECTOR_SIZE);
    flash_range_program((uint32_t)&s_TimerSettings - XIP_BASE, (const uint8_t *)new_settings, sizeof(*new_settings));
    portEXIT_CRITICAL();
}

#include "timer.h"

#include <portmacro.h>

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <hardware/flash.h>
#include <hardware/watchdog.h>

#include "json_parser.h"
#include "debug_printf.h"

static char buffer_timer_data[100];

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

SemaphoreHandle_t s_StartTimerSemaphore = NULL;
SemaphoreHandle_t s_StopTimerSemaphore = NULL;
SemaphoreHandle_t s_UpdateTimerSemaphore = NULL;

static TaskHandle_t s_TimerTaskHandle = NULL;

static JsonStatus parse_timer(http_connection conn, timer_settings *out)
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
        // picture (int)
        if (!extract_value(line, "\"picture\"", buffer, sizeof(buffer))) {
            return JSON_MISSING_KEY;
        }
        if (!is_strict_integer(buffer)) {
            return JSON_INVALID_TYPE;
        }
        debug_printf("\tpicture:%d\n", atoi(buffer));
        out->picture_number = atoi(buffer);
        
        // exposure (int)
        if (!extract_value(line, "\"exposure\"", buffer, sizeof(buffer))) {
            return JSON_MISSING_KEY;
        }
        if (!is_strict_float(buffer)) {
            return JSON_INVALID_TYPE;
        }
        debug_printf("\texposure:%f\n", atof(buffer));
        out->exposure_time = atof(buffer)*1000;
        
        // delay (float)
        if (!extract_value(line, "\"delay\"", buffer, sizeof(buffer))) {
            return JSON_MISSING_KEY;
        }
        if (!is_strict_float(buffer)) {
            return JSON_INVALID_TYPE;
        }
        debug_printf("\tdelay:%f\n", atof(buffer));
        out->delay_time = atof(buffer)*1000;
    }
    if (count<=0) {
        debug_printf("\tNo data received\n");
        return JSON_KO;
    }
    return JSON_OK;
}

static char *format_timer_settings(char *buffer, timer_settings *timer_data)
{
    debug_printf("\tformat_timer_settings:");
    int n = sprintf(buffer, "{\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}", timer_data->picture_number, (float)timer_data->exposure_time/1000, (float)timer_data->delay_time/1000);
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

static void timer_task(void *arg)
{
    timer_settings *param = arg;
    
    uint32_t nbPicture = param->picture_number;
    uint32_t expTime = param->exposure_time;
    uint32_t delayTime = param->delay_time;
    debug_printf("param: {\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}\n", nbPicture, (float)expTime/1000, (float)delayTime/1000);
    TickType_t xLasteWakeTime = xTaskGetTickCount();
    for (int i=0;i<nbPicture-1;i++) {
        debug_printf("\t- loop %d/%d - %d\n", i, nbPicture, xTaskGetTickCount());
        cyw43_arch_gpio_put(SHUTTER_PIN, 1);
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(expTime));
        cyw43_arch_gpio_put(SHUTTER_PIN, 0);
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(delayTime));
    }
    // Last exposure outside the loop to skip the 'delayTime'
    cyw43_arch_gpio_put(SHUTTER_PIN, 1);
    vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(expTime));
    cyw43_arch_gpio_put(SHUTTER_PIN, 0);
    
    debug_printf("\tEnd of task!\n");
    s_TimerTaskHandle = NULL;
    vTaskDelete(NULL);
}

bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    static timer_settings timer_data;
    timer_data = *get_timer_settings();
    if (!strcmp(path, "start")) {
        debug_printf("start\n");
        if (xSemaphoreTake(s_StartTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
            JsonStatus status = parse_timer(conn, &timer_data);
            debug_printf("\tstatus: %s\n", JSON_status_message(status));
            if (status != JSON_OK) {
                char *err = JSON_status_message(status);
                debug_printf("Error: %s\n", err);
                xSemaphoreGive(s_StartTimerSemaphore);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return true;
            }
            xTaskCreate(timer_task, "Timer", configMINIMAL_STACK_SIZE, &timer_data, TIMER_TASK_PRIORITY, &s_TimerTaskHandle);
            xSemaphoreGive(s_StartTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
            return true;
        } else {
            debug_printf("Timer task is already running\n");
            xSemaphoreGive(s_StartTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
            return false;
        }
    }
    else if (!strcmp(path, "stop")) {
        debug_printf("stop\n");
        if (xSemaphoreTake(s_StopTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle != NULL) {
            vTaskDelete(s_TimerTaskHandle);
            s_TimerTaskHandle = NULL;
            xSemaphoreGive(s_StopTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
            cyw43_arch_gpio_put(SHUTTER_PIN, 0);
            return true;
        } else {
            debug_printf("No Timer task is running\n");
            xSemaphoreGive(s_StopTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
            return false;
        }
    }
    else if (!strcmp(path, "update")) {
        debug_printf("update\n");
        if (xSemaphoreTake(s_UpdateTimerSemaphore, 0) == pdTRUE) {
            char *err = format_timer_settings(buffer_timer_data, &timer_data);
            if (err) {
                debug_printf("\tError: %s\n", err);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return true;
            }
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "application/json", buffer_timer_data, "close", -1);
            return true;
        } else {
            debug_printf("Timer is updating webapp infos...\n");
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
            return false;
        }
    } else if (!strcmp(path, "settings")) {
        debug_printf("settings ");
        if (xSemaphoreTake(s_UpdateTimerSemaphore, 0) == pdTRUE) {
            if (type == HTTP_POST) {
                debug_printf("[POST]\n");
                JsonStatus status = parse_timer(conn, &timer_data);
                debug_printf("\tstatus: %s\n", JSON_status_message(status));
                if (status != JSON_OK) {
                    char *err = JSON_status_message(status);
                    debug_printf("Error: %s\n", err);
                    http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                    return true;
                } else {
                    debug_printf("/!\\--- write_timer_settings() ---/!\\... ");
                    write_timer_settings(&timer_data);
                    debug_printf("Done\n");
                    xSemaphoreGive(s_UpdateTimerSemaphore);
                    http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
                    watchdog_reboot(0, SRAM_END, 500);
                }
                return false;
            } else {
                debug_printf("[GET]\n");
                format_timer_settings(buffer_timer_data, &timer_data);
                xSemaphoreGive(s_UpdateTimerSemaphore);
                http_server_send_reply(conn, "200 OK", "text/json", buffer_timer_data, "close", -1);
                return true;
            }
        } else {
            debug_printf("Timer is updating settings...\n");
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
            return false;
        }
    }
    return false;
}

#include "api_callbacks.h"

#include <pico/cyw43_arch.h>

#include <hardware/watchdog.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include "json_parser.h"
#include "debug_printf.h"
#include "timer_core.h"
#include "server_settings.h"
#include "stream.h"
#include "timer_settings.h"

static char buffer_server_settings[512]; // Set server setting buffer

static char buffer_timer_settings[128]; // Set timer settings buffer

SemaphoreHandle_t s_StartTimerSemaphore = NULL;
SemaphoreHandle_t s_StopTimerSemaphore = NULL;
SemaphoreHandle_t s_UpdateTimerSemaphore = NULL;

extern SemaphoreHandle_t s_TimerSettingsSemaphore;

TaskHandle_t s_TimerTaskHandle = NULL;

bool do_handle_settings_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    if (type == HTTP_POST) {
        static server_settings settings;
        settings = *get_server_settings();
        
        JsonStatus status = parse_server_settings(conn, &settings);
        if (status != JSON_OK) {
            char *err = JSON_status_message(status);
            debug_printf("Error: %s\n", err);
            http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
            return false;
        }
        
        debug_printf("/!\\--- write_pico_server_settings() ---/!\\... ");
        write_pico_server_settings(&settings);
        debug_printf("Done\n");
        http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
        watchdog_reboot(0, SRAM_END, 500);
        return true;
    } else if (type == HTTP_GET) {
        const server_settings *settings = get_server_settings();
        format_server_settings(buffer_server_settings, settings);
        http_server_send_reply(conn, "200 OK", "application/json", buffer_server_settings, "close", -1);
        return true;
    } else {
        debug_printf("\tError: 405 Method Not Allowed, only GET and POST supported\n");
        http_server_send_reply(conn, "405 Method Not Allowed", "text/plain", "Only GET and POST supported", "close", -1);
        return false;
    }
    return false;
}

static void timer_task(void *arg)
{
    timer_settings *settings = arg;
    debug_printf("Start timer_task...\n");
    
    timer_core(settings->picture_number, settings->exposure_time, settings->delay_time);
    
    debug_printf("timer_task ended!\n");
    s_TimerTaskHandle = NULL;
    vTaskDelete(NULL);
}

bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    static timer_settings *ptr_settings;
    if (!strcmp(path, "start")) {
        debug_printf("start\n");
        ptr_settings = get_timer_settings();
        if (ptr_settings == NULL) {
            debug_printf("do_handle_timer_api_call() [start] -> FAILED access to timer_settings\n");
            return false;
        }
        if (xSemaphoreTake(s_StartTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
            JsonStatus status = parse_timer(conn, ptr_settings);
            if (status != JSON_OK) {
                char *err = JSON_status_message(status);
                debug_printf("Error: %s\n", err);
                xSemaphoreGive(s_TimerSettingsSemaphore);
                xSemaphoreGive(s_StartTimerSemaphore);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return false;
            }
            debug_printf("/!\\--- write_timer_settings() ---/!\\... ");
            write_timer_settings(ptr_settings);
            debug_printf("Done\n");
            xTaskCreate(timer_task, "Timer", configMINIMAL_STACK_SIZE, ptr_settings, TIMER_TASK_PRIORITY, &s_TimerTaskHandle);
            xSemaphoreGive(s_TimerSettingsSemaphore);
            xSemaphoreGive(s_StartTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
        } else {
            debug_printf("Timer task is already running\n");
            xSemaphoreGive(s_TimerSettingsSemaphore);
            xSemaphoreGive(s_StartTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "RUNNING TASK", "close", -1);
            return false;
        }
        return true;
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
            http_server_send_reply(conn, "200 OK", "text/plain", "NO TASK", "close", -1);
            return false;
        }
    } else if (!strcmp(path, "settings")) {
        debug_printf("settings ");
        ptr_settings = get_timer_settings();
        if (ptr_settings == NULL) {
            debug_printf("do_handle_timer_api_call() [settings] -> FAILED access to timer_settings\n");
            return false;
        }
        if (type == HTTP_POST) {
            debug_printf("[POST]\n");
            if (xSemaphoreTake(s_UpdateTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
                JsonStatus status = parse_timer(conn, ptr_settings);
                if (status != JSON_OK) {
                    char *err = JSON_status_message(status);
                    debug_printf("Error: %s\n", err);
                    xSemaphoreGive(s_TimerSettingsSemaphore);
                    xSemaphoreGive(s_UpdateTimerSemaphore);
                    http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                    return false;
                } else {
                    debug_printf("/!\\--- write_timer_settings() ---/!\\... ");
                    write_timer_settings(ptr_settings);
                    debug_printf("Done\n");
                    xSemaphoreGive(s_TimerSettingsSemaphore);
                    xSemaphoreGive(s_UpdateTimerSemaphore);
                }
                return false;
            } else {
                debug_printf("AstroTimer is busy...\n");
                xSemaphoreGive(s_TimerSettingsSemaphore);
                xSemaphoreGive(s_UpdateTimerSemaphore);
                http_server_send_reply(conn, "200 OK", "text/plain", "BUSY", "close", -1);
                return false;
            }
        } else if (type == HTTP_GET) {
            debug_printf("[GET]\n");
            format_timer_settings(buffer_timer_settings, ptr_settings);
            xSemaphoreGive(s_TimerSettingsSemaphore);
            http_server_send_reply(conn, "200 OK", "application/json", buffer_timer_settings, "close", -1);
            return true;
        } else {
            xSemaphoreGive(s_TimerSettingsSemaphore);
            debug_printf("\tError: 405 Method Not Allowed, only GET and POST supported\n");
            http_server_send_reply(conn, "405 Method Not Allowed", "text/plain", "Only GET and POST supported", "close", -1);
            return false;
        }
    }
    return false;
}

bool do_handle_stream_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    debug_printf("stream ");
    if (type == HTTP_GET) {
        debug_printf("[GET]\n");
        
        sse_context_t ctx = {
            .conn = &conn,
            .stream_count_semaphore = xSemaphoreCreateCounting(2,0),
        };
        
        if (!http_server_begin_write_reply(*ctx.conn, "200 OK", "text/event-stream", "keep-alive")){
            debug_printf("-> Unable to send stream request header\n");
            return false;
        }
        xTaskCreate(temperature_stream, "SSE_temperature", configMINIMAL_STACK_SIZE, &ctx, tskIDLE_PRIORITY, NULL);
        xTaskCreate(battery_stream, "SSE_battery", configMINIMAL_STACK_SIZE, &ctx, tskIDLE_PRIORITY, NULL);
        
        if (xSemaphoreTake(ctx.stream_count_semaphore, portMAX_DELAY) == pdTRUE) {
            debug_printf("-> All stream ended\n");
        }
        return true;
    } else {
        debug_printf("\tError: 405 Method Not Allowed, only GET supported\n");
        http_server_send_reply(conn, "405 Method Not Allowed", "text/plain", "Only GET supported", "close", -1);
        return false;
    }
    return false;
}

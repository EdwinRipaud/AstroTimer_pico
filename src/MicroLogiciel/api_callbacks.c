#include "api_callbacks.h"

#include <pico/cyw43_arch.h>

#include <hardware/watchdog.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include "json_parser.h"
#include "debug_printf.h"
#include "timer_core.h"
#include "server_settings.h"
#include "timer_settings.h"

static char buffer_server_settings[512]; // Set server setting buffer

static char buffer_timer_data[128]; // Set timer settings buffer

SemaphoreHandle_t s_StartTimerSemaphore = NULL;
SemaphoreHandle_t s_StopTimerSemaphore = NULL;
SemaphoreHandle_t s_UpdateTimerSemaphore = NULL;

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
            return true;
        }
        
        debug_printf("/!\\--- write_pico_server_settings() ---/!\\... ");
        write_pico_server_settings(&settings);
        debug_printf("Done\n");
        http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
        watchdog_reboot(0, SRAM_END, 500);
         
        return true;
    } else {
        const server_settings *settings = get_server_settings();
        format_server_settings(buffer_server_settings, settings);
        http_server_send_reply(conn, "200 OK", "text/json", buffer_server_settings, "close", -1);
        
        return true;
    }
    return false;
}

static void timer_task(void *arg)
{
    timer_settings *param = arg;
    debug_printf("Start Timer_task...\n");
    
    timer_core(param->picture_number, param->exposure_time, param->delay_time);
    
    debug_printf("Timer_task ended!\n");
    s_TimerTaskHandle = NULL;
    vTaskDelete(NULL);
}

bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    static timer_settings timer_data;
    if (!strcmp(path, "start")) {
        debug_printf("start\n");
        timer_data = *get_timer_settings();
        if (xSemaphoreTake(s_StartTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
            JsonStatus status = parse_timer(conn, &timer_data);
            if (status != JSON_OK) {
                char *err = JSON_status_message(status);
                debug_printf("Error: %s\n", err);
                xSemaphoreGive(s_StartTimerSemaphore);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return true;
            }
            timer_settings interval_data = timer_data;
            debug_printf("/!\\--- write_timer_settings() ---/!\\... ");
            write_timer_settings(&timer_data);
            debug_printf("Done\n");
            xTaskCreate(timer_task, "Timer", configMINIMAL_STACK_SIZE, &interval_data, TIMER_TASK_PRIORITY, &s_TimerTaskHandle);
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
        timer_data = *get_timer_settings();
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
    } else if (!strcmp(path, "settings")) {
        debug_printf("settings ");
        timer_data = *get_timer_settings();
        if (type == HTTP_POST) {
            debug_printf("[POST]\n");
            if (xSemaphoreTake(s_StartTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
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
                    //http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
                    //watchdog_reboot(0, SRAM_END, 500);
                }
                return false;
            } else {
                debug_printf("AstroTimer is busy...\n");
                xSemaphoreGive(s_StartTimerSemaphore);
                http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
                return false;
            }
        } else {
            debug_printf("[GET]\n");
            format_timer_settings(buffer_timer_data, &timer_data);
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/json", buffer_timer_data, "close", -1);
            return true;
        }
    }
    return false;
}

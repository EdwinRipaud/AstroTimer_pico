#include "api_callbacks.h"

#include <pico/cyw43_arch.h>
#include <lwip/sockets.h>

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
        http_server_send_reply(conn, "200 OK", "application/json", buffer_server_settings, "close", -1);
        
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
            http_server_send_reply(conn, "200 OK", "application/json", buffer_timer_data, "close", -1);
            return true;
        }
    }
    return false; // TODO: rework all boolean function output status: true = function OK; false = function NOT OK
}

TaskHandle_t s_StreamTaskHandle = NULL;

static void stream_task(void *pvParameters)
{
    debug_printf("Start stream_task...\n");
    
    sse_context_t *ctx = (sse_context_t *)pvParameters;
    http_connection conn = ctx->conn;
    
    if (!http_server_begin_write_reply(conn, "200 OK", "text/event-stream", "keep-alive")){
        debug_printf("-> Unable to send header\n");
        goto cleanup;
    }
    char buffer[128];
    TickType_t xLasteWakeTime;
    for (;;) {
        int n = sprintf(buffer, "event: Temp\ndata: {\"temperature\": %.1f}\nretry: %d\n\n", get_onboard_temperature('C'), 2*3000);
        debug_printf("stream -> \n");
        
        if (!http_server_write_reply(conn, buffer)){
            debug_printf("-> Client connection lost\n");
            break; // lost client connection
        }
        debug_printf(buffer);
        
        xLasteWakeTime = xTaskGetTickCount();
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(3000));
    }
    http_server_end_write_reply(conn, NULL);
    
cleanup:
    debug_printf("stream_task ended!\n");
    closesocket(http_connection_get_socket(conn));
    vPortFree(conn);
    vPortFree(ctx);
    vTaskDelete(NULL);
}

bool do_handle_stream_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    debug_printf("stream ");
    if (type != HTTP_GET) {
        debug_printf("[POST]\n");
        debug_printf("\tError: 405 Method Not Allowed, only GET supported\n");
        http_server_send_reply(conn, "405 Method Not Allowed", "text/plain", "Only GET supported", "close", -1);
        return true;
    }
    
    debug_printf("[GET]\n");
    
    sse_context_t *ctx = pvPortMalloc(sizeof(sse_context_t));
    if (!ctx) {
        debug_printf("\tError: 500 Internal Server Error, unable to allocate ‘sse_context_t‘\n");
        http_server_send_reply(conn, "500 Internal Server Error", "text/plain", "Out of memory", "close", -1);
        return true;
    }
    
    ctx->conn = conn;
    
    if (xTaskCreate(stream_task, "SSE_Stream", configMINIMAL_STACK_SIZE, ctx, tskIDLE_PRIORITY + 1, &s_StreamTaskHandle) != pdPASS) {
        vPortFree(ctx);
        debug_printf("\tError: 500 Internal Server Error, unable to create ‘stream_task‘\n");
        http_server_send_reply(conn, "500 Internal Server Error", "text/plain", "Task creation failed", "close", -1);
        return true;
    } else {
        debug_printf("!!! - SSE_Stream running...\n");
    }
    return false;
}

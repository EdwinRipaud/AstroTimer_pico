#include "timer.h"

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include "json_parser.h"
#include "debug_printf.h"

static char buffer_timerData[100];

// TODO: define timer settings as a const union (cf. 's_Settings' -> 'server_settings.c') to enable default timer settings modification
TimerData_t timerData = {3, 2000, 1000};

SemaphoreHandle_t s_StartTimerSemaphore = NULL;
SemaphoreHandle_t s_StopTimerSemaphore = NULL;
SemaphoreHandle_t s_UpdateTimerSemaphore = NULL;

static TaskHandle_t s_TimerTaskHandle = NULL;

static JsonStatus parse_timer(http_connection conn, TimerData_t* out)
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
        out->numberPicture = atoi(buffer);
        
        // exposure (int)
        if (!extract_value(line, "\"exposure\"", buffer, sizeof(buffer))) {
            return JSON_MISSING_KEY;
        }
        if (!is_strict_float(buffer)) {
            return JSON_INVALID_TYPE;
        }
        debug_printf("\texposure:%f\n", atof(buffer));
        out->exposureTime = atof(buffer)*1000;
        
        // delay (float)
        if (!extract_value(line, "\"delay\"", buffer, sizeof(buffer))) {
            return JSON_MISSING_KEY;
        }
        if (!is_strict_float(buffer)) {
            return JSON_INVALID_TYPE;
        }
        debug_printf("\tdelay:%f\n", atof(buffer));
        out->delayTime = atof(buffer)*1000;
    }
    if (count<=0) {
        debug_printf("\tNo data received\n");
        return JSON_KO;
    }
    return JSON_OK;
}

static char *format_timer_settings(char *buffer, TimerData_t *timerData)
{
    debug_printf("\tformat_timer_settings:");
    int n = sprintf(buffer, "{\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}", timerData->numberPicture, timerData->exposureTime/1000, timerData->delayTime/1000);
    if (!n){
        debug_printf("\tUnable to format data :'(\n");
        return "Unable to format data";
    }
    debug_printf(buffer);
    debug_printf("\n");
    return NULL;
}

static void timer_task(void *arg)
{
    TimerData_t *param = arg;
    
    int nbPicture = param->numberPicture;
    float expTime = param->exposureTime;
    float delayTime = param->delayTime;
    debug_printf("param: {\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}\n", nbPicture, expTime/1000, delayTime/1000);
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
    if (!strcmp(path, "start")) {
        debug_printf("start\n");
        if (xSemaphoreTake(s_StartTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
            JsonStatus status = parse_timer(conn, &timerData);
            debug_printf("\tstatus: %s\n", JSON_status_message(status));
            if (status != JSON_OK) {
                char *err = JSON_status_message(status);
                debug_printf("Error: %s\n", err);
                xSemaphoreGive(s_StartTimerSemaphore);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return true;
            }
            xTaskCreate(timer_task, "Timer", configMINIMAL_STACK_SIZE, &timerData, TIMER_TASK_PRIORITY, &s_TimerTaskHandle);
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
            char *err = format_timer_settings(buffer_timerData, &timerData);
            if (err) {
                debug_printf("\tError: %s\n", err);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return true;
            }
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "application/json", buffer_timerData, "close", -1);
            return true;
        } else {
            debug_printf("No Timer task is running\n");
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
            return false;
        }
    }
    return false;
}

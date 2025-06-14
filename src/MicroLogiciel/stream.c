#include "stream.h"

#include <pico/cyw43_arch.h>
#include <hardware/adc.h>

#include "debug_printf.h"

// Choose 'C' for Celsius or 'F' for Fahrenheit. TODO: add to general settings
#define TEMPERATURE_UNITS 'C'
#define TEMPERATURE_DELAY 3000

// References for this implementation: raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
float get_onboard_temperature(const char unit)
{
    /* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
    const float conversionFactor = 3.3f / (1 << 12);
    
    float adc = (float)adc_read() * conversionFactor;
    float tempC = 27.0f - (adc - 0.706f) / 0.001721f;
    
    if (unit == 'C') {
        return tempC;
    } else if (unit == 'F') {
        return tempC * 9 / 5 + 32;
    }
    
    return -1.0f;
}

// TODO: set a power management struct to hold percentage, voltage, amperage, cycle count
// TODO: store those informations into the flash like s_ServerSettings
float get_onboard_battery()
{
    // TODO: make a proper battery monitoring script
    int batt = rand() % 1001;
    return (float)batt/10;
}


void temperature_stream(void *pvParameters)
{
    sse_context_t *ctx = (sse_context_t *)pvParameters;
    http_connection conn = ctx->conn;
    
    int update_time = 1500; // TODO: set update time from global settings
    
    char buffer[128];
    TickType_t xLasteWakeTime = xTaskGetTickCount();
    for (;;) {
        float temp = get_onboard_temperature('C');
        int n = sprintf(buffer, "event: Temperature\ndata: {\"temperature\": %.1f}\n\n", temp);
        debug_printf("stream : Temperature -> \n");
        
        if (!http_server_write_reply(conn, buffer)){
            debug_printf("-> Client connection lost\n");
            break; // lost client connection
        }
        debug_printf(buffer);
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(update_time));
    }
    xSemaphoreGive(ctx->stream_end_semaphore);
    vTaskDelete(NULL);
}

void battery_stream(void *pvParameters)
{
    sse_context_t *ctx = (sse_context_t *)pvParameters;
    http_connection conn = ctx->conn;
    
    int update_time = 4000; // TODO: set update time from global settings
    
    char buffer[128];
    TickType_t xLasteWakeTime = xTaskGetTickCount();
    for (;;) {
        float batt = get_onboard_battery();
        int n = sprintf(buffer, "event: Battery\ndata: {\"battery\": %.1f}\n\n", batt);
        debug_printf("stream : Battery -> \n");
        
        if (!http_server_write_reply(conn, buffer)){
            debug_printf("-> Client connection lost\n");
            break; // lost client connection
        }
        debug_printf(buffer);
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(update_time));
    }
    xSemaphoreGive(ctx->stream_end_semaphore);
    vTaskDelete(NULL);
}

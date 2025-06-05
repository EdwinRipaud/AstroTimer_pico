#include "timer_core.h"

#include <pico/cyw43_arch.h>

#include <FreeRTOS.h>
#include <task.h>

#include "debug_printf.h"

void timer_core(uint32_t nbPicture, uint32_t expTime, uint32_t delayTime)
{
    debug_printf("\tparam: {\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}\n", nbPicture, (float)(expTime)/1000, (float)(delayTime)/1000);
    TickType_t xLasteWakeTime, xCounterTime;
    int i=1;
    for (;i<nbPicture;i++) {
        debug_printf("\t- loop %d/%d - ", i, nbPicture);
        // Take picture
        xCounterTime = xTaskGetTickCount(); // counter
        xLasteWakeTime = xTaskGetTickCount();
        cyw43_arch_gpio_put(SHUTTER_PIN, 1);
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(expTime));
        debug_printf("%d ms - ", xTaskGetTickCount()-xCounterTime);
        // Wait for the next picture
        xCounterTime = xTaskGetTickCount(); // counter
        cyw43_arch_gpio_put(SHUTTER_PIN, 0);
        xLasteWakeTime = xTaskGetTickCount();
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(delayTime));
        debug_printf("%d ms\n", xTaskGetTickCount()-xCounterTime);
    }
    // Last exposure outside the loop to skip the 'delayTime'
    debug_printf("\t- loop %d/%d - ", i, nbPicture);
    xCounterTime = xTaskGetTickCount(); // counter
    xLasteWakeTime = xTaskGetTickCount();
    cyw43_arch_gpio_put(SHUTTER_PIN, 1);
    vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(expTime));
    debug_printf("%d ms\n", xTaskGetTickCount()-xCounterTime);
    cyw43_arch_gpio_put(SHUTTER_PIN, 0);
}

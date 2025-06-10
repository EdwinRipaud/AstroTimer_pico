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

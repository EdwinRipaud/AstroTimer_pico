#ifndef STREAM_H
#define STREAM_H

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include "httpserver.h"

float get_onboard_temperature(const char unit);
void temperature_stream(void *pvParameters);

float get_onboard_battery();
void battery_stream(void *pvParameters);

#endif


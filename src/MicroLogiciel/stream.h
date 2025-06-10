#ifndef STREAM_H
#define STREAM_H

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include "httpserver.h"

float get_onboard_temperature(const char unit);

#endif


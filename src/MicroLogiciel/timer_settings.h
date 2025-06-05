#ifndef TIMER_SETTINGS_H
#define TIMER_SETTINGS_H

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include "json_parser.h"
#include "httpserver.h"

#define TIMER_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

typedef struct
{
    uint32_t picture_number;
    uint32_t exposure_time;
    uint32_t delay_time;
} timer_settings;

JsonStatus parse_timer(http_connection conn, timer_settings *dest);

char *format_timer_settings(char *buffer, timer_settings *timerData);

const timer_settings *get_timer_settings();

void write_timer_settings(const timer_settings *new_settings);

#endif

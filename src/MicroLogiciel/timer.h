#ifndef TIMER_H
#define TIMER_H

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "json_parser.h"

#include "httpserver.h"

#define SHUTTER_PIN CYW43_WL_GPIO_LED_PIN // FIXME: replace by physical PIN
#define TIMER_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

typedef struct
{
    uint32_t picture_number;
    uint32_t exposure_time;
    uint32_t delay_time;
} timer_settings;

static JsonStatus parse_timer(http_connection conn, timer_settings* out);

static char *format_timer_settings(char *buffer, timer_settings *timerData);

const timer_settings *get_timer_settings();

void write_timer_settings(const timer_settings *new_settings);

static void timer_task(void *arg);

bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context);

#endif

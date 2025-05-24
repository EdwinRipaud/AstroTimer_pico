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
{ // TODO: standardize variable names
    int numberPicture;
    float exposureTime;
    float delayTime;
} TimerData_t;

static JsonStatus parse_timer_JSON(http_connection conn, TimerData_t* out);
static char *parse_timer_settings(http_connection conn, TimerData_t *timerData);

static char *format_timer_settings(char *buffer, TimerData_t *timerData);

static void timer_task(void *arg);

bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context);

#endif

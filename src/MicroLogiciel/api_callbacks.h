#ifndef API_CALLBACKS_H
#define API_CALLBACKS_H

#include <pico/stdlib.h>

#include "httpserver.h"

bool do_handle_settings_api_call(http_connection conn, enum http_request_type type, char *path, void *context);

static void timer_task(void *arg);

bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context);

static void stream_task(void *arg);

bool do_handle_stream_api_call(http_connection conn, enum http_request_type type, char *path, void *context);

#endif

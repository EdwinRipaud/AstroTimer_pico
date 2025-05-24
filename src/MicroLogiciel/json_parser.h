#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <pico/stdlib.h>

typedef enum
{
    JSON_OK,
    JSON_KO,
    JSON_MISSING_KEY,
    JSON_INVALID_TYPE
} JsonStatus;

char *JSON_status_message(JsonStatus status);

int extract_value(const char* json, const char* key, char* out_value, size_t out_size);

int is_strict_integer(const char* s);
int is_strict_float(const char* s);

#endif

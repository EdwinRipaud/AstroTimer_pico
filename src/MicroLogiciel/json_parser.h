#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <pico/stdlib.h>

typedef enum
{
    JSON_OK,
    JSON_KO,
    JSON_MISSING_KEY,
    JSON_INVALID_TYPE,
    JSON_INVALID_INTEGER,
    JSON_INVALID_FLOAT,
    JSON_INVALID_BOOLEAN,
    JSON_INVALID_STRING,
    JSON_INVALID_IP_ADDRESS
} JsonStatus;

char *JSON_status_message(JsonStatus status);

int extract_value(const char* json, const char* key, char* out_value, size_t out_size);

int is_strict_integer(const char* s);
int is_strict_float(const char* s);
int is_strict_boolean(const char* s);
int is_strict_string(const char* s);
int is_valid_ip_address(const char* ip);

int copy_strip_quote(const char* src, char* dest, size_t dest_size);
int to_quoted_string(const char* src, char* dest);

JsonStatus getBoolean(const char* json, const char* key, bool* dest);
JsonStatus getInteger(const char* json, const char* key, uint32_t* dest);
JsonStatus getFloatInt(const char* json, const char* key, uint32_t* dest);
JsonStatus getString(const char* json, const char* key, char* buffer, size_t buffer_size);
JsonStatus getIPAddress(const char* json, const char* key, uint32_t* dest);

#endif

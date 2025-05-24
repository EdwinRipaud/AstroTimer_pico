#include "json_parser.h"

#include <string.h>
#include <ctype.h>

char *JSON_status_message(JsonStatus status) {
    switch (status) {
        case JSON_OK:
            return "JSON OK";
        case JSON_KO:
            return "JSON KO";
        case JSON_MISSING_KEY:
            return "JSON missing key";
        case JSON_INVALID_TYPE:
            return "JSON invalid type";
        
        default:
            return NULL;
    }
}

int extract_value(const char* json, const char* key, char* out_value, size_t out_size) {
    char* found = strstr(json, key);
    if (!found) return 0;
    
    found = strchr(found, ':');
    if (!found) return 0;
    found++;
    
    while (isspace(*found)) found++;
    
    if (*found == '"') {
        found++;
        char* end = strchr(found, '"');
        if (!end) {
            return 0;
        }
        size_t len = end - found;
        if (len >= out_size) {
            return 0;
        }
        strncpy(out_value, found, len);
        out_value[len] = '\0';
    } else {
        char* end = strpbrk(found, ",}");
        if (!end) {
            return 0;
        }
        size_t len = end - found;
        if (len >= out_size) {
            return 0;
        }
        strncpy(out_value, found, len);
        out_value[len] = '\0';
    }
    
    return 1;
}

int is_strict_integer(const char* s) {
    if (*s == '-') s++; // Skip leading minus sign if present
    if (!*s) {
        return 0; // Reject if string is just "-" or empty
    }
    for (; *s; s++) {
        if (!isdigit(*s)) {
            return 0; // Must be all digits
        }
    }
    return 1;
}

int is_strict_float(const char* s) {
    if (*s == '-') s++; // Skip leading minus sign if present
    
    int has_digit = 0;
    int has_dot = 0;
    
    for (; *s; s++) {
        if (*s == '.') {
            if (has_dot) {
                return 0; // Only one dot allowed
            }
            has_dot = 1;
        } else if (isdigit(*s)) {
            has_digit = 1; // At least one digit required
        } else {
            return 0; // Invalid character
        }
    }
    
    return has_digit;
}

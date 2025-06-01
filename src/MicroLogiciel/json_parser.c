#include "json_parser.h"

#include <string.h>
#include <ctype.h>

#include <pico/cyw43_arch.h>

#include "debug_printf.h"

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
        case JSON_INVALID_INTEGER:
            return "JSON invalid (int)";
        case JSON_INVALID_FLOAT:
            return "JSON invalid (float)";
        case JSON_INVALID_BOOLEAN:
            return "JSON invalid (bool)";
        case JSON_INVALID_STRING:
            return "JSON invalid (str)";
        case JSON_INVALID_IP_ADDRESS:
            return "JSON invalid ip address";
        
        default:
            return NULL;
    }
}

int extract_value(const char* json, const char* key, char* out_value, size_t out_size) {
    char* found = strstr(json, key); // Find the first occurence of the key
    if (!found) {
        return 0;
    }
    found = strchr(found, ':'); // Find the first value delimiter from the found key
    if (!found) {
        return 0;
    }
    found++;
    while (isspace(*found)) found++; // Skip whitespace
    char* end = strpbrk(found, ",}"); // Find the first occurence of one of the delimiter ',' our '}'
    if (!end) {
        return 0;
    }
    size_t len = end - found; // Get the length of the value
    if (len >= out_size) {
        return 0;
    }
    strncpy(out_value, found, len); // Copy the value tou the output
    out_value[len] = '\0'; // Ensure it's a string
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

int is_strict_boolean(const char* s) {
    return strcmp(s, "true") == 0 || strcmp(s, "false") == 0 || (strlen(s) == 1 && (s[0] == '0' || s[0] == '1'));
}

int is_strict_string(const char* s) {
    size_t len = strlen(s);
    if (len < 2) {
        return 0; // Too short to be quoted
    }
    if (s[0] != '"' || s[len - 1] != '"') {
        return 0;
    }
    return 1;
}

int is_valid_ip_address(const char* ip) {
    [[maybe_unused]] ip4_addr_t _;
    if (!ip4addr_aton(ip, &_)) {
        return 0;
    }
    return 1;
}

int copy_strip_quote(const char* src, char* dest, size_t dest_size) {
    size_t len = strlen(src);
    
    // Must start and end with quotes, and be at least 2 chars long
    if (len < 2 || src[0] != '"' || src[len - 1] != '"')
        return 0;
    
    size_t content_len = len - 2;
    if (content_len >= dest_size)
        return 0; // not enough space in dest
    
    strncpy(dest, src + 1, content_len);
    dest[content_len] = '\0';
    
    return 1;
}

int to_quoted_string(const char* src, char* dest) {
    if (!is_strict_string(src)) { // If not a quoted string
        if (!sprintf(dest, "\"%s\"", src)) { // Quote the output string
            return 0;
        }
    } else {
        strncpy(dest, src, strlen(src)); // Copy the input quoted string to the output
    }
    return 1;
}

JsonStatus getBoolean(const char* json, const char* key, bool* dest) {
    char buffer[7];
    char key_[strlen(key)+2];
    if (!to_quoted_string(key, key_)) { // Ensure 'key' is a qouted string
        return JSON_KO;
    }
    debug_printf("%s\n", key);
    if (!extract_value(json, key_, buffer, sizeof(buffer))) { // Extract the value associated with the key
        debug_printf("\tMissing key: %s\n", key_);
        return JSON_MISSING_KEY;
    } else if (!is_strict_boolean(buffer)) { // Check if it's a boolean
        debug_printf("\tNot a boolean: %s -> %s\n", key_, buffer);
        return JSON_INVALID_BOOLEAN;
    }
    dest = (bool*)(!strcasecmp(buffer, "true") || buffer[0] == '1'); // Caste the value to the output
    
    debug_printf("\t-> %s: %d\n", key_, dest);
    return JSON_OK;
}

JsonStatus getInteger(const char* json, const char* key, uint32_t* dest) {
    char buffer[10];
    char tmp[10];
    char key_[strlen(key)+2];
    if (!to_quoted_string(key, key_)) { // Ensure 'key' is a qouted string
        return JSON_KO;
    }
    debug_printf("%s\n", key);
    if (!extract_value(json, key_, buffer, sizeof(buffer))) { // Extract the value associated with the key
        debug_printf("\tMissing key: %s\n", key_);
        return JSON_MISSING_KEY;
    }
    copy_strip_quote(buffer, tmp, sizeof(tmp));
    if (!is_strict_integer(tmp)) { // Check if it's an interger
        debug_printf("\tNot an interger: %s -> %s\n", key_, tmp);
        return JSON_INVALID_INTEGER;
    }
    *dest = atoi(tmp);; // Caste the value to the output
    
    debug_printf("\t-> %s: %d\n", key_, *dest);
    return JSON_OK;
}

JsonStatus getFloatInt(const char* json, const char* key, uint32_t* dest) {
    char buffer[16];
    char tmp[16];
    char key_[strlen(key)+2];
    if (!to_quoted_string(key, key_)) { // Ensure 'key' is a qouted string
        return JSON_KO;
    }
    debug_printf("%s\n", key);
    if (!extract_value(json, key_, buffer, sizeof(buffer))) { // Extract the value associated with the key
        debug_printf("\tMissing key: %s\n", key_);
        return JSON_MISSING_KEY;
    }
    copy_strip_quote(buffer, tmp, sizeof(tmp));
    if (!is_strict_float(tmp)) { // Check if it's an float
        debug_printf("\tNot a float: %s -> %s\n", key_, tmp);
        return JSON_INVALID_FLOAT;
    }
    *dest = (uint32_t)(atof(tmp)*1000); // Caste the value to the output
    
    debug_printf("\t-> %s: %d\n", key_, *dest);
    return JSON_OK;
}

JsonStatus getString(const char* json, const char* key, char* dest, size_t dest_size) {
    char buffer[dest_size];
    char key_[strlen(key)+2];
    if (!to_quoted_string(key, key_)) { // Ensure 'key' is a qouted string
        return JSON_KO;
    }
    debug_printf("%s\n", key);
    if (!extract_value(json, key_, buffer, dest_size)) { // Extract the value associated with the key
        debug_printf("\tMissing key: %s\n", key_);
        return JSON_MISSING_KEY;
    }
    if (!is_strict_string(buffer)) { // Check if it's a string
        debug_printf("\tNot a string: %s -> %s\n", key_, buffer);
        return JSON_INVALID_STRING;
    } else if (!copy_strip_quote(buffer, dest, dest_size)) {
        debug_printf("\tBad %s\n", key_);
        return JSON_KO;
    }
    debug_printf("\t-> %s: %s\n", key_, dest);
    return JSON_OK;
}

JsonStatus getIPAddress(const char* json, const char* key, uint32_t* dest) {
    char buffer[17];
    char tmp[17];
    uint32_t tmp_ip;
    char key_[strlen(key)+2];
    if (!to_quoted_string(key, key_)) { // Ensure 'key' is a qouted string
        return JSON_KO;
    }
    debug_printf("%s\n", key);
    if (!extract_value(json, key_, buffer, sizeof(buffer))) { // Extract the value associated with the key
        debug_printf("\tMissing key: %s\n", key_);
        return JSON_MISSING_KEY;
    }
    copy_strip_quote(buffer, tmp, sizeof(tmp));
    if (!is_valid_ip_address(tmp)) { // Check if it's an IP address
        debug_printf("\tNot an IP address: %s -> %s\n", key_, buffer);
        return JSON_INVALID_IP_ADDRESS;
    }
    *dest = ipaddr_addr(tmp);
    if (*dest == -1) { // Double security check
        debug_printf("\tInvalide IP: ipaddr -> %s\n", tmp);
        return JSON_INVALID_IP_ADDRESS;
    }
    debug_printf("\t-> %s: %X\n", key_, *dest);
    return JSON_OK;
}


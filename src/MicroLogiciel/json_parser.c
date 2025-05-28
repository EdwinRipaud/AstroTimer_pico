#include "json_parser.h"

#include <string.h>
#include <ctype.h>

#include <pico/cyw43_arch.h>

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
        case JSON_INVALID_INT:
            return "JSON invalid (int)";
        case JSON_INVALID_FLOAT:
            return "JSON invalid (float)";
        case JSON_INVALID_BOOL:
            return "JSON invalid (bool)";
        case JSON_INVALID_STRING:
            return "JSON invalid (str)";
        case JSON_INVALID_IP:
            return "JSON invalid ip address";
        
        default:
            return NULL;
    }
}

int extract_value(const char* json, const char* key, char* out_value, size_t out_size) {
    char* found = strstr(json, key);
    if (!found) {
        return 0;
    }
    
    found = strchr(found, ':');
    if (!found) {
        return 0;
    }
    found++;
    
    while (isspace(*found)) found++;
    
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
    /*
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
    }*/
    
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
    //return (strlen(s) == 1 && (s[0] == '0' || s[0] == '1'));
    return strcmp(s, "true") == 0 || strcmp(s, "false") == 0;
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

int is_valid_ip(const char* ip) {
    if (!ip) {
        return 0;
    }
    
    size_t len = strlen(ip);
    if (len < 3 || ip[0] != '"' || ip[len - 1] != '"') {
        return 0;
    }
    
    // Copy content inside the quotes
    char addr[32];
    size_t addr_len = len - 2;
    if (addr_len >= sizeof(addr)) {
        return 0;
    }
    strncpy(addr, ip + 1, addr_len);
    addr[addr_len] = '\0';
    
    // Tokenize and validate format "X.X.X.X"
    int parts = 0;
    char* token = strtok(addr, ".");
    while (token) {
        if (parts >= 4) {
            return 0; // too many segments
        }
        
        // Only digits allowed
        for (int i = 0; token[i]; i++) {
            if (!isdigit((unsigned char)token[i])) {
                return 0;
            }
        }
        
        // Reject leading zeros (e.g. "01")
        if (token[0] == '0' && strlen(token) > 1) {
            return 0;
        }
        
        int num = atoi(token);
        if (num < 0 || num > 255) {
            return 0;
        }
        
        parts++;
        token = strtok(NULL, ".");
    }
    
    return parts == 4;
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

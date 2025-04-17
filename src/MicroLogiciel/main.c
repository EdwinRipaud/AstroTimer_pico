//#include <stdarg.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>

#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "dhcpserver/dhcpserver.h"
#include "dnsserver/dnsserver.h"
#include "server_settings.h"
#include "httpserver.h"
#include "../Tools/SimpleFSBuilder/SimpleFS.h"
#include "debug_printf.h"

#define TEST_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

struct SimpleFSContext
{
    GlobalFSHeader *header;
    StoredFileEntry *entries;
    char *names, *data;
} s_SimpleFS;

bool simplefs_init(struct SimpleFSContext *ctx, void *data)
{
    ctx->header = (GlobalFSHeader *)data;
    if (ctx->header->Magic != kSimpleFSHeaderMagic)
        return false;
    ctx->entries = (StoredFileEntry *)(ctx->header + 1);
    ctx->names = (char *)(ctx->entries + ctx->header->EntryCount);
    ctx->data = (char *)(ctx->names + ctx->header->NameBlockSize);
    return true;
}

static void set_secondary_ip_address(int address)
{
    extern int ip4_secondary_ip_address;
    ip4_secondary_ip_address = address;
}

static bool do_retrieve_file(http_connection conn, enum http_request_type type, char *path, void *context)
{
    for (int i = 0; i < s_SimpleFS.header->EntryCount; i++) {
        if (!strcmp(s_SimpleFS.names + s_SimpleFS.entries[i].NameOffset, path)) {
            http_server_send_reply(conn,
                "200 OK",
                s_SimpleFS.names + s_SimpleFS.entries[i].ContentTypeOffset,
                s_SimpleFS.data + s_SimpleFS.entries[i].DataOffset,
                "close",
                s_SimpleFS.entries[i].FileSize);
            return true;
        }
    }
    
    return false;
}

typedef struct {
    int numberPicture;
    float exposureTime;
    float delayTime;
} TimerData_t;
TimerData_t timerData = {3, 2000, 1000};

SemaphoreHandle_t s_StartTimerSemaphore = NULL;
SemaphoreHandle_t s_StopTimerSemaphore = NULL;
TaskHandle_t s_TimerTaskHandle = NULL;

static char *parse_timer_settings(http_connection conn, TimerData_t *timerData)
{
    debug_printf("\tparse_timer_settings:\n");
    for(;;) {
        char *line = http_server_read_post_line(conn);
        if (!line)
            break;
        debug_printf("\trecieve: %s\n", line);
        char *p = strchr(line, '=');
        if (!p)
            continue;
        *p++ = 0;
        if (!strcasecmp(line, "picture")) {
            int picture = p ? atoi(p) : 0;
            if (picture) {
                debug_printf("\t\tpicture: %d\n", picture);
                timerData->numberPicture = picture;
            }
            else {
                return "Invalid number of picture!";
            }
        }
        else if (!strcasecmp(line, "exposure")) {
            float exposure = p ? atof(p) : 0;
            if (exposure) {
                debug_printf("\t\texposure: %f\n", exposure);
                timerData->exposureTime = exposure*1000;
            }
            else {
                return "Invalid exposure!";
            }
        }
        else if (!strcasecmp(line, "delay")) {
            float delay = p ? atof(p) : 0;
            if (delay) {
                debug_printf("\t\tdelay: %f\n", delay);
                timerData->delayTime = delay*1000;
            }
            else {
                return "Invalid delay!";
            }
        }
    }
    return NULL;
}

static void timer_task(void *arg)
{
    TimerData_t *param = arg;
    TickType_t xLasteWakeTime = xTaskGetTickCount();
    for (int i=0;i<param->numberPicture;i++) {
        debug_printf("\t- loop %d/%d - %d\n", i, param->numberPicture, xTaskGetTickCount());
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(param->exposureTime));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(param->delayTime));
    }
    debug_printf("\tEnd of task!\n");
    s_TimerTaskHandle = NULL;
    vTaskDelete(NULL);
}

static bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    if (!strcmp(path, "start")) {
        if (xSemaphoreTake(s_StartTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
            char *err = parse_timer_settings(conn, &timerData);
            if (err) {
                debug_printf("\tError: %s\n", err);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return true;
            }
            xTaskCreate(timer_task, "Timer", configMINIMAL_STACK_SIZE, &timerData, TEST_TASK_PRIORITY, &s_TimerTaskHandle);
            xSemaphoreGive(s_StartTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
            return true;
        } else {
            debug_printf("Timer task is already running\n");
            xSemaphoreGive(s_StartTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "Timer is already running", "close", -1);
            return false;
        }
    }
    else if (!strcmp(path, "stop")) {
        if (xSemaphoreTake(s_StopTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle != NULL) {
            vTaskDelete(s_TimerTaskHandle);
            s_TimerTaskHandle = NULL;
            xSemaphoreGive(s_StopTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // replace by physical PIN
            return true;
        } else {
            debug_printf("No Timer task is running\n");
            http_server_send_reply(conn, "200 OK", "text/plain", "No Timer is running", "close", -1);
            return false;
        }
    }
    return false;
}

static void main_task(__unused void *params)
{
    
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return;
    }
    
    extern void *_binary_www_fs_start;
    if (!simplefs_init(&s_SimpleFS, &_binary_www_fs_start)) {
        printf("missing/corrupt FS image");
        return;
    }
    
    xSemaphoreGive(s_StartTimerSemaphore);
    xSemaphoreGive(s_StopTimerSemaphore);
    
    const pico_server_settings *settings = get_pico_server_settings();

    cyw43_arch_enable_ap_mode(settings->network_name, settings->network_password, settings->network_password[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);

    struct netif *netif = netif_default;
    ip4_addr_t addr = { .addr = settings->ip_address }, mask = { .addr = settings->network_mask };
    
    netif_set_addr(netif, &addr, &mask, &addr);
    
    // Start the dhcp server
    static dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &netif->ip_addr, &netif->netmask, settings->domain_name);
    dns_server_init(netif->ip_addr.addr, settings->secondary_address, settings->hostname, settings->domain_name, settings->dns_ignores_network_suffix);
    set_secondary_ip_address(settings->secondary_address);
    http_server_instance server = http_server_create(settings->hostname, settings->domain_name, 4, 4096);
    static http_zone zone1, zone2;
    http_server_add_zone(server, &zone1, "", do_retrieve_file, NULL);
    http_server_add_zone(server, &zone2, "/api/timer", do_handle_timer_api_call, NULL);
    vTaskDelete(NULL);
}

xSemaphoreHandle s_PrintfSemaphore;

void debug_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);
    vprintf(format, args);
    va_end(args);
    xSemaphoreGive(s_PrintfSemaphore);
}

void debug_write(const void *data, int size)
{
    xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);
    //write(1, data, size);
    xSemaphoreGive(s_PrintfSemaphore);
}

void key_pressed_func() {
    char key = getchar_timeout_us(0); // get any pending key press but don't wait
    debug_printf("-> %c\n", key);
}

int main(void)
{
    stdio_init_all();
    
    TaskHandle_t task;
    
    // Semaphore declaration
    s_StartTimerSemaphore = xSemaphoreCreateBinary();
    s_StopTimerSemaphore = xSemaphoreCreateBinary();
    s_PrintfSemaphore = xSemaphoreCreateMutex();
    
    // Get notified if the user presses a key
    stdio_set_chars_available_callback(key_pressed_func, NULL);
    
    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, TEST_TASK_PRIORITY, &task);
    vTaskStartScheduler();
    
    // Ne jamais atteindre ici
    for (;;)
    return 0;
}

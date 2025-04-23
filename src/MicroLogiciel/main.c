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
static char buffer_timerData[100];

SemaphoreHandle_t s_StartTimerSemaphore = NULL;
SemaphoreHandle_t s_StopTimerSemaphore = NULL;
SemaphoreHandle_t s_UpdateTimerSemaphore = NULL;
TaskHandle_t s_TimerTaskHandle = NULL;

static char *parse_timer_settings(http_connection conn, TimerData_t *timerData)
{
    debug_printf("\tparse_timer_settings:\n");
    int count = 0;
    for(;;) {
        char *line = http_server_read_post_line(conn);
        if (!line)
            break;
        count++;
        debug_printf("\trecieve: %s\n", line);
        char *p = strchr(line, '=');
        if (!p)
            continue;
        *p++ = 0;
        if (!strcasecmp(line, "picture")) {
            int picture = p ? atoi(p) : 0;
            if (picture) {
                //debug_printf("\t\tpicture: %d\n", picture);
                timerData->numberPicture = picture;
            }
            else {
                return "Invalid number of picture!";
            }
        }
        else if (!strcasecmp(line, "exposure")) {
            float exposure = p ? atof(p) : 0;
            if (exposure) {
                //debug_printf("\t\texposure: %f\n", exposure);
                timerData->exposureTime = exposure*1000;
            }
            else {
                return "Invalid exposure!";
            }
        }
        else if (!strcasecmp(line, "delay")) {
            float delay = p ? atof(p) : 0;
            if (delay) {
                //debug_printf("\t\tdelay: %f\n", delay);
                timerData->delayTime = delay*1000;
            }
            else {
                return "Invalid delay!";
            }
        }
    }
    if (count<=0)
        return "unable to read data!";
    
    debug_printf("parse_timerData: {\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}\n", timerData->numberPicture, timerData->exposureTime/1000, timerData->delayTime/1000);
    return NULL;
}

static char *format_timer_settings(char *buffer, TimerData_t *timerData)
{
    debug_printf("\tformat_timer_settings:\n");
    int n = sprintf(buffer, "{\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}", timerData->numberPicture, timerData->exposureTime/1000, timerData->delayTime/1000);
    if (!n){
        return "Unable to format data";
    }
    return NULL;
}

static void timer_task(void *arg)
{
    TimerData_t *param = arg;
    
    int nbPicture = param->numberPicture;
    float expTime = param->exposureTime;
    float delayTime = param->delayTime;
    debug_printf("param: {\"picture\":%d,\"exposure\":%.2f,\"delay\":%.2f}\n", nbPicture, expTime/1000, delayTime/1000);
    TickType_t xLasteWakeTime = xTaskGetTickCount();
    for (int i=0;i<nbPicture;i++) { // TODO: loop one time less to avoid last delayTime whitch is useless
        debug_printf("\t- loop %d/%d - %d\n", i, nbPicture, xTaskGetTickCount());
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); // FIXME: replace by physical PIN
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(expTime));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // FIXME: replace by physical PIN
        vTaskDelayUntil(&xLasteWakeTime, pdMS_TO_TICKS(delayTime));
    }
    debug_printf("\tEnd of task!\n");
    s_TimerTaskHandle = NULL;
    vTaskDelete(NULL);
}

static bool do_handle_timer_api_call(http_connection conn, enum http_request_type type, char *path, void *context)
{
    if (!strcmp(path, "start")) {
        debug_printf("start\n");
        if (xSemaphoreTake(s_StartTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle == NULL){
            char *err = parse_timer_settings(conn, &timerData);
            if (err) {
                debug_printf("\tError: %s\n", err);
                xSemaphoreGive(s_StartTimerSemaphore);
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
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
            return false;
        }
    }
    else if (!strcmp(path, "stop")) {
        debug_printf("stop\n");
        if (xSemaphoreTake(s_StopTimerSemaphore, 0) == pdTRUE && s_TimerTaskHandle != NULL) {
            vTaskDelete(s_TimerTaskHandle);
            s_TimerTaskHandle = NULL;
            xSemaphoreGive(s_StopTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "OK", "close", -1);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // TODO: replace by physical PIN
            return true;
        } else {
            debug_printf("No Timer task is running\n");
            xSemaphoreGive(s_StopTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
            return false;
        }
    }
    else if (!strcmp(path, "update")) {
        debug_printf("update\n");
        if (xSemaphoreTake(s_UpdateTimerSemaphore, 0) == pdTRUE) {
            char *err = format_timer_settings(buffer_timerData, &timerData);
            if (err) {
                debug_printf("\tError: %s\n", err);
                http_server_send_reply(conn, "200 OK", "text/plain", err, "close", -1);
                return true;
            }
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "application/json", buffer_timerData, "close", -1);
            return true;
        } else {
            debug_printf("No Timer task is running\n");
            xSemaphoreGive(s_UpdateTimerSemaphore);
            http_server_send_reply(conn, "200 OK", "text/plain", "NOT OK", "close", -1);
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
    xSemaphoreGive(s_UpdateTimerSemaphore);
    
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

void increase_timer_settings(TimerData_t *timerData)
{
    debug_printf("\tincrease_timer_settings\n");
    timerData->numberPicture = timerData->numberPicture + 1;
    timerData->exposureTime = timerData->exposureTime + 1000;
    timerData->delayTime = timerData->delayTime + 1000;
}

void key_pressed_func() {
    char key = getchar_timeout_us(0); // get any pending key press but don't wait
    debug_printf("-> %c\n", key);
    increase_timer_settings(&timerData); // TODO: only for test purposes
}

int main(void)
{
    stdio_init_all();
    
    TaskHandle_t task;
    
    // Semaphore declaration
    s_StartTimerSemaphore = xSemaphoreCreateBinary();
    s_StopTimerSemaphore = xSemaphoreCreateBinary();
    s_UpdateTimerSemaphore = xSemaphoreCreateBinary();
    
    s_PrintfSemaphore = xSemaphoreCreateMutex();
    
    // Get notified if the user presses a key
    stdio_set_chars_available_callback(key_pressed_func, NULL);
    
    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, TEST_TASK_PRIORITY, &task);
    vTaskStartScheduler();
    
    // Never get here
    for (;;)
    return 0;
}

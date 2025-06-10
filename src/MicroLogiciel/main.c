#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <hardware/adc.h>

#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "../Tools/SimpleFSBuilder/SimpleFS.h"

#include "dhcpserver/dhcpserver.h"
#include "dnsserver/dnsserver.h"
#include "httpserver.h"
#include "server_settings.h"
#include "timer_settings.h"
#include "api_callbacks.h"
#include "debug_printf.h"
#include "json_parser.h"

#define MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

struct SimpleFSContext
{
    GlobalFSHeader *header;
    StoredFileEntry *entries;
    char *names, *data;
} s_SimpleFS;

bool simplefs_init(struct SimpleFSContext *ctx, void *data)
{
    ctx->header = (GlobalFSHeader *)data;
    if (ctx->header->Magic != kSimpleFSHeaderMagic) {
        return false;
    }
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
    extern SemaphoreHandle_t s_StartTimerSemaphore;
    extern SemaphoreHandle_t s_StopTimerSemaphore;
    extern SemaphoreHandle_t s_UpdateTimerSemaphore;
    
    xSemaphoreGive(s_StartTimerSemaphore);
    xSemaphoreGive(s_StopTimerSemaphore);
    xSemaphoreGive(s_UpdateTimerSemaphore);
    
    const server_settings *settings = get_server_settings();

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
    // TODO: simplify http server zone with one master API zone and callback function
    static http_zone zone1, zone2, zone3, zone4;
    http_server_add_zone(server, &zone1, "", do_retrieve_file, NULL);
    http_server_add_zone(server, &zone2, "/api/timer", do_handle_timer_api_call, NULL);
    http_server_add_zone(server, &zone3, "/api/settings", do_handle_settings_api_call, NULL);
    http_server_add_zone(server, &zone4, "/api/stream", do_handle_stream_api_call, NULL);
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

void increase_timer_settings(timer_settings *timer_data)
{
    debug_printf("\tincrease_timer_settings\n");
    timer_data->picture_number = (timer_data->picture_number % 5) + 1;
    timer_data->exposure_time = timer_data->exposure_time + 500;
    timer_data->delay_time = timer_data->delay_time + 250;
}

void key_pressed_func() {
    char key = getchar_timeout_us(0); // get any pending key press but don't wait
    debug_printf("-> %c\n", key);
    static timer_settings timer_data;
    timer_data = *get_timer_settings();
    increase_timer_settings(&timer_data); // MARK: only for test purposes
}

int main(void)
{
    stdio_init_all();
    
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    
    TaskHandle_t task;
    
    // Semaphore declaration
    extern SemaphoreHandle_t s_StartTimerSemaphore;
    extern SemaphoreHandle_t s_StopTimerSemaphore;
    extern SemaphoreHandle_t s_UpdateTimerSemaphore;
    
    s_StartTimerSemaphore = xSemaphoreCreateBinary();
    s_StopTimerSemaphore = xSemaphoreCreateBinary();
    s_UpdateTimerSemaphore = xSemaphoreCreateBinary();
    
    s_PrintfSemaphore = xSemaphoreCreateMutex();
    
    // Get notified if the user presses a key
    stdio_set_chars_available_callback(key_pressed_func, NULL);
    
    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, MAIN_TASK_PRIORITY, &task);
    vTaskStartScheduler();
    
    // Never get here
    for (;;)
    return 0;
}

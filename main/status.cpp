#include "status.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc.h"

#include <string.h>
#include <time.h>

#include "ota.h"
#include "wifi.h"

static const char *TAG = "status";

static char page_buf[1536];
static int buf_offset;

struct timeval status_first_sntp_sync{};

static int buf_printf(const char *format, ...)
{
    if (buf_offset >= sizeof(page_buf) - 1)
    {
        return -1;
    }
    va_list ap;
    va_start(ap, format);
    int left = sizeof(page_buf) - buf_offset;
    int n = vsnprintf(page_buf + buf_offset, left, format, ap);
    va_end(ap);
    if (n > 0)
    {
        buf_offset += n > left ? left - 1 : n;
    }

    return n;
}

void status_print_info(int (*printfn)(const char *format, ...))
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printfn("This is %s chip with %d CPU core(s), WiFi%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printfn("silicon revision %d, ", chip_info.revision);

    rtc_cpu_freq_config_t config;
    rtc_clk_cpu_freq_get_config(&config);
    printfn("freq %d, ", config.freq_mhz);

    uint32_t size;
    esp_flash_get_size(nullptr, &size);
    printfn("%dMB %s flash\n", size / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    printfn("MAC %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const char *name;
    esp_err_t err = esp_netif_get_hostname(wifi_get_netif(), &name);
    if (err == ESP_OK)
    {
        printfn("hostname: %s\n", name);
    }
    else
    {
        printfn("get hostname err: %d\n", err);
    }

    if (wifi_ssid[0] != '\0')
    {
        printfn("Wifi: %s\n", wifi_ssid);
    }

    char time_buf[32] = "time not synced";

    if (status_first_sntp_sync.tv_sec != 0)
    {
        struct tm t;
        localtime_r(&status_first_sntp_sync.tv_sec, &t);
        asctime_r(&t, time_buf);
        size_t len = strlen(time_buf);
        if (len > 0)
        {
            time_buf[len - 1] = '\0';
        }
    }

    printfn("First sntp sync: %s\n", time_buf);

    printfn("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());



    multi_heap_info_t info;
	heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
	size_t tot = info.total_free_bytes + info.total_allocated_bytes;
    printfn("%zd psram\n", tot);

    int64_t uptime = esp_timer_get_time();
    int64_t secs = uptime / 1000000;
    int64_t mins = secs / 60;
    secs %= 60;
    int64_t hours = mins / 60;
    mins %= 60;
    int64_t days = hours / 24;
    hours %= 24;
    printfn("Uptime: %d days %d hours %d mins %d secs\n", (int)days, (int)hours, (int)mins, (int)secs);

    status_extra_info_function(printfn);
    
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != nullptr; it = esp_partition_next(it)) 
    {
        const esp_partition_t *p = esp_partition_get(it);
        printfn("partition: %s %d %d %x %x\n", p->label, p->type, p->subtype, p->address, p->size);
    }
    esp_partition_iterator_release(it);

    const esp_partition_t *partition = esp_ota_get_running_partition();
    printfn("Currently running partition: %s\n", partition->label);
    char boot_hash[65], current_hash[65];
    ota_get_partition_hashes(boot_hash, current_hash);
    printfn("boot sha256: %s\n", boot_hash);
    printfn("current partition sha256: %s\n", current_hash);
    int nTasks = uxTaskGetNumberOfTasks();
    printfn("%d tasks\n", nTasks);
    char *buf = static_cast<char*>(malloc(50 * nTasks));
    vTaskList(buf);
    printfn("%s\n", buf);
    free(buf);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "status req: %p", * (void **)req->aux);
    buf_offset = 0;
    buf_printf("<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\" /><link rel=\"icon\" href=\"favicon.ico\" type=\"image/x-icon\" /></head>");
    buf_printf("<body><a href=\".\">Home</a><br/><pre style=\"font-size: 1.2rem\">\n");
    status_print_info(buf_printf);    
    buf_printf("</pre></body></html>\n");
    esp_err_t res = httpd_resp_set_type(req, "text/html");
    if(res != ESP_OK){
        return res;
    }
    res = httpd_resp_set_hdr(req, "Connection", "close");
	return httpd_resp_send(req, page_buf, buf_offset);
}

void status_add_endpoints(httpd_handle_t server)
{
    httpd_uri_t status{};
    status.uri       = "/status";
    status.method    = HTTP_GET;
    status.handler   = status_handler;
    httpd_register_uri_handler(server, &status);
}


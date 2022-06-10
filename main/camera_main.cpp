/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "camera.h"
#include "index.h"
#include "ota.h"
#include "wifi.h"

#include <esp_http_server.h>


#define TAG "camera"
#define PART_BOUNDARY "123456789000000000000987654321"

#define _STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" PART_BOUNDARY

static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req);
static esp_err_t still_handler(httpd_req_t *req);

static char page_buf[1024];
static int buf_offset;

static unsigned int persistent_flags;

static int buf_printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(page_buf + buf_offset, sizeof(page_buf) - buf_offset, format, ap);
    va_end(ap);
    if (n > 0)
    {
        buf_offset += n;
    }

    return n;
}

static esp_err_t get_saved_flags()
{
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open("camera", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "no saved flags, err %d\n", err);
        return err;
    }

    err = nvs_get_u32(nvs_handle, "flags", &persistent_flags);
    ESP_LOGI(TAG, "read saved flags %u, err %d\n", persistent_flags, err);
    nvs_close(nvs_handle);

    return ESP_OK;
}

static esp_err_t set_saved_flags()
{
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open("camera", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_u32(nvs_handle, "flags", persistent_flags);
    ESP_LOGI(TAG, "save saved flags %u, err %d\n", persistent_flags, err);
    nvs_close(nvs_handle);
    return ESP_OK;
}

static void print_chip_info(int (*printfn)(const char *format, ...))
{
    printfn("<html><body><pre>\n");
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

    printfn("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

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
    auto s = esp_camera_sensor_get();
    if (s != nullptr)
    {
        printfn("Camera: vflip %d hflip %d\n", s->status.vflip, s->status.hmirror);
    }
    
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != nullptr; it = esp_partition_next(it)) 
    {
        const esp_partition_t *p = esp_partition_get(it);
        printfn("partition: %s %d %d %x %x\n", p->label, p->type, p->subtype, p->address, p->size);
    }
    esp_partition_iterator_release(it);

    int nTasks = uxTaskGetNumberOfTasks();
    printfn("%d tasks\n", nTasks);
    char *buf = static_cast<char*>(malloc(50 * nTasks));
    vTaskList(buf);
    printfn("%s\n", buf);
    free(buf);
    
    printfn("</pre></body></html>\n");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "status req: %p", * (void **)req->aux);
    buf_offset = 0;
    print_chip_info(buf_printf);
    esp_err_t res = httpd_resp_set_type(req, "text/html");
    if(res != ESP_OK){
        return res;
    }
    res = httpd_resp_set_hdr(req, "Connection", "close");
	return httpd_resp_send(req, page_buf, buf_offset);
}

static esp_err_t log_handler(httpd_req_t *req)
{
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) 
    {
        char *buf = (char *)malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(TAG, "Found log query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "level", param, sizeof(param)) == ESP_OK) 
            {
                esp_log_level_set("*", static_cast<esp_log_level_t>(atoi(param)));
            }
        }
        free(buf);
    }

    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_handler(httpd_req_t *req)
{
    auto res = httpd_resp_set_hdr(req, "Connection", "close");
    if (res != ESP_OK)
    {
        return res;
    }

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) 
    {
        char *buf = (char *)malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            auto s = esp_camera_sensor_get();
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "resolution", param, sizeof(param)) == ESP_OK) 
            {
                ESP_LOGI(TAG, "Found URL query parameter => resolution=%s", param);
                if (strcmp(param, "svga") == 0)
                {
                    s->set_framesize(s, FRAMESIZE_SVGA);
                }
                else if (strcmp(param, "xga") == 0)
                {
                    s->set_framesize(s, FRAMESIZE_XGA);
                }
            }
            if (httpd_query_key_value(buf, "vflip", param, sizeof(param)) == ESP_OK) 
            {
                ESP_LOGI(TAG, "Found URL query parameter => vflip=%s", param);
                if (strcmp(param, "0") == 0 || strcmp(param, "1") == 0)
                {
                    s->set_vflip(s, param[0] - '0');
                }
                else if (strcmp(param, "2") == 0)
                {
                    uint8_t cur = s->status.vflip;
                    s->set_vflip(s, 1 - cur);
                }
                persistent_flags = (persistent_flags & ~1u) | (s->status.vflip != 0);
                set_saved_flags();
            }
            if (httpd_query_key_value(buf, "hflip", param, sizeof(param)) == ESP_OK) 
            {
                ESP_LOGI(TAG, "Found URL query parameter => hflip=%s", param);
                if (strcmp(param, "0") == 0 || strcmp(param, "1") == 0)
                {
                    s->set_hmirror(s, param[0] - '0');
                }
                else if (strcmp(param, "2") == 0)
                {
                    uint8_t cur = s->status.hmirror;
                    s->set_hmirror(s, 1 - cur);
                }
                persistent_flags = (persistent_flags & ~2u) | ((s->status.hmirror != 0) << 1);
                set_saved_flags();
            }
        }
        free(buf);
    }    
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t update_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, "text/html");
    if(res != ESP_OK){
        return res;
    }
    res = httpd_resp_set_hdr(req, "Connection", "close");
	return httpd_resp_send(req, update_page, strlen(update_page));
}

static esp_err_t index_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, "text/html");
    if(res != ESP_OK){
        return res;
    }
    res = httpd_resp_set_hdr(req, "Connection", "close");
	return httpd_resp_send(req, index_page, strlen(index_page));
}

static esp_err_t restart_handler(httpd_req_t *req)
{
	send_reboot_page(req, "Rebooting now!");

	vTaskDelay(500 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 32;
    config.core_id = 0;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_uri_t index{};
        index.uri       = "/";
        index.method    = HTTP_GET;
        index.handler   = index_handler;
        httpd_register_uri_handler(server, &index);

        httpd_uri_t status{};
        status.uri       = "/status";
        status.method    = HTTP_GET;
        status.handler   = status_handler;
        httpd_register_uri_handler(server, &status);

        httpd_uri_t stream{};
        stream.uri       = "/stream";
        stream.method    = HTTP_GET;
        stream.handler   = stream_handler;
        httpd_register_uri_handler(server, &stream);

        httpd_uri_t still{};
        still.uri       = "/still";
        still.method    = HTTP_GET,
        still.handler   = still_handler,
        httpd_register_uri_handler(server, &still);

        httpd_uri_t update{};
        update.uri	  = "/update";
        update.method   = HTTP_GET;
        update.handler  = update_handler;
        httpd_register_uri_handler(server, &update);

        httpd_uri_t post_update{};
        post_update.uri	  = "/post_update";
        post_update.method   = HTTP_POST;
        post_update.handler  = update_post_handler;
        httpd_register_uri_handler(server, &post_update);

        httpd_uri_t update_config{};
        update_config.uri	  = "/config";
        update_config.method   = HTTP_GET;
        update_config.handler  = config_handler;
        httpd_register_uri_handler(server, &update_config);

        httpd_uri_t log_config{};
        log_config.uri	  = "/log";
        log_config.method   = HTTP_GET;
        log_config.handler  = log_handler;
        httpd_register_uri_handler(server, &log_config);

        httpd_uri_t restart{};
        restart.uri	  = "/restart";
        restart.method   = HTTP_GET;
        restart.handler  = restart_handler;
        httpd_register_uri_handler(server, &restart);

        #if CONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(server);
        #endif
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

extern "C" void app_main(void)
{
    print_chip_info(printf);

    esp_err_t err =  nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "start up wifi");
    wifi_init_sta();

    ESP_LOGI(TAG, "start up camera");
    camera_init();

    if (get_saved_flags() == ESP_OK)
    {
        auto s = esp_camera_sensor_get();
        if (s != nullptr)
        {
            s->set_vflip(s, persistent_flags & 1);
            s->set_hmirror(s, (persistent_flags & 2) != 0);
        }

    }


    ota_mark_valid();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    sntp_set_time_sync_notification_cb([](timeval *tv)
        { 
            struct tm t;
            localtime_r(&tv->tv_sec, &t);
            char buf[32];
            ESP_LOGI(TAG, "Time synchronized %s", asctime_r(&t, buf));

        });

    //int ret = xTaskCreatePinnedToCore(thread_routine, name, stacksize, arg, prio, thread, core_id);

    start_webserver();
}

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t still_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "still_httpd req: %d", httpd_req_to_sockfd(req));
    int64_t fr_start = esp_timer_get_time();

    camera_fb_t * fb = esp_camera_fb_get();
    if (fb == nullptr) 
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    auto res = httpd_resp_set_type(req, "image/jpeg");
    if(res == ESP_OK)
    {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }
    if(res == ESP_OK)
    {
        res = httpd_resp_set_hdr(req, "Cache-control", "no-cache");
    }
    res = httpd_resp_set_hdr(req, "Connection", "close");

    size_t fb_len = 0;
    if(res == ESP_OK)
    {
        if(fb->format == PIXFORMAT_JPEG)
        {
            fb_len = fb->len;
            ESP_LOGI(TAG, "jpeg fb %d", fb->len);
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        }
        else
        {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
            fb_len = jchunk.len;
        }
    }

    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %uKB %ums", (uint32_t)(fb_len/1024), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

struct async_frame_resp
{
    httpd_handle_t hd;
    int fd;
    int64_t last_frame_time;
};

static esp_err_t send_all(async_frame_resp *afr, const char *buf, ssize_t buf_len)
{
    while (buf_len > 0)
    {
        int len = httpd_socket_send(afr->hd, afr->fd, buf, buf_len, 0);

        if (len < 0)
        {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
        buf += len;
        buf_len -= len;
    }

    return ESP_OK;
}

static esp_err_t send_chunk(async_frame_resp *afr, const char *buf, ssize_t buf_len)
{
    char len_str[10];
    snprintf(len_str, sizeof(len_str), "%x\r\n", buf_len);
    if (send_all(afr, len_str, strlen(len_str)) != ESP_OK)
    {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    if (buf != nullptr)
    {
        if (send_all(afr, buf, (size_t) buf_len) != ESP_OK)
        {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
    }

    /* Indicate end of chunk */
    if (send_all(afr, "\r\n", 2) != ESP_OK)
    {
        return ESP_ERR_HTTPD_RESP_SEND;
    }
    return ESP_OK;
}

static void send_next_frame(void *data)
{
    async_frame_resp *afr = static_cast<async_frame_resp *>(data);
    ESP_LOGI(TAG, "stream next frame: %d", afr->fd);

    camera_fb_t * fb = esp_camera_fb_get();
    if (fb == nullptr)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        free(afr);
        return;
    }

    size_t _jpg_buf_len;
    uint8_t * _jpg_buf;

    if (fb->format != PIXFORMAT_JPEG)
    {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        if(!jpeg_converted)
        {
            ESP_LOGE(TAG, "JPEG compression failed");
            esp_camera_fb_return(fb);
            free(afr);
            return;
        }
    }
    else 
    {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
    }

    int64_t send_start = esp_timer_get_time();

    char part_buf[128];
    auto res = send_chunk(afr, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    if (res == ESP_OK)
    {
        size_t hlen = snprintf(part_buf, 64, _STREAM_PART, _jpg_buf_len);

        res = send_chunk(afr, part_buf, hlen);
    }

    if(res == ESP_OK)
    {
        res = send_chunk(afr, (const char *)_jpg_buf, _jpg_buf_len);
    }

    int64_t send_end = esp_timer_get_time();
    int64_t send_time = send_end - send_start;
    int64_t rate = (_jpg_buf_len * 1000000ll) / send_time;
    ESP_LOGI(TAG, "Send rate %d bps %d in %d", (int32_t)rate, _jpg_buf_len, (int32_t)send_time);
    if(fb->format != PIXFORMAT_JPEG)
    {
        free(_jpg_buf);
    }

    esp_camera_fb_return(fb);

    if (res != ESP_OK)
    {
        ESP_LOGI(TAG, "Send err %d", (int)res);
        free(afr);
        return;
    }

    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - afr->last_frame_time;
    afr->last_frame_time = fr_end;
    frame_time /= 1000;
    ESP_LOGI(TAG, "MJPG: fd %d: %uKB %ums (%.1ffps)", afr->fd, (uint32_t)(_jpg_buf_len/1024), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    httpd_queue_work(afr->hd, send_next_frame, afr);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "stream2 req: %d", httpd_req_to_sockfd(req));
    auto *afr = static_cast<async_frame_resp*>(malloc(sizeof(struct async_frame_resp)));
    afr->hd = req->handle;
    afr->fd = httpd_req_to_sockfd(req);
    afr->last_frame_time = esp_timer_get_time();
    if (afr->fd < 0) 
    {
        return ESP_FAIL;
    }

    const char *httpd_hdr_str = "HTTP/1.1 200 ok\r\nContent-Type: " _STREAM_CONTENT_TYPE "\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    auto res = send_all(afr, httpd_hdr_str, strlen(httpd_hdr_str));
    if (res != ESP_OK)
    {
        ESP_LOGI(TAG, "send header failed : %d", res);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Queuing work fd : %d", afr->fd);
    res = httpd_queue_work(req->handle, send_next_frame, afr);
    if (res != ESP_OK)
    {
       ESP_LOGI(TAG, "Queuing work failed : %d", res);
    }
    return ESP_OK;
}

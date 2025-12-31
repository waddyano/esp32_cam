/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "camera.h"
#include "favicon.h"
#include "httpd_util.h"
#include "index.h"
#include "rom/gpio.h"
#include "lwip/sockets.h"
#include "ota.h"
#include "sse.h"
#include "status.h"
#include "temp.h"
#include "wifi.h"

#include <esp_http_server.h>

const gpio_num_t LED_PIN = GPIO_NUM_21; //GPIO_NUM_4;
static bool led_state = false;

#define TAG "camera"
#define PART_BOUNDARY "123456789000000000000987654321"

#define _STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" PART_BOUNDARY

static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req);
static esp_err_t still_handler(httpd_req_t *req);

static uint32_t persistent_flags;
char camera_name[32];

void status_extra_info_function(int (*printfn)(const char *format, ...))
{
    auto s = esp_camera_sensor_get();
    if (s != nullptr)
    {
        printfn("Camera: vflip %d hflip %d\n", s->status.vflip, s->status.hmirror);
    }

    if (camera_name[0] != '\0')
    {
        printfn("Camera: name %s\n", camera_name);
    }

    float temp = temp_read();
    if (temp > 0)
    {
        printfn("Chip temperature %3.1f C\n", temp);
    }
}

static esp_err_t get_saved_prefs()
{
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open("camera", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "no saved flags, err %d\n", err);
        return err;
    }

    persistent_flags = 0;
    err = nvs_get_u32(nvs_handle, "flags", &persistent_flags);
    ESP_LOGI(TAG, "read saved flags %lu, err %d", persistent_flags, err);
    size_t namelen = sizeof(camera_name);
    camera_name[0] = '\0';
    nvs_get_str(nvs_handle, "name", camera_name, &namelen);
    ESP_LOGI(TAG, "read saved name %s, err %d", camera_name, err);
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
    ESP_LOGI(TAG, "save saved flags %lu, err %d", persistent_flags, err);
    nvs_close(nvs_handle);
    return ESP_OK;
}

static esp_err_t set_saved_name()
{
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open("camera", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(nvs_handle, "name", camera_name);
    ESP_LOGI(TAG, "save saved name %s, err %d", camera_name, err);
    nvs_close(nvs_handle);
    return ESP_OK;
}

static void set_led(bool on)
{
    led_state = on;
    gpio_set_level(LED_PIN, (int)led_state);
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

static esp_err_t led_handler(httpd_req_t *req)
{
    set_led(!led_state);
    const char *state = led_state ? "on" : "off";
    ESP_LOGI(TAG, "LED is now %s", state);
    sse_broadcast("led", state, strlen(state));
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t name_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, "text/plain");
    if (res != ESP_OK)
    {
        return res;
    }
    return httpd_resp_send(req, camera_name, HTTPD_RESP_USE_STRLEN);
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
                else if (strcmp(param, "vga") == 0)
                {
                    s->set_framesize(s, FRAMESIZE_VGA);
                }
                else if (strcmp(param, "xga") == 0)
                {
                    s->set_framesize(s, FRAMESIZE_XGA);
                }
                else if (strcmp(param, "cif") == 0)
                {
                    s->set_framesize(s, FRAMESIZE_CIF);
                }
            }
            else if (httpd_query_key_value(buf, "vflip", param, sizeof(param)) == ESP_OK) 
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
            else if (httpd_query_key_value(buf, "hflip", param, sizeof(param)) == ESP_OK) 
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
            else if (httpd_query_key_value(buf, "name", param, sizeof(param)) == ESP_OK) 
            {
                ESP_LOGI(TAG, "Found URL query parameter => name=%s", param);
                if (strlen(param) < sizeof(camera_name))
                {
                    strcpy(camera_name, param);
                    set_saved_name();
                }
            }
            else if (httpd_query_key_value(buf, "contrast", param, sizeof(param)) == ESP_OK) 
            {
                ESP_LOGI(TAG, "Found URL query parameter => contrast=%s", param);
                int level = atoi(param);
                s->set_contrast(s, level);
            }
            else if (httpd_query_key_value(buf, "brightness", param, sizeof(param)) == ESP_OK) 
            {
                ESP_LOGI(TAG, "Found URL query parameter => brightness=%s", param);
                int level = atoi(param);
                s->set_brightness(s, level);
            }
            else if (httpd_query_key_value(buf, "effect", param, sizeof(param)) == ESP_OK) 
            {
                ESP_LOGI(TAG, "Found URL query parameter => effect=%s", param);
                int effect = atoi(param);
                s->set_special_effect(s, effect);
            }
        }
        free(buf);
    }    
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, "text/html");
    if(res != ESP_OK){
        return res;
    }
    res = httpd_resp_set_hdr(req, "Connection", "close");
    const char *head = get_index_page_head();
    res = httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);
    if (res != ESP_OK)
    {
        return res;
    }
    const char *body = index_page_body;
	res = httpd_resp_send_chunk(req, body, HTTPD_RESP_USE_STRLEN);
    if (res != ESP_OK)
    {
        return res;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static void server_close_fn(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "client closed %d", sockfd);
    sse_remove_sink(sockfd);
    ESP_LOGI(TAG, "sink removed %d", sockfd);
    int err = close(sockfd);
    ESP_LOGI(TAG, "close stat %d", err);
}

static httpd_handle_t start_webserver(void)
{
    //esp_log_level_set("lwip", ESP_LOG_DEBUG);  
    esp_log_level_set("httpd", ESP_LOG_DEBUG);  
    //esp_log_level_set("httpd_parse", ESP_LOG_DEBUG);  
    sse_init();
    
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 32;
    config.core_id = 0;
    config.close_fn = server_close_fn;

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

        status_add_endpoints(server);
        favicon_add_endpoint(server);

        httpd_uri_t stream{};
        stream.uri       = "/stream";
        stream.method    = HTTP_GET;
        stream.handler   = stream_handler;
        httpd_register_uri_handler(server, &stream);

        httpd_uri_t events{};
        events.uri       = "/events";
        events.method    = HTTP_GET;
        events.handler   = sse_handler;
        httpd_register_uri_handler(server, &events);

        httpd_uri_t still{};
        still.uri       = "/still";
        still.method    = HTTP_GET,
        still.handler   = still_handler,
        httpd_register_uri_handler(server, &still);

        httpd_uri_t name{};
        name.uri       = "/name";
        name.method    = HTTP_GET,
        name.handler   = name_handler,
        httpd_register_uri_handler(server, &name);

        ota_add_endpoints(server);

        httpd_uri_t update_config{};
        update_config.uri	  = "/config";
        update_config.method   = HTTP_GET;
        update_config.handler  = config_handler;
        httpd_register_uri_handler(server, &update_config);

        httpd_uri_t led_config{};
        led_config.uri	  = "/led";
        led_config.method   = HTTP_GET;
        led_config.handler  = led_handler;
        httpd_register_uri_handler(server, &led_config);

        httpd_uri_t log_config{};
        log_config.uri	  = "/log";
        log_config.method   = HTTP_GET;
        log_config.handler  = log_handler;
        httpd_register_uri_handler(server, &log_config);

        #if CONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(server);
        #endif
        ESP_LOGI(TAG, "Registered URI handlers");
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

extern "C" void app_main(void)
{
    gpio_pad_select_gpio(LED_PIN); 
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT); 
    set_led(true);
    status_print_info(printf);

    ESP_LOGI(TAG, "init flash");
    esp_err_t err =  nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG, "inited flash");

    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "start up wifi");
    wifi_init_sta("esp_camera", false);

    ESP_LOGI(TAG, "start up camera");
    camera_init();

    if (get_saved_prefs() == ESP_OK)
    {
        auto s = esp_camera_sensor_get();
        if (s != nullptr)
        {
            s->set_vflip(s, persistent_flags & 1);
            s->set_hmirror(s, (persistent_flags & 2) != 0);
        }
    }

    ota_mark_valid();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    sntp_set_time_sync_notification_cb([](timeval *tv)
        { 
            struct tm t;
            localtime_r(&tv->tv_sec, &t);
            char buf[32];
            asctime_r(&t, buf);
            size_t len = strlen(buf);
            if (len > 0)
            {
                buf[len - 1] = '\0';
            }
            ESP_LOGI(TAG, "Time synchronized %s", buf);
            if (status_first_sntp_sync.tv_sec == 0)
            {
                status_first_sntp_sync.tv_sec = tv->tv_sec;
            }

        });

    //int ret = xTaskCreatePinnedToCore(thread_routine, name, stacksize, arg, prio, thread, core_id);

    set_led(false);
    temp_init();
    httpd_handle_t handle = start_webserver();
    ESP_LOGI(TAG, "started webserver %p", handle);
    for (;;)
    {
        wifi_reconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
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

static const uint8_t jfif_header[] = 
{
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00,  //      0: ......JFIF......
    0x00, 0x00, 0x00, 0x00                                                                           //     10: ....
};

static const uint8_t exif_header[] =
{
    0xff, 0xd8, 0xff, 0xe1, 0x00, 0x86, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00, 0x49, 0x49, 0x2a, 0x00,  //      0: ......Exif..II*.
    0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x32, 0x01, 0x02, 0x00, 0x14, 0x00, 0x00, 0x00, 0x26, 0x00,  //     10: ......2.......&.
    0x00, 0x00, 0x69, 0x87, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3a, 0x00, 0x00, 0x00, 0x60, 0x00,  //     20: ..i.......:...`.
    0x00, 0x00, 0x32, 0x30, 0x32, 0x32, 0x3a, 0x30, 0x36, 0x3a, 0x31, 0x30, 0x20, 0x32, 0x31, 0x3a,  //     30: ..2022:06:10 21:
    0x32, 0x34, 0x3a, 0x35, 0x31, 0x00, 0x01, 0x00, 0x03, 0x90, 0x02, 0x00, 0x14, 0x00, 0x00, 0x00,  //     40: 24:51...........
    0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x30, 0x32, 0x32, 0x3a, 0x30, 0x36, 0x3a,  //     50: L.......2022:06:
    0x31, 0x30, 0x20, 0x32, 0x31, 0x3a, 0x32, 0x34, 0x3a, 0x35, 0x31, 0x00, 0x02, 0x00, 0x01, 0x02,  //     60: 10 21:24:51.....
    0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x02, 0x02, 0x04, 0x00, 0x01, 0x00,  //     70: ......~.........
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00                                       //     80: ..........
};

static esp_err_t send_jpeg_as_exif(httpd_handle_t hd, int fd, const char *buf, ssize_t buf_len)
{
    char tmp_header[sizeof(exif_header)];
    memcpy(tmp_header, exif_header, sizeof(exif_header));
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    if (snprintf(tmp_header + 50, 20, "%4d:%02d:%02d %02d:%02d:%02d", 1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec) > 0)
    {
        ESP_LOGI(TAG, "image timestamp %s", tmp_header + 50);
    }
    memcpy(tmp_header + 88, tmp_header + 50, 20);
    esp_err_t res = socket_send_all(hd, fd, tmp_header, sizeof(exif_header));
    if (res == ESP_OK)
    {
        res = socket_send_all(hd, fd, buf + sizeof(jfif_header), buf_len - sizeof(jfif_header));
    }
    return res;
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
    if (res == ESP_OK)
    {
        res = httpd_resp_set_hdr(req, "Connection", "close");
    }

    unsigned int fb_len = 0;
    if(res == ESP_OK)
    {
        if(fb->format == PIXFORMAT_JPEG)
        {
            fb_len = fb->len;
            // A bit of a hack having seen the source of http_resp_send. If you pass in a non-zero length but a null buffer
            // all the headers are sent for you including Content-length but none of the body allowing sending of the body here without
            // copying everything out of the fb buffer as we only need to change the header from jfif to exif
            if (fb->len > sizeof(jfif_header) && memcmp(fb->buf, jfif_header, sizeof(jfif_header)) == 0)
            {
                int new_len = fb->len + sizeof(exif_header) - sizeof(jfif_header);
                res = httpd_resp_send(req, nullptr, new_len);
                if (res == ESP_OK)
                {
                    res = send_jpeg_as_exif(req->handle, httpd_req_to_sockfd(req), (const char *)fb->buf, fb->len);
                }
                else
                {
                    ESP_LOGI(TAG, "jpeg header send status %d", res);
                }
            }
            else
            {
                res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
            }
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
    ESP_LOGI(TAG, "JPG: %ub %lums - res %d", fb_len, (uint32_t)((fr_end - fr_start)/1000), res);
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
    return socket_send_all(afr->hd, afr->fd, buf, buf_len);
}

static unsigned int n_frames;

unsigned int camera_get_frame_count(bool reset)
{
    unsigned int res = n_frames;

    if (reset)
    {
        n_frames = 0;
    }

    return res;
}

static void send_next_frame(void *data)
{
    async_frame_resp *afr = static_cast<async_frame_resp *>(data);
    int64_t grab_start = esp_timer_get_time();
    ESP_LOGI(TAG, "@%lld: stream next frame: %d on %d", grab_start / 1000, afr->fd, xPortGetCoreID());

    camera_fb_t * fb = esp_camera_fb_get();
    if (fb == nullptr)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        free(afr);
        return;
    }

    ++n_frames;

    size_t jpg_buf_len;
    uint8_t * jpg_buf;

    if (fb->format != PIXFORMAT_JPEG)
    {
        bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
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
        jpg_buf_len = fb->len;
        jpg_buf = fb->buf;
    }

    ESP_LOGI(TAG, "frame length: %zd", jpg_buf_len);

    int64_t send_start = esp_timer_get_time();

    char part_buf[128];
    auto res = socket_send_chunk(afr->hd, afr->fd, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    if (res == ESP_OK)
    {
        size_t hlen = snprintf(part_buf, 64, _STREAM_PART, jpg_buf_len);

        res = socket_send_chunk(afr->hd, afr->fd, part_buf, hlen);
    }

    if (res == ESP_OK)
    {
        if (jpg_buf_len > sizeof(jfif_header) && memcmp(jpg_buf, jfif_header, sizeof(jfif_header)) == 0)
        {
            int new_len = jpg_buf_len + sizeof(exif_header) - sizeof(jfif_header);
            char len_str[10];
            snprintf(len_str, sizeof(len_str), "%x\r\n", new_len);
            res = send_all(afr, len_str, strlen(len_str));
            if (res == ESP_OK)
            {
                res = send_jpeg_as_exif(afr->hd, afr->fd, (const char *)jpg_buf, jpg_buf_len);
            }
            if (res == ESP_OK)
            {
                res = send_all(afr, "\r\n", 2); // end of chunk
            }
        }
        else
        {
            res = socket_send_chunk(afr->hd, afr->fd, (const char *)jpg_buf, jpg_buf_len);
        }
    }

    int64_t send_end = esp_timer_get_time();
    int64_t send_time = send_end - send_start;
    int64_t rate = (jpg_buf_len * 1000000ll) / send_time;
    ESP_LOGI(TAG, "@%lld: Send rate %ld bps %d in %ld.%03ld ms", send_end / 1000, (int32_t)rate, jpg_buf_len, (int32_t)(send_time / 1000), (int32_t)(send_time % 1000));
    if (fb->format != PIXFORMAT_JPEG)
    {
        free(jpg_buf);
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
    ESP_LOGI(TAG, "MJPG: fd %d: %luKB %lums (%.1ffps)", afr->fd, (uint32_t)(jpg_buf_len/1024), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    res = httpd_queue_work(afr->hd, send_next_frame, afr);
    ESP_LOGI(TAG, "Queue work err %d", (int)res);
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
        free(afr);
        return ESP_FAIL;
    }

    int one = 1;
    setsockopt(afr->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    const char *httpd_hdr_str = "HTTP/1.1 200 ok\r\nContent-Type: " _STREAM_CONTENT_TYPE "\r\nTransfer-Encoding: chunked\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
    auto res = send_all(afr, httpd_hdr_str, strlen(httpd_hdr_str));
    if (res != ESP_OK)
    {
        ESP_LOGI(TAG, "send header failed : %d", res);
        free(afr);
        return res;
    }

    ESP_LOGI(TAG, "Queuing work fd : %d", afr->fd);
    res = httpd_queue_work(req->handle, send_next_frame, afr);
    if (res != ESP_OK)
    {
        free(afr);
        ESP_LOGI(TAG, "Queuing work failed : %d", res);
    }
    return res;
}

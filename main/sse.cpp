#include <esp_log.h>
#include "camera.h"
#include "httpd_util.h"
#include "sse.h"
#include "temp.h"
#include <time.h>

#include "freertos/timers.h"
#include "freertos/semphr.h"

struct async_event_resp
{
    httpd_handle_t hd;
    int fd;
};

#define TAG "sse"

static SemaphoreHandle_t mutex = nullptr;
static async_event_resp *sinks[CONFIG_LWIP_MAX_SOCKETS];

esp_err_t sse_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "sse req: %d", httpd_req_to_sockfd(req));
    auto *aer = static_cast<async_event_resp*>(malloc(sizeof(struct async_event_resp)));
    aer->hd = req->handle;
    aer->fd = httpd_req_to_sockfd(req);
    if (aer->fd < 0) 
    {
        free(aer);
        return ESP_FAIL;
    }

    const char *httpd_hdr_str = "HTTP/1.1 200 ok\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
    auto res = socket_send_all(req->handle, aer->fd, httpd_hdr_str, strlen(httpd_hdr_str));
    if (res != ESP_OK)
    {
        ESP_LOGI(TAG, "send header failed : %d", res);
        free(aer);
        return res;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; ++i)
    {
        if (sinks[i] == nullptr)
        {
            sinks[i] = aer;
            break;
        }
    }
    xSemaphoreGive(mutex);

    //ESP_LOGI(TAG, "Queuing work fd : %d", aer->fd);
    //res = httpd_queue_work(req->handle, send_next_event, aer);

    return res;
}

void sse_remove_sink(int fd)
{
    bool found = false;
    async_event_resp *aer = nullptr;

    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; ++i)
    {
        if (sinks[i] != nullptr && sinks[i]->fd == fd)
        {
            aer = sinks[i];
            sinks[i] = nullptr;
            found = true;
            break;
        }
    }
    xSemaphoreGive(mutex);
    if (found)
    {
        free(aer);
    }
}

static const char heartbeat_msg[] = ": heartbeat\n\n";

static void send_heartbeat(void *data)
{
    async_event_resp *aer = static_cast<async_event_resp *>(data);
    ESP_LOGI(TAG, "send heartbeat: %d", aer->fd);

    auto res = socket_send_chunk(aer->hd, aer->fd, heartbeat_msg, sizeof(heartbeat_msg) - 1);
    if (res != ESP_OK)
    {
        ESP_LOGI(TAG, "send heartbeat failed %d", res);
        sse_remove_sink(aer->fd);
    }
}

static char *current_broadcast = nullptr;
static int sse_id = 0;

static void send_message(void *data)
{
    async_event_resp *aer = static_cast<async_event_resp *>(data);
    ESP_LOGI(TAG, "send message to: %d: %s", aer->fd, current_broadcast);

    auto res = socket_send_chunk(aer->hd, aer->fd, current_broadcast, strlen(current_broadcast));
    if (res != ESP_OK)
    {
        ESP_LOGI(TAG, "send message failed %d", res);
        sse_remove_sink(aer->fd);
    }
}

void sse_broadcast(const char *type, const char *data, unsigned int len)
{
    ESP_LOGD(TAG, "Broadcast message \"%s\" length %u", type, len);
    const char *msg = "event: %s\nid: %d\ndata: %.*s\n\n";

    size_t buflen = strlen(msg) + strlen(type) + len + 12;
    free(current_broadcast);
    current_broadcast = (char *)malloc(buflen);
    ++sse_id;
    snprintf(current_broadcast, buflen, msg, type, sse_id, len, data);
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; ++i)
    {
        if (sinks[i] != nullptr)
        {
            esp_err_t res = httpd_queue_work(sinks[i]->hd, send_message, sinks[i]);
            if (res != ESP_OK)
            {
                ESP_LOGI(TAG, "Queuing sse broadcast work failed : %d", res);
            }
        }
    }
    xSemaphoreGive(mutex);
}

static void time_ticker(TimerHandle_t arg)
{
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    char buf[128];
    unsigned int frames = camera_get_frame_count(true);

    float celsius = temp_read();
    if (snprintf(buf, sizeof(buf), "{ \"time\": \"%4d:%02d:%02d-%02d:%02d:%02d\", \"frames\": %u, \"tempc\": %.2g }", 
                1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, frames, celsius) > 0)
    {
        sse_broadcast("status", buf, strlen(buf));
    }
}

static void ticker(TimerHandle_t arg)
{
    ESP_LOGD(TAG, "Tick, stack left %d", uxTaskGetStackHighWaterMark(nullptr));
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; ++i)
    {
        if (sinks[i] != nullptr)
        {
            esp_err_t res = httpd_queue_work(sinks[i]->hd, send_heartbeat, sinks[i]);
            if (res != ESP_OK)
            {
                ESP_LOGI(TAG, "Queuing work failed : %d", res);
            }
        }
    }
    xSemaphoreGive(mutex);
    ESP_LOGD(TAG, "End tick, stack left %d", uxTaskGetStackHighWaterMark(nullptr));
}

esp_err_t sse_init()
{
    mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "initializing timers");
    auto handle = xTimerCreate("heartbeat timer", pdMS_TO_TICKS(15000), pdTRUE, nullptr, ticker);
    ESP_LOGI(TAG, "initialized timer %p", handle);
    auto stat = xTimerStart(handle, 0);
    ESP_LOGI(TAG, "timer started %d", stat);
    auto time_handle = xTimerCreate("time timer", pdMS_TO_TICKS(1000), pdTRUE, nullptr, time_ticker);
    ESP_LOGI(TAG, "initialized time timer %p", handle);
    stat = xTimerStart(time_handle, 0);
    ESP_LOGI(TAG, "time timer started %d", stat);
    return ESP_OK;
}
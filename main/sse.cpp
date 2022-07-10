#include <esp_log.h>
#include "httpd_util.h"
#include "sse.h"

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

    const char *httpd_hdr_str = "HTTP/1.1 200 ok\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n";
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

static void ticker(TimerHandle_t arg)
{
    ESP_LOGI(TAG, "Tick");
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
}

esp_err_t sse_init()
{
    ESP_LOGI(TAG, "Initializing timer");
    auto handle = xTimerCreate("heartbeat timer", pdMS_TO_TICKS(15000), pdTRUE, nullptr, ticker);
    ESP_LOGI(TAG, "Initialized timer %p", handle);
    auto stat = xTimerStart(handle, 0);
    ESP_LOGI(TAG, "timer started %d", stat);
    mutex = xSemaphoreCreateMutex();

    return ESP_OK;
}
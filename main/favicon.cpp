#include "esp_log.h"

#include "favicon.h"
#include "favicon_image.h"

#define TAG "favicon"

static esp_err_t favicon_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "status req: %p", * (void **)req->aux);
    esp_err_t res = httpd_resp_set_type(req, "image/x-icon");
    if(res != ESP_OK) 
    {
        return res;
    }
    res = httpd_resp_set_hdr(req, "Connection", "close");
    if(res != ESP_OK) 
    {
        return res;
    }
	return httpd_resp_send(req, favicon_image, sizeof(favicon_image));
}

extern void favicon_add_endpoint(httpd_handle_t server)
{
    httpd_uri_t fav{};
    fav.uri       = "/favicon.ico";
    fav.method    = HTTP_GET;
    fav.handler   = favicon_handler;
    httpd_register_uri_handler(server, &fav);
}


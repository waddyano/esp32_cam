#pragma once

#include "esp_http_server.h"

esp_err_t sse_handler(httpd_req_t *req);
esp_err_t sse_init();
void sse_remove_sink(int fd);



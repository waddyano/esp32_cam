#pragma once

#include "esp_http_server.h"

esp_err_t sse_handler(httpd_req_t *req);
esp_err_t sse_init();
bool sse_remove_sink(int fd);
void sse_broadcast(const char *type, const char *data, unsigned int len);



#pragma once

#include "esp_http_server.h"

esp_err_t socket_send_all(httpd_handle_t hd, int fd, const char *buf, ssize_t buf_len);
esp_err_t socket_send_chunk(httpd_handle_t hd, int fd, const char *buf, ssize_t buf_len);


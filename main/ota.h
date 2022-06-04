#pragma once

#include "esp_http_server.h"

extern void start_ota(void);
esp_err_t update_post_handler(httpd_req_t *req);
extern void ota_mark_valid();

extern void send_reboot_page(httpd_req_t *req, const char *msg);

extern const char *update_page;



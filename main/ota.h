#pragma once

#include "esp_http_server.h"

extern void ota_add_endpoints(httpd_handle_t server);
extern void ota_start(void);
extern void ota_mark_valid();
extern void ota_send_reboot_page(httpd_req_t *req, const char *msg);



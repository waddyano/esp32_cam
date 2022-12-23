#pragma once

#include "esp_http_server.h"

extern struct timeval status_first_sntp_sync;

// host app must provide this
extern void status_extra_info_function(int (*printfn)(const char *format, ...));

extern void status_print_info(int (*printfn)(const char *format, ...));
extern void status_add_endpoints(httpd_handle_t server);


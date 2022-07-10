#include "httpd_util.h"

esp_err_t socket_send_all(httpd_handle_t hd, int fd, const char *buf, ssize_t buf_len)
{
    while (buf_len > 0)
    {
        int len = httpd_socket_send(hd, fd, buf, buf_len, 0);

        if (len < 0)
        {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
        buf += len;
        buf_len -= len;
    }

    return ESP_OK;
}

esp_err_t socket_send_chunk(httpd_handle_t hd, int fd, const char *buf, ssize_t buf_len)
{
    char len_str[10];
    snprintf(len_str, sizeof(len_str), "%x\r\n", buf_len);
    if (socket_send_all(hd, fd, len_str, strlen(len_str)) != ESP_OK)
    {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    if (buf != nullptr)
    {
        if (socket_send_all(hd, fd, buf, (size_t) buf_len) != ESP_OK)
        {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
    }

    /* Indicate end of chunk */
    if (socket_send_all(hd, fd, "\r\n", 2) != ESP_OK)
    {
        return ESP_ERR_HTTPD_RESP_SEND;
    }
    return ESP_OK;
}




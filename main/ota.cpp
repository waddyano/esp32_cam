/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"

#include "ota.h"

//#include <sys/socket.h>

#define HASH_LEN 32

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
/* The interface name value can refer to if_desc in esp_netif_defaults.h */
#if CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF_ETH
static const char *bind_interface_name = "eth";
#elif CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF_STA
static const char *bind_interface_name = "sta";
#endif
#endif

static const char *TAG = "ota";
//extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
//extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#define OTA_URL_SIZE 256

static char title[64] = "Firmware Update";

// Note this has two %s substitutions bot for the title
static const char *update_page = R"!(<html>
	<head>
		<meta http-equiv="content-type" content="text/html; charset=utf-8" />
		<title>%s</title>
		<script>
function startUpload() {
    var otafile = document.getElementById("otafile").files;

    if (otafile.length == 0) {
        alert("No file selected!");
    } else {
        document.getElementById("otafile").disabled = true;
        document.getElementById("upload").disabled = true;

        var file = otafile[0];
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function() {
            if (xhr.readyState == 4) {
                if (xhr.status == 200) {
                    document.open();
                    document.write(xhr.responseText);
                    document.close();
                } else if (xhr.status == 0) {
                    alert("Server closed the connection abruptly!");
                    location.reload()
                } else {
                    alert(xhr.status + " Error!\n" + xhr.responseText);
                    location.reload()
                }
            }
        };

        xhr.upload.onprogress = function (e) {
            let percent = (e.loaded / e.total * 100).toFixed(0);
            let progress2 = document.getElementById("progress2");
            progress2.textContent = "" + percent + "%";
            let progress = document.getElementById("progress");
            progress.value = percent;
            progress.textContent = "" + percent + "%";
        };
        xhr.open("POST", "/post_update", true);
        xhr.send(file);
    }
}
		</script>
<style>
body, textarea, label, button, input[type=file] {font-family: arial, sans-serif;}
label, input[type=file] { line-height:2.4rem; font-size:1.2rem; }
input::file-selector-button, button { border: 0; border-radius: 0.3rem; background:#1fa3ec; color:#ffffff; line-height:2.4rem; font-size:1.2rem; width:180px;
-webkit-transition-duration:0.4s;transition-duration:0.4s;cursor:pointer;}
button:hover{background:#0b73aa;}
</style>
	</head>
	<body>
		<h1 style="text-align: center">%s</h1>
        <div style="margin-left:auto; margin-right: auto; display: table;">
            <div style="padding: 6px">
                <label for="otafile" class="file">Update firmware:&nbsp</label>
                <input type="file" id="otafile" name="otafile" />
            </div>
            <span>
                <button id="upload" type="button" onclick="startUpload()">Upload</button>
            </span><span>
            <progress id="progress" value="0" max="100"></progress></span>
            <span id="progress2"></span>
        </div>
	</body>
</html>)!";

#if 0
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}
#endif

static esp_err_t update_handler(httpd_req_t *req)
{
    size_t len = httpd_req_get_hdr_value_len(req, "X-Forwarded-Host");
    ESP_LOGI(TAG, "forward host len %zd", len);
    if (len != 0)
    {
        auto err = httpd_resp_set_status(req, "403 Not Authorized");
        if (err != ESP_OK)
        {
            return err;
        }
    	return httpd_resp_send(req, nullptr, 0);
    }

    esp_err_t res = httpd_resp_set_type(req, "text/html");
    if(res != ESP_OK)
    {
        return res;
    }

    res = httpd_resp_set_hdr(req, "Connection", "close");

    len = strlen(update_page) + 2 * strlen(title) + 1; // bit too big
    char *page = static_cast<char *>(malloc(len));
    snprintf(page, len, update_page, title, title);
	auto err = httpd_resp_send(req, page, strlen(page));
    free(page);
    return err;
}

#if 0
void simple_ota_example_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA example");
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
    esp_netif_t *netif = get_example_netif_from_desc(bind_interface_name);
    if (netif == NULL) {
        ESP_LOGE(TAG, "Can't find netif from interface description");
        abort();
    }
    struct ifreq ifr;
    esp_netif_get_netif_impl_name(netif, ifr.ifr_name);
    ESP_LOGI(TAG, "Bind interface name is %s", ifr.ifr_name);
#endif
    esp_http_client_config_t http_config{};
    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;
    http_config.url = "http://192.168.10.223:9090/quack.bin";
    //config.cert_pem = (char *)server_cert_pem_start;
    http_config.event_handler = _http_event_handler;
    http_config.keep_alive_enable = true;
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
        .if_name = &ifr,
#endif

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    vTaskDelete(nullptr);
}
#endif

static const char *hex = "0123456789abcdef";

static void format_sha256(const uint8_t *image_hash, char *formatted)
{
    for (int i = 0; i < HASH_LEN; ++i)
    {
        formatted[i * 2] = hex[(image_hash[i] >> 4) & 0xf];
        formatted[i * 2 + 1] = hex[image_hash[i] & 0xf];
    }
    formatted[2 * HASH_LEN] = '\0';
}

void ota_get_partition_hashes(char *boot_hash, char *current_partition_hash)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    format_sha256(sha_256, boot_hash);

    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    format_sha256(sha_256, current_partition_hash);
}

void ota_mark_valid()
{
	const esp_partition_t *partition = esp_ota_get_running_partition();
	printf("Currently running partition: %s\n", partition->label);

	esp_ota_img_states_t ota_state;
	if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK) {
		if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
			esp_ota_mark_app_valid_cancel_rollback();
		}
	}
}

void ota_send_reboot_page(httpd_req_t *req, const char *msg)
{
    httpd_resp_set_type(req, "text/html");
    const char *head = R"!(
<html>
<head>
	<title>Rebooting</title>
    <script>
        const id = setInterval(ping, 5000);
        let controller = null;

        function ping()
        {
            if (controller != null)
            {
                controller.abort();
            }
            controller = new AbortController();
            fetch('/', { signal: controller.signal })
                .then(response => response.text())
                .then(function(data) { clearInterval(id); window.location.href = '/'; })
                .catch((error) => { console.error('Error:', error); });
        };

        ping();
    </script>
<style>
body, p {font-family: arial, sans-serif;}
p { line-height:2.4rem; font-size:1.2rem; }
</style>
</head>
<body><p>
    )!";
	httpd_resp_sendstr_chunk(req, head);
	httpd_resp_sendstr_chunk(req, msg);
	httpd_resp_sendstr_chunk(req, "</p></body></html>\n");
	httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
	char buf[1000];
	esp_ota_handle_t ota_handle;
	int remaining = req->content_len;

    ESP_LOGI(TAG, "got update post request");
	const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
	ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

	while (remaining > 0) {
		int recv_len = httpd_req_recv(req, buf, std::min(remaining, (int)sizeof(buf)));

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			return ESP_FAIL;
		}

		// Successful Upload: Flash firmware chunk
		if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
			return ESP_FAIL;
		}

		remaining -= recv_len;
	}

	// Validate and switch to new OTA image and reboot
	if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
			return ESP_FAIL;
	}

    ota_send_reboot_page(req, "Firmware update complete, rebooting now!");

	vTaskDelay(500 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}

static esp_err_t restart_handler(httpd_req_t *req)
{
	ota_send_reboot_page(req, "Rebooting now!");

	vTaskDelay(500 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}

extern void ota_add_endpoints(httpd_handle_t server)
{
    httpd_uri_t update{};
    update.uri	  = "/update";
    update.method   = HTTP_GET;
    update.handler  = update_handler;
    httpd_register_uri_handler(server, &update);

    httpd_uri_t post_update{};
    post_update.uri	  = "/post_update";
    post_update.method   = HTTP_POST;
    post_update.handler  = update_post_handler;
    httpd_register_uri_handler(server, &post_update);

    httpd_uri_t restart{};
    restart.uri	  = "/restart";
    restart.method   = HTTP_GET;
    restart.handler  = restart_handler;
    httpd_register_uri_handler(server, &restart);
}

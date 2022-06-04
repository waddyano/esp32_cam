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

static const char *TAG = "simple_ota_example";
//extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
//extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#define OTA_URL_SIZE 256

const char *update_page = R"!(<html>
	<head>
		<meta http-equiv="content-type" content="text/html; charset=utf-8" />
		<title>ESP32 Camera Update</title>
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
						progress2.textContent = "Progress: " + percent + "%";
						let progress = document.getElementById("progress");
                        progress.value = percent;
						progress.textContent = "" + percent + "%";
					};
					xhr.open("POST", "/post_update", true);
					xhr.send(file);
				}
			}
		</script>
	</head>
	<body>
		<h1>ESP32 Camera Update</h1>
		<div>
			<label for="otafile">Firmware file:</label>
			<input type="file" id="otafile" name="otafile" />
		</div>
		<div>
			<button id="upload" type="button" onclick="startUpload()">Upload</button>
		</div>
        <progress id="progress" value="0" max="100"></progress>
		<div id="progress2"></div>
	</body>
</html>)!";

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
    }
    return ESP_OK;
}

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
    esp_http_client_config_t config{};
    config.url = "http://192.168.10.223:9090/quack.bin";
    //config.cert_pem = (char *)server_cert_pem_start;
    config.event_handler = _http_event_handler;
    config.keep_alive_enable = true;
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

    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    vTaskDelete(nullptr);
}

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

void ota_mark_valid()
{
	const esp_partition_t *partition = esp_ota_get_running_partition();
	printf("Currently running partition: %s\r\n", partition->label);

	esp_ota_img_states_t ota_state;
	if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK) {
		if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
			esp_ota_mark_app_valid_cancel_rollback();
		}
	}
}

void send_reboot_page(httpd_req_t *req, const char *msg)
{
    httpd_resp_set_type(req, "text/html");
    const char *head = R"!(
<html>
<head>
	<title>Rebooting</title>
    <script>
        setInterval(ping, 1000);

        function ping()
        {
            fetch('/').then(response => response.text()).then(function(data) { window.location.href = '/';});
        };
    </script>
</head>
<body><p>
    )!";
	httpd_resp_sendstr_chunk(req, head);
	httpd_resp_sendstr_chunk(req, msg);
	httpd_resp_sendstr_chunk(req, "</p></body></html>\n");
	httpd_resp_send_chunk(req, nullptr, 0);
}

/*
 * Handle OTA file upload
 */
esp_err_t update_post_handler(httpd_req_t *req)
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

    send_reboot_page(req, "Firmware update complete, rebooting now!");

	vTaskDelay(500 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}


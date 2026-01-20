#include <string.h>

#include <freertos/FreeRTOS.h>
#include <esp_http_server.h>
#include <freertos/task.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
//#include <nvs_flash.h>
#include <sys/param.h>
//#include <esp_wifi.h>
#include "esp_partition.h"
#include "esp_log.h"

#define WIFI_SSID "ESP32 OTA Update"

/*
 * Serve OTA update portal (index.html)
 */
static const char INDEX_HTML[] =
"<!DOCTYPE html>\n"
"<html>\n"
"  <head>\n"
"    <meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\" />\n"
"    <title>ESP32 OTA Update</title>\n"
"    <script>\n"
"      function startUpload() {\n"
"        var otafile = document.getElementById(\"otafile\").files;\n"
"\n"
"        if (otafile.length == 0) {\n"
"          alert(\"No file selected!\");\n"
"        } else {\n"
"          document.getElementById(\"otafile\").disabled = true;\n"
"          document.getElementById(\"upload\").disabled = true;\n"
"\n"
"          var file = otafile[0];\n"
"          var xhr = new XMLHttpRequest();\n"
"          xhr.onreadystatechange = function() {\n"
"            if (xhr.readyState == 4) {\n"
"              if (xhr.status == 200) {\n"
"                document.open();\n"
"                document.write(xhr.responseText);\n"
"                document.close();\n"
"              } else if (xhr.status == 0) {\n"
"                alert(\"Server closed the connection abruptly!\");\n"
"                location.reload();\n"
"              } else {\n"
"                alert(xhr.status + \" Error!\\n\" + xhr.responseText);\n"
"                location.reload();\n"
"              }\n"
"            }\n"
"          };\n"
"\n"
"          xhr.upload.onprogress = function (e) {\n"
"            var progress = document.getElementById(\"progress\");\n"
"            progress.textContent = \"Progress: \" + (e.loaded / e.total * 100).toFixed(0) + \"%\";\n"
"          };\n"
"          xhr.open(\"POST\", \"/update\", true);\n"
"          xhr.send(file);\n"
"        }\n"
"      }\n"
"    </script>\n"
"  </head>\n"
"  <body>\n"
"    <h1>ESP32 OTA Firmware Update</h1>\n"
"    <div>\n"
"      <label for=\"otafile\">Firmware file:</label>\n"
"      <input type=\"file\" id=\"otafile\" name=\"otafile\" />\n"
"    </div>\n"
"    <div>\n"
"      <button id=\"upload\" type=\"button\" onclick=\"startUpload()\">Upload</button>\n"
"    </div>\n"
"    <div id=\"progress\"></div>\n"
"  </body>\n"
"</html>\n";

static char buf[1000];

// esp_err_t index_get_handler(httpd_req_t *req)
// {
// 	httpd_resp_send(req, (const char *) index_html_start, index_html_end - index_html_start);
// 	return ESP_OK;
// }

esp_err_t index_get_handler(httpd_req_t *req)
{
    //httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

/*
 * Handle OTA file upload
 */
esp_err_t update_post_handler(httpd_req_t *req)
{
	esp_ota_handle_t ota_handle;
	int remaining = req->content_len;

	// const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
	// ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

	
	const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
	if (!ota_partition) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition (check partition table)");
		return ESP_FAIL;
	}

	ESP_LOGI("OTA", "Next update partition: label=%s subtype=0x%02x offset=0x%08lx size=0x%lx",
			ota_partition->label, ota_partition->subtype,
			(unsigned long)ota_partition->address, (unsigned long)ota_partition->size);

	esp_err_t err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
	if (err != ESP_OK) {
		ESP_LOGE("OTA", "esp_ota_begin failed: %s", esp_err_to_name(err));
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
		return ESP_FAIL;
	}


	while (remaining > 0) {
		int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			return ESP_FAIL;
		}

		// Successful Upload: Flash firmware chuESP_OKnk
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

	httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");

	vTaskDelay(500 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}

/*
 * HTTP Server
 */
httpd_uri_t index_get = {
	.uri	  = "/",
	.method   = HTTP_GET,
	.handler  = index_get_handler,
	.user_ctx = NULL
};

httpd_uri_t update_post = {
	.uri	  = "/update",
	.method   = HTTP_POST,
	.handler  = update_post_handler,
	.user_ctx = NULL
};

// static esp_err_t http_server_init(void)
// {
// 	static httpd_handle_t http_server = NULL;

// 	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

// 	if (httpd_start(&http_server, &config) == ESP_OK) {
// 		httpd_register_uri_handler(http_server, &index_get);
// 		httpd_register_uri_handler(http_server, &update_post);
// 	}

// 	return http_server == NULL ? ESP_FAIL : ESP_OK;
// }

static esp_err_t http_server_init(void)
{
    static httpd_handle_t http_server = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // provisioning SoftAP already uses an http server (port 80),
    // so pick another port for your OTA portal:
    config.server_port = 8080;

    // IMPORTANT: if you run more than one httpd instance, ctrl_port must also be unique
    config.ctrl_port = 32769;

    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &index_get);
        httpd_register_uri_handler(http_server, &update_post);
        return ESP_OK;
    }
    return ESP_FAIL;
}


/*
 * WiFi configuration
 */
// static esp_err_t softap_init(void)
// {
// 	esp_err_t res = ESP_OK;

// 	res |= esp_netif_init();
// 	res |= esp_event_loop_create_default();
// 	esp_netif_create_default_wifi_ap();

// 	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
// 	res |= esp_wifi_init(&cfg);

// 	wifi_config_t wifi_config = {
// 		.ap = {
// 			.ssid = WIFI_SSID,
// 			.ssid_len = strlen(WIFI_SSID),
// 			.channel = 6,
// 			.authmode = WIFI_AUTH_OPEN,
// 			.max_connection = 3
// 		},
// 	};

// 	res |= esp_wifi_set_mode(WIFI_MODE_AP);
// 	res |= esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
// 	res |= esp_wifi_start();

// 	return res;
// }


static void dump_app_partitions(void)
{
	esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
	while (it) {
		const esp_partition_t *p = esp_partition_get(it);
		printf("APP: label=%s subtype=0x%02x offset=0x%08lx size=0x%lx\n",
			p->label, p->subtype, (unsigned long)p->address, (unsigned long)p->size);
		it = esp_partition_next(it);
	}
}


void esp32_softap_ota(void) {
	// esp_err_t ret = nvs_flash_init();

	// if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	// 	ESP_ERROR_CHECK(nvs_flash_erase());
	// 	ret = nvs_flash_init();
	// }

	//ESP_ERROR_CHECK(ret);
	//ESP_ERROR_CHECK(softap_init());
	ESP_ERROR_CHECK(http_server_init());

	/* Mark current app as valid */
	const esp_partition_t *partition = esp_ota_get_running_partition();
	printf("Currently running partition: %s\r\n", partition->label);

	esp_ota_img_states_t ota_state;
	if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK) {
		if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
			esp_ota_mark_app_valid_cancel_rollback();
		}
	}

	dump_app_partitions();

	//while(1) vTaskDelay(10);
}

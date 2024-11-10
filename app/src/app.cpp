/**
 ******************************************************************************
 * @file        : app.cpp
 * @brief       : Application
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : Application
 ******************************************************************************
 */

#include "app.hpp"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mdns.h>
#include <nvs_flash.h>
#include <wifi_provisioning/manager.h>

#include <memory>

#include "cJSON.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "status_led.hpp"

static const char* kTag = "app";

App* App::instance_ = nullptr;
SemaphoreHandle_t App::semaphore_ = xSemaphoreCreateMutex();

#if defined CONFIG_SPIRAM && defined CONFIG_SPIRAM_USE_CAPS_ALLOC

static void* psram_malloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
static void psram_free(void* ptr) { heap_caps_free(ptr); }
#endif

App::App() {
    ESP_LOGI(kTag, "Creating App ...");
#if defined CONFIG_SPIRAM && defined CONFIG_SPIRAM_USE_CAPS_ALLOC

    cJSON_Hooks hooks = {.malloc_fn = psram_malloc, .free_fn = psram_free};
    cJSON_InitHooks(&hooks);
#endif

    // Initialize NVS partition
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Initialize TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize the event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize Wi-Fi including netif with default config
    wifi_ = esp_netif_create_default_wifi_sta();

    // Get Hostname from NVS "system:hostname" (if available)
    nvs_handle_t my_handle;
    char hostname[32];
    err = nvs_open("system", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        size_t size = sizeof(hostname);
        err = nvs_get_str(my_handle, "hostname", hostname, &size);
        if (err == ESP_OK) {
            ESP_LOGI(kTag, "Hostname : %s", hostname);
            err = esp_netif_set_hostname(wifi_, hostname);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "Failed to set hostname");
            }
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGW(kTag, "Failed to open NVS handle");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    httpd_ = Httpd::GetInstance();
    mqtt_ = MQTT::GetInstance();
    updater_ = Updater::GetInstance();
    prov_ = Provisioner::GetInstance();
}

App* App::GetInstance() {
    if (instance_ == nullptr) {
        xSemaphoreTake(semaphore_, portMAX_DELAY);
        if (instance_ == nullptr) {
            instance_ = new App();
        }
        xSemaphoreGive(semaphore_);
    }
    return instance_;
}

void App::Init(StatusLed* led) {
    ESP_LOGI(kTag, "Initializing App ...");

    led_ = led;
    if (led_ != nullptr) {
        led_->On(kRed);
        prov_->SetLed(led_);
        mqtt_->SetLed(led_);
    }

    AddRoute("/firmware-upgrade", HTTP_POST, DoFirmwareUpgrade, this);
    AddRoute("/reset", HTTP_POST, DoReset, this);
    AddRoute("/config/set-key", HTTP_POST, DoConfigSetKey, this);
    AddRoute("/config/get-key", HTTP_GET, DoConfigGetKey, this);
    AddRoute("/config/get-all", HTTP_GET, DoConfigGetAll, this);
    AddRoute("/config/delete-key", HTTP_DELETE, DoConfigDeleteKey, this);
    AddRoute("/config/delete-namespace", HTTP_DELETE, DoConfigDeleteNameSpace, this);
    AddRoute("/info", HTTP_GET, DoGetInfo, this);
}

esp_err_t App::PublishMessage(
    const char* topic, const char* data, bool prefixed, int qos, int retain) {
    if (prefixed) {
        return mqtt_->Publish(mqtt_->Prefixed(topic).c_str(), data, 0, qos, retain);
    } else {
        return mqtt_->Publish(topic, data, 0, qos, retain);
    }
}

void App::Provision(const char* country, const char* proof_of_possession) {
    if (led_ != nullptr) {
        led_->Blink(100, 200, kBlue);
    }

    xTaskCreate(ReprovionerTaskForwarder,
                "ReprovionerTask",
                4096,
                this,
                uxTaskPriorityGet(nullptr),
                nullptr);

    prov_->Provision(country, proof_of_possession);
    if (led_ != nullptr) {
        led_->On(kBlue);
    }
    char* wifi_hostname = nullptr;
    esp_err_t err = esp_netif_get_hostname(wifi_, (const char**)&wifi_hostname);
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "Hostname : %s", wifi_hostname);
        strncpy(hostname_, wifi_hostname, sizeof(hostname_));
    } else {
        ESP_LOGW(kTag, "Failed to get hostname : %s", esp_err_to_name(err));
    }
}

void App::ResetProvisioning() { wifi_prov_mgr_reset_provisioning(); }

void App::ReprovionerTask() {
    ESP_LOGI(kTag, "ReprovionerTask started");
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << GPIO_NUM_0;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    int64_t start_pressed = 0;
    int prev_btn_state = 1;

    while (1) {
        int btn_state = gpio_get_level(GPIO_NUM_0);
        if (btn_state == 0) {
            if (led_ != nullptr) {
                led_->Flash(200, 0, 1, kOrange);
            }
            if (prev_btn_state == 1) {
                ESP_LOGI(kTag, "Button pressed");
                start_pressed = esp_timer_get_time() / 1000;
            } else if ((esp_timer_get_time() / 1000) - start_pressed > 10000) {
                ESP_LOGI(kTag, "Starting reprovisioning");
                if (led_ != nullptr) {
                    led_->On(kOrange);
                }
                vTaskDelay(pdMS_TO_TICKS(2000));
                ResetProvisioning();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
        } else {
            if (prev_btn_state == 0) {
                ESP_LOGI(kTag, "Button released");
            }
        }
        prev_btn_state = btn_state;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t App::StartMdns(const char* name) {
    // initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(kTag, "MDNS Init failed: %d\n", err);
        return err;
    }

    err = mdns_hostname_set(hostname_);
    if (err) {
        ESP_LOGE(kTag, "MDNS Hostname set failed: %d\n", err);
        return err;
    }

    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    mdns_instance_name_set(name);
    return ESP_OK;
}

// ----- Web Services -----

esp_err_t App::DoFirmwareUpgrade(httpd_req_t* req) {
    const int kBufferSize = 4096;
    App* ctx = (App*)req->user_ctx;
#if defined CONFIG_SPIRAM && defined CONFIG_SPIRAM_USE_CAPS_ALLOC

    std::shared_ptr<char> buffer((char*)heap_caps_malloc(kBufferSize, MALLOC_CAP_SPIRAM),
                                 heap_caps_free);
#else
    std::shared_ptr<char> buffer((char*)malloc(kBufferSize), free);
#endif

    int res = ctx->httpd_->Receive(req, buffer.get(), kBufferSize);
    if (res < 0) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    } else if (res != req->content_len) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive all data");
        return ESP_FAIL;
    }

    std::shared_ptr<cJSON> json(cJSON_Parse(buffer.get()), cJSON_Delete);

    if (json.get() == nullptr) {
        ESP_LOGW(kTag, "Failed to parse JSON");
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGW(kTag, "Error before: %s\n", error_ptr);
        }

        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse JSON");
        return ESP_FAIL;
    }

    cJSON* url = cJSON_GetObjectItemCaseSensitive(json.get(), "url");
    if (cJSON_IsString(url) && (url->valuestring != nullptr)) {
        ESP_LOGI(kTag, "URL : \"%s\"\n", url->valuestring);
    } else {
        ESP_LOGW(kTag, "Failed to parse URL");
        return ESP_FAIL;
    }

    ctx->httpd_->Reply(req, "Firmware update started\n");
    if (ctx->updater_->Update(url->valuestring) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update firmware");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t App::DoReset(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;
    ctx->httpd_->Reply(req, "Resetting device\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    ctx->httpd_->Stop();
    esp_restart();
    return ESP_OK;
}

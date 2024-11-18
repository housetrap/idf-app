/**
 ******************************************************************************
 * @file        : firmware_updater.cpp
 * @brief       : Firmware Updater
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : Firmware Updater
 ******************************************************************************
 */

#include "firmware_updater.hpp"

#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>

#include <string>

static const char* kTag = "firmware_upgrade";

Updater* Updater::instance_ = nullptr;
SemaphoreHandle_t Updater::semaphore_ = xSemaphoreCreateMutex();

static esp_err_t HttpClientInitCallback(esp_http_client_handle_t client) {
    Updater* updater = Updater::GetInstance();
    for (const HttpHeader& header : updater->headers_) {
        esp_err_t ret =
            esp_http_client_set_header(client, header.key.c_str(), header.value.c_str());
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

Updater* Updater::GetInstance() {
    if (instance_ == nullptr) {
        xSemaphoreTake(semaphore_, portMAX_DELAY);
        if (instance_ == nullptr) {
            instance_ = new Updater();
        }
        xSemaphoreGive(semaphore_);
    }
    return instance_;
}

void Updater::EventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != ESP_HTTPS_OTA_EVENT) {
        return;
    }
    switch (event_id) {
        case ESP_HTTPS_OTA_START:
            ESP_LOGI(kTag, "OTA started");
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            ESP_LOGI(kTag, "Connected to server");
            break;
        case ESP_HTTPS_OTA_GET_IMG_DESC:
            ESP_LOGI(kTag, "Reading Image Description");
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
            ESP_LOGI(kTag, "Verifying chip id of new image: %d", *(esp_chip_id_t*)event_data);
            break;
        case ESP_HTTPS_OTA_DECRYPT_CB:
            ESP_LOGI(kTag, "Callback to decrypt function");
            break;
        case ESP_HTTPS_OTA_WRITE_FLASH:
            ESP_LOGD(kTag, "Writing to flash: %d written", *(int*)event_data);
            break;
        case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
            ESP_LOGI(kTag,
                     "Boot partition updated. Next Partition: %d",
                     *(esp_partition_subtype_t*)event_data);
            break;
        case ESP_HTTPS_OTA_FINISH:
            ESP_LOGI(kTag, "OTA finish");
            break;
        case ESP_HTTPS_OTA_ABORT:
            ESP_LOGI(kTag, "OTA abort");
            break;
    }
}

void Updater::AddHeader(const char* key, const char* value) { headers_.push_back({key, value}); }
void Updater::AddHeader(const std::string key, const std::string value) {
    headers_.push_back({key, value});
}

void Updater::AddBearerToken(const char* token) {
    AddHeader("Authorization", std::string("Bearer ") + token);
}

esp_err_t Updater::Update(const char* url) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.buffer_size = 4096;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;
    ota_config.partial_http_download = true;
    ota_config.http_client_init_cb = HttpClientInitCallback;

    ESP_ERROR_CHECK(esp_event_handler_register(
        ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, EventHandlerForwarder, nullptr));

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }

    if (ret != ESP_OK) {
        return ESP_FAIL;
    }

    esp_restart();
    return ESP_OK;
}

bool Updater::PendingVerification() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t res = esp_ota_get_state_partition(running, &ota_state);
    if (res != ESP_OK) {
        ESP_LOGE(kTag, "Failed to get OTA state");
        return false;
    }
    return ota_state == ESP_OTA_IMG_PENDING_VERIFY;
}

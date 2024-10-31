/**
 ******************************************************************************
 * @file        : provisioner.cpp
 * @brief       : WiFi Provisioning over BLE
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : WiFi Provisioning over BLE
 ******************************************************************************
 */

#include "provisioner.hpp"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char* kTag = "provisioner";

Provisioner* Provisioner::instance_       = nullptr;
SemaphoreHandle_t Provisioner::semaphore_ = xSemaphoreCreateMutex();

Provisioner* Provisioner::GetInstance() {
    if (instance_ == nullptr) {
        xSemaphoreTake(semaphore_, portMAX_DELAY);
        if (instance_ == nullptr) {
            instance_ = new Provisioner();
        }
        xSemaphoreGive(semaphore_);
    }
    return instance_;
}

Provisioner::Provisioner() {
    // Configuration for the provisioning manager
    wifi_prov_mgr_config_t config = {};
    config.scheme                 = wifi_prov_scheme_ble;
    config.scheme_event_handler   = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    wifi_event_group_ = xEventGroupCreate();
    GetDefautlServiceName();

    // Register our event handler for Wi-Fi, IP and Provisioning related events
    struct {
        esp_event_base_t event_base;
        int32_t event_id;
    } events[] = {
        {WIFI_PROV_EVENT, ESP_EVENT_ANY_ID},
        {PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID},
        {PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID},
        {WIFI_EVENT, ESP_EVENT_ANY_ID},
        {IP_EVENT, IP_EVENT_STA_GOT_IP},
    };

    for (auto& event : events) {
        ESP_ERROR_CHECK(esp_event_handler_register(
            event.event_base, event.event_id, &EventHandlerForwarder, this));
    }
}

void Provisioner::EventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(kTag, "Provisioner started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t* wifi_sta_cfg = (wifi_sta_config_t*)event_data;
                ESP_LOGI(kTag,
                         "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char*)wifi_sta_cfg->ssid,
                         (const char*)wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t* reason = (wifi_prov_sta_fail_reason_t*)event_data;
                ESP_LOGE(kTag,
                         "Provisioner failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR)
                             ? "Wi-Fi station authentication failed"
                             : "Wi-Fi access-point not found");
                retries_++;
                if (retries_ >= kMaxRetriesCount) {
                    ESP_LOGI(
                        kTag,
                        "Failed to connect with provisioned AP, resetting provisioned credentials");
                    wifi_prov_mgr_reset_sm_state_on_failure();
                    retries_ = 0;
                }
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(kTag, "Provisioner successful");
                retries_ = 0;
                break;
            case WIFI_PROV_END:
                // De-initialize manager once provisioning is finished
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(kTag, "Disconnected. Connecting to the AP again...");
                if (led_ != nullptr) {
                    led_->Flash(200, 0, 1, kRed);
                }
                esp_wifi_connect();
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(kTag, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        // Signal main application to continue execution
        xEventGroupSetBits(wifi_event_group_, kWifiConnectedEvent);
    } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
            case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
                ESP_LOGI(kTag, "BLE transport: Connected!");
                break;
            case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
                ESP_LOGI(kTag, "BLE transport: Disconnected!");
                break;
            default:
                break;
        }
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
            case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
                ESP_LOGI(kTag, "Secured session established!");
                break;
            case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
                ESP_LOGE(kTag,
                         "Received invalid security parameters for establishing secure session!");
                break;
            case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
                ESP_LOGE(kTag,
                         "Received incorrect username and/or PoP for establishing secure session!");
                break;
            default:
                break;
        }
    }
}

bool Provisioner::IsProvisioned() {
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    return provisioned;
}

void Provisioner::GetDefautlServiceName() {
    uint8_t eth_mac[6];
    const char* ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name_,
             sizeof(service_name_),
             "%s%02X%02X%02X",
             ssid_prefix,
             eth_mac[3],
             eth_mac[4],
             eth_mac[5]);
}

void Provisioner::InitSTA() {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void Provisioner::Provision(const char* country, const char* proof_of_possession) {
    esp_err_t err = esp_wifi_set_country_code(country, true);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to set country code");
    }

    if (IsProvisioned()) {
        ESP_LOGI(kTag, "Already provisioned, starting Wi-Fi STA");
        wifi_prov_mgr_deinit();
        InitSTA();
    } else {
        retries_ = 0;
        ESP_LOGI(kTag, "Starting provisioning");

        // clang-format off
        uint8_t custom_service_uuid[] = {  // LSB to MSB
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        };
        // clang-format on

        ESP_ERROR_CHECK(wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid));
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_1, (const void*)proof_of_possession, service_name_, nullptr));

        // Deinitialization is triggered by the default event loop handler,
        // so we don't need to wait for the provisioning to finish.
        // wifi_prov_mgr_wait();
        // wifi_prov_mgr_deinit();
    }
    xEventGroupWaitBits(wifi_event_group_, kWifiConnectedEvent, true, true, portMAX_DELAY);
}

void Provisioner::ResetProvisioning() {
    /* Resetting provisioning state machine to enable re-provisioning */
    wifi_prov_mgr_reset_provisioning();
}
/**
 ******************************************************************************
 * @file        : mqtt.cpp
 * @brief       : MQTT Client
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : MQTT Client
 ******************************************************************************
 */

#include "mqtt.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mqtt_client.h>

#include "nvs_config.hpp"

static const char* kTag = "mqtt";

MQTT* MQTT::instance_ = nullptr;
SemaphoreHandle_t MQTT::semaphore_ = xSemaphoreCreateMutex();

static void LogErrorIfNonZero(const char* message, int errorCode) {
    if (errorCode != 0) {
        ESP_LOGE(kTag, "Last error %s: 0x%x", message, errorCode);
    }
}

MQTT* MQTT::GetInstance() {
    if (instance_ == nullptr) {
        xSemaphoreTake(semaphore_, portMAX_DELAY);
        if (instance_ == nullptr) {
            instance_ = new MQTT();
        }
        xSemaphoreGive(semaphore_);
    }
    return instance_;
}

void MQTT::AddSubscription(const char* topic, int qos) {
    subscription t = {
        .topic = std::string(topic),
        .qos = qos,
    };
    subscriptions_.push_back(t);
}

MQTT::MQTT() { connected_ = false; }

esp_err_t MQTT::Init(LastWill* last_will) {
    NvsHandle handle;

    char broker[64] = {0};
    char username[64] = {0};
    char password[64] = {0};
    char topic_base[64] = {0};

    handle.Open("mqtt", NVS_READONLY);
    size_t length = sizeof(broker);
    if (handle.GetString("broker", broker, &length) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to read broker from NVS");
        return ESP_FAIL;
    }

    length = sizeof(username);
    handle.GetString("username", username, &length);
    length = sizeof(password);
    handle.GetString("password", password, &length);

    length = sizeof(topic_base);
    if (handle.GetString("topic-base", topic_base, &length) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to read topic_base from NVS");
        return ESP_FAIL;
    }

    topic_base_ = std::string(topic_base);

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = broker;
    if (strlen(username) > 0 && strlen(password) > 0) {
        mqtt_cfg.credentials.username = username;
        mqtt_cfg.credentials.authentication.password = password;
    }
    if (last_will != nullptr) {
        mqtt_cfg.session.last_will = *last_will;
    }

    ESP_LOGI(kTag, "MQTT URI: %s", broker);
    client_ = esp_mqtt_client_init(&mqtt_cfg);
    if (client_ == nullptr) {
        ESP_LOGE(kTag, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }
    esp_err_t err = esp_mqtt_client_register_event(
        client_, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, EventHandlerForwarder, this);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_mqtt_client_register_event failed: 0x%x", err);
        return err;
    }
    return ESP_OK;
}

esp_err_t MQTT::Start() {
    esp_err_t err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_mqtt_client_start failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(kTag, "MQTT started");
    return ESP_OK;
}

void MQTT::EventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            connected_ = true;
            ESP_LOGI(kTag, "MQTT_EVENT_CONNECTED");
            for (auto& s : subscriptions_) {
                const char* filter = s.topic.c_str();
                ESP_LOGI(kTag, "- Subscribing to %s", filter);
                esp_mqtt_client_subscribe(client, filter, s.qos);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(kTag, "MQTT_EVENT_DISCONNECTED");
            connected_ = false;
            break;
        case MQTT_EVENT_DATA:
            if (led_ != nullptr) {
                led_->Flash(100, 0, 1, kBlue);
            }
            ESP_LOGD(kTag, "MQTT_EVENT_DATA");
            ESP_LOGD(kTag, "- TOPIC=%.*s\r\n", event->topic_len, event->topic);
            ESP_LOGD(kTag, "- DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_PUBLISHED:
            if (led_ != nullptr) {
                led_->Flash(100, 0, 1, kWhite);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(kTag, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                LogErrorIfNonZero("reported from esp-tls",
                                  event->error_handle->esp_tls_last_esp_err);
                LogErrorIfNonZero("reported from tls stack",
                                  event->error_handle->esp_tls_stack_err);
                LogErrorIfNonZero("captured as transport's socket errno",
                                  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(kTag,
                         "Last errno string (%s)",
                         strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            break;
    }
}

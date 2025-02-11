#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mqtt_client.h>

#include <string>
#include <vector>

#include "status_led.hpp"

class MQTT {
   public:
    using LastWill = esp_mqtt_client_config_t::session_t::last_will_t;
    static MQTT* GetInstance();
    void AddSubscription(const char* topic, int qos = 1);
    void SetLed(StatusLed* led) { led_ = led; }
    esp_err_t Init(LastWill* last_will = nullptr, int keep_alive = 120);
    esp_err_t Start();

    esp_err_t RegisterEventHandler(esp_mqtt_event_id_t event,
                                   esp_event_handler_t event_handler,
                                   void* event_handler_arg);

    std::string Prefixed(const char* topic) { return topic_base_ + topic; }
    esp_err_t Publish(const char* topic, const char* data, int len, int qos = 1, int retain = 0);

    std::string topic_base_ = "esp/";

    bool fatal_error_ = false;
    bool connected_ = false;

   private:
    struct subscription {
        std::string topic;
        int qos;
    };

    static MQTT* instance_;
    static SemaphoreHandle_t semaphore_;

    MQTT();
    MQTT(MQTT const&) = delete;
    void operator=(MQTT const&) = delete;

    void EventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void EventHandlerForwarder(void* arg,
                                      esp_event_base_t event_base,
                                      int32_t event_id,
                                      void* event_data) {
        MQTT* instance = static_cast<MQTT*>(arg);
        if (instance == nullptr || instance->fatal_error_) {
            return;
        }
        instance->EventHandler(event_base, event_id, event_data);
    }

    StatusLed* led_ = nullptr;
    esp_mqtt_client_handle_t client_;
    std::vector<subscription> subscriptions_;
};

/**
 ******************************************************************************
 * @file        : app.hpp
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

#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "firmware_updater.hpp"
#include "httpd.hpp"
#include "mqtt.hpp"
#include "provisioner.hpp"
#include "status_led.hpp"

class App {
   public:
    static App* GetInstance();

    void Init(StatusLed* led);
    void Provision(const char* country, const char* proof_of_possession);
    void ResetProvisioning();
    void AddRoute(const char* uri,
                  httpd_method_t method,
                  esp_err_t (*handler)(httpd_req_t* r),
                  void* user_ctx) {
        httpd_->AddRoute(uri, method, handler, user_ctx);
    }

    esp_err_t StartMdns(const char* name);
    void StartHttpd(size_t stack_size, int max_uri_handlers) {
        httpd_->Start(stack_size, max_uri_handlers);
    }

    esp_err_t InitMQTT(MQTT::LastWill* last_will = nullptr, int keep_alive = 120) {
        return mqtt_->Init(last_will, keep_alive);
    }
    void AddSubscription(const char* topic, bool prefixed = true, int qos = 1) {
        if (prefixed) {
            mqtt_->AddSubscription(mqtt_->Prefixed(topic).c_str(), qos);
        } else {
            mqtt_->AddSubscription(topic, qos);
        }
    }
    esp_err_t RegisterMQTTEventHandler(esp_mqtt_event_id_t event,
                                       esp_event_handler_t event_handler,
                                       void* event_handler_arg) {
        return mqtt_->RegisterEventHandler(event, event_handler, event_handler_arg);
    }
    esp_err_t StartMQTT() { return mqtt_->Start(); }
    std::string TopicBase() { return mqtt_->topic_base_; }
    esp_err_t PublishMessage(
        const char* topic, const char* data, bool prefixed = true, int qos = 1, int retain = 0);

    bool PendingUpdateVerification() { return updater_->PendingVerification(); }
    void CommitUpdate() { updater_->Commit(); }
    void RollbackUpdate() { updater_->Rollback(); }

    StatusLed* GetStatusLed() { return led_; }
    Httpd* GetHttpd() { return httpd_; }
    MQTT* GetMQTT() { return mqtt_; }

    char hostname_[32];

    StatusLed* led_ = nullptr;
    Httpd* httpd_;
    MQTT* mqtt_;
    Updater* updater_;
    Provisioner* prov_;

   private:
    static App* instance_;
    static SemaphoreHandle_t semaphore_;

    static esp_err_t DoFirmwareUpgrade(httpd_req_t* req);
    static esp_err_t DoReset(httpd_req_t* req);
    static esp_err_t DoConfigSetKey(httpd_req_t* req);
    static esp_err_t DoConfigGetKey(httpd_req_t* req);
    static esp_err_t DoConfigGetAll(httpd_req_t* req);
    static esp_err_t DoConfigDeleteKey(httpd_req_t* req);
    static esp_err_t DoConfigDeleteNameSpace(httpd_req_t* req);
    static esp_err_t DoGetInfo(httpd_req_t* req);
    static esp_err_t DoInfo(httpd_req_t* req);

    static void ReprovionerTaskForwarder(void* arg) {
        App* instance = static_cast<App*>(arg);
        instance->ReprovionerTask();
    }
    void ReprovionerTask();

    App();
    App(App const&) = delete;
    void operator=(App const&) = delete;

    esp_netif_t* wifi_ = nullptr;
};

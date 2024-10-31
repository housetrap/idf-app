/**
 ******************************************************************************
 * @file        : provisioner.hpp
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

#pragma once

#include <esp_err.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include "status_led.hpp"

class Provisioner {
   public:
    static Provisioner* GetInstance();

    void SetLed(StatusLed* led) { led_ = led; }
    bool IsProvisioned();
    void Provision(const char* country, const char* proof_of_possession);
    void ResetProvisioning();

    void GetDefautlServiceName();

   private:
    static Provisioner* instance_;
    static SemaphoreHandle_t semaphore_;

    Provisioner();
    Provisioner(Provisioner const&)    = delete;
    void operator=(Provisioner const&) = delete;

    static const int kWifiConnectedEvent = BIT0;
    static const int kMaxRetriesCount    = 5;
    void InitSTA();
    void EventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void EventHandlerForwarder(void* arg,
                                      esp_event_base_t event_base,
                                      int32_t event_id,
                                      void* event_data) {
        Provisioner* instance = static_cast<Provisioner*>(arg);
        instance->EventHandler(event_base, event_id, event_data);
    }
    StatusLed* led_ = nullptr;
    EventGroupHandle_t wifi_event_group_;
    char service_name_[32];
    int retries_;
};
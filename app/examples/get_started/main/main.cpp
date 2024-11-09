#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>

#include "app.hpp"
#include "httpd.hpp"
#include "status_led.hpp"

extern "C" {
void app_main(void);
}

static const char* kTag = "app";

esp_err_t Hello(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;
    ctx->httpd_->Reply(req, "Hello FunHouse\n");
    return ESP_OK;
}

void app_main(void) {
    App* app = App::GetInstance();

    status_led::LedDevice* led_device = new status_led::Ws2812Led(47, true);
    StatusLed* led = new StatusLed(led_device);

    app->Init(led);
    app->Provision("CH", "fun24");
    app->AddRoute("/hello", HTTP_GET, Hello, app);
    app->StartMdns("FunHouse Demo");
    app->StartHttpd(8 * 1024, 32);

    if (app->PendingUpdateVerification()) {
        ESP_LOGI(kTag, "Pending verification ...");
        // run diagnostic function ...
        bool diagnostic_is_ok = true;
        if (diagnostic_is_ok) {
            ESP_LOGI(kTag, "Diagnostics completed successfully! Continuing execution ...");
            app->CommitUpdate();
        } else {
            ESP_LOGE(kTag, "Diagnostics failed! Start rollback to the previous version ...");
            app->RollbackUpdate();
        }
    }

    if (app->led_ != nullptr) {
        app->led_->On(kGreen);
    }

    if (app->InitMQTT() == ESP_OK) {
        app->AddSubscription("test/#");
        app->StartMQTT();
    } else {
        ESP_LOGE(kTag, "Failed to initialize MQTT");
    }

    while (true) {
        ESP_LOGI(kTag, "App running ...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        app->PublishMessage((app->TopicBase() + "ping").c_str(), "Hello MQTT", 0);
    }
}

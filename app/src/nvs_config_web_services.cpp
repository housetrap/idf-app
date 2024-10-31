/**
 ******************************************************************************
 * @file        : nvs_confog_web_servoices.cpp
 * @brief       : Web Services for NVS Configuration
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : Web Services for NVS Configuration
 ******************************************************************************
 */

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include <map>
#include <memory>

#include "app.hpp"
#include "cJSON.h"
#include "nvs_config.hpp"

static const char* kTag = "config webservices";

// ----- Static funtions -----

static esp_err_t GetNameSpace(
    httpd_req_t* req, App* ctx, char* query_string, char* name_space, size_t name_space_size) {
    if (httpd_query_key_value(query_string, "namespace", name_space, name_space_size) != ESP_OK) {
        ctx->httpd_->SendError(
            req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get namespace parameter");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t GetKey(
    httpd_req_t* req, App* ctx, char* query_string, char* key, size_t key_size) {
    if (httpd_query_key_value(query_string, "key", key, key_size) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get key parameter");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t JsonNode(cJSON* json,
                          httpd_req_t* req,
                          App* ctx,
                          NvsHandle& handle,
                          const char* key,
                          nvs_type_t nvs_type) {
    char typeName[16];
    if (handle.TypeName(nvs_type, typeName, sizeof(typeName)) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get type");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(json, "type", typeName);

    switch (nvs_type) {
        case NVS_TYPE_I8:
        case NVS_TYPE_U8:
        case NVS_TYPE_I16:
        case NVS_TYPE_U16:
        case NVS_TYPE_I32:
        case NVS_TYPE_U32:
        case NVS_TYPE_I64:
        case NVS_TYPE_U64: {
            double value;
            if (handle.GetInt(key, nvs_type, &value) != ESP_OK) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get integer value");
                return ESP_FAIL;
            }
            cJSON_AddNumberToObject(json, "value", value);
            break;
        }
        case NVS_TYPE_STR: {
            size_t size = 0;
            if (handle.GetString(key, nullptr, &size) != ESP_OK) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get string value");
                return ESP_FAIL;
            }
            std::shared_ptr<char> value((char*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM),
                                        heap_caps_free);
            if (handle.GetString(key, value.get(), &size) != ESP_OK) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get string value");
                return ESP_FAIL;
            }
            cJSON_AddStringToObject(json, "value", value.get());
            break;
        }
        case NVS_TYPE_BLOB: {
            size_t size = 0;
            if (handle.GetBlob(key, nullptr, &size) != ESP_OK) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get blob value");
                return ESP_FAIL;
            }
            std::shared_ptr<void> value((void*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM),
                                        heap_caps_free);
            if (handle.GetBlob(key, value.get(), &size) != ESP_OK) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get blob value");
                return ESP_FAIL;
            }

            size_t enc64_size = 4 + size * 2;
            std::shared_ptr<char> enc64((char*)heap_caps_malloc(enc64_size, MALLOC_CAP_SPIRAM),
                                        heap_caps_free);

            size_t olen;
            if (NvsHandle::Base64Encode(
                    enc64.get(), enc64_size, &olen, (const char*)value.get(), size) != 0) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to encode blob");
                return ESP_FAIL;
            }

            cJSON_AddStringToObject(json, "value", enc64.get());
            break;
        }
        default:
            ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unknown type");
            return ESP_FAIL;
    }
    return ESP_OK;
}

// ----- Web services -----

esp_err_t App::DoConfigSetKey(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;

    const int kBufferSize = 4096;
    std::shared_ptr<char> buffer((char*)heap_caps_malloc(kBufferSize, MALLOC_CAP_SPIRAM),
                                 heap_caps_free);

    if (httpd_req_get_url_query_str(req, buffer.get(), kBufferSize) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get query string");
        return ESP_FAIL;
    }

    char name_space[32];
    if (GetNameSpace(req, ctx, buffer.get(), name_space, sizeof(name_space)) != ESP_OK) {
        return ESP_FAIL;
    }

    char key[32];
    if (GetKey(req, ctx, buffer.get(), key, sizeof(key)) != ESP_OK) {
        return ESP_FAIL;
    }

    int res = ctx->httpd_->Receive(req, buffer.get(), kBufferSize);

    if (res < 0) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    } else if (res != req->content_len) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive all data");
        return ESP_FAIL;
    }

    std::shared_ptr<cJSON> json(cJSON_Parse(buffer.get()), cJSON_Delete);

    if (json == nullptr) {
        ESP_LOGW(kTag, "Failed to parse JSON");
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGW(kTag, "Error before: %s\n", error_ptr);
        }

        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse JSON");
        return ESP_FAIL;
    }

    cJSON* type  = cJSON_GetObjectItemCaseSensitive(json.get(), "type");
    cJSON* value = cJSON_GetObjectItemCaseSensitive(json.get(), "value");

    if ((!cJSON_IsString(type)) || (type->valuestring == nullptr)) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse type");
        return ESP_FAIL;
    }

    nvs_type_t nvs_type;
    if (NvsHandle::TypeOf(type->valuestring, &nvs_type) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unknown type");
        return ESP_FAIL;
    }

    NvsHandle my_handle;
    ESP_LOGI(kTag, "Opening namespace '%s'", name_space);
    if (my_handle.Open(name_space, NVS_READWRITE) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS handle");
        return ESP_FAIL;
    }

    if (cJSON_IsNumber(value)) {
        switch (nvs_type) {
            case NVS_TYPE_U8:
            case NVS_TYPE_I8:
            case NVS_TYPE_U16:
            case NVS_TYPE_I16:
            case NVS_TYPE_U32:
            case NVS_TYPE_I32:
            case NVS_TYPE_U64:
            case NVS_TYPE_I64:
                break;
            default:
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid type for integer value");
                return ESP_FAIL;
        }
        if (my_handle.SetInt(key, nvs_type, value->valueint) != ESP_OK) {
            ctx->httpd_->SendError(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set integer value");
            return ESP_FAIL;
        }
        ESP_LOGI(kTag, "Set integer value '%d'", value->valueint);
    } else if (cJSON_IsString(value) && (value->valuestring != nullptr)) {
        if (nvs_type == NVS_TYPE_STR) {
            if (my_handle.SetString(key, value->valuestring) != ESP_OK) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set string value");
                return ESP_FAIL;
            }
            ESP_LOGI(kTag, "Set string value '%s'", value->valuestring);
        } else if (nvs_type == NVS_TYPE_BLOB) {
            size_t olen;
            std::shared_ptr<void> dec64(
                (void*)heap_caps_malloc(strlen(value->valuestring), MALLOC_CAP_SPIRAM),
                heap_caps_free);
            if (NvsHandle::Base64Decode((char*)dec64.get(),
                                        strlen(value->valuestring),
                                        &olen,
                                        value->valuestring,
                                        strlen(value->valuestring)) != 0) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to decode base64 value");
                return ESP_FAIL;
            }
            if (my_handle.SetBlob(key, dec64.get(), olen) != ESP_OK) {
                ctx->httpd_->SendError(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set string value");
                return ESP_FAIL;
            }

            ESP_LOGI(kTag, "Set blob value '%s'", value->valuestring);
        } else {
            ctx->httpd_->SendError(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid type for string value");
            return ESP_FAIL;
        }
    } else {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse value");
        return ESP_FAIL;
    }

    if (my_handle.Commit() != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to commit NVS");
        return ESP_FAIL;
    }

    ESP_LOGI(kTag, "Configuration done");
    ctx->httpd_->Reply(req, "Configuration set\n");
    return ESP_OK;
}

esp_err_t App::DoConfigGetKey(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;

    const size_t kBufferSize = 1024;
    std::shared_ptr<char> buffer((char*)heap_caps_malloc(kBufferSize, MALLOC_CAP_SPIRAM),
                                 heap_caps_free);

    if (httpd_req_get_url_query_str(req, buffer.get(), kBufferSize) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get query string");
        return ESP_FAIL;
    }

    char name_space[32];
    if (GetNameSpace(req, ctx, buffer.get(), name_space, sizeof(name_space)) != ESP_OK) {
        return ESP_FAIL;
    }

    char key[32];
    if (GetKey(req, ctx, buffer.get(), key, sizeof(key)) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGD(kTag, "Opening namespace '%s'", name_space);
    NvsHandle my_handle;
    if (my_handle.Open(name_space, NVS_READONLY) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS handle");
        return ESP_FAIL;
    }

    ESP_LOGD(kTag, "Finding key '%s'", key);
    nvs_type_t nvs_type;
    if (my_handle.FindKey(key, &nvs_type) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to find key");
        return ESP_FAIL;
    }

    std::shared_ptr<cJSON> response(cJSON_CreateObject(), cJSON_Delete);
    if (JsonNode(response.get(), req, ctx, my_handle, key, nvs_type) != ESP_OK) {
        return ESP_FAIL;
    }

    std::shared_ptr<char> str(cJSON_PrintUnformatted(response.get()), free);
    ctx->httpd_->ReplyJson(req, str.get());
    ESP_LOGD(kTag, "Configuration replied");
    return ESP_OK;
}

esp_err_t App::DoConfigGetAll(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;
    std::map<std::string, std::map<std::string, int>> config;

    nvs_iterator_t it = NULL;
    esp_err_t res     = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &it);
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        ESP_LOGD(
            kTag, "Namespace '%s', key '%s', type '%d'", info.namespace_name, info.key, info.type);
        config[info.namespace_name][info.key] = info.type;
        res                                   = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    std::shared_ptr<cJSON> response(cJSON_CreateObject(), cJSON_Delete);
    NvsHandle my_handle;
    for (auto& ns : config) {
        my_handle.Open(ns.first.c_str(), NVS_READONLY);
        cJSON* namespace_json = cJSON_CreateObject();
        for (auto& key : ns.second) {
            cJSON* key_json = cJSON_CreateObject();
            std::shared_ptr<cJSON> response(cJSON_CreateObject(), cJSON_Delete);
            if (JsonNode(
                    key_json, req, ctx, my_handle, key.first.c_str(), (nvs_type_t)key.second) !=
                ESP_OK) {
                return ESP_FAIL;
            }
            cJSON_AddItemToObject(namespace_json, key.first.c_str(), key_json);
        }
        my_handle.Close();
        cJSON_AddItemToObject(response.get(), ns.first.c_str(), namespace_json);
    }
    std::shared_ptr<char> str(cJSON_PrintUnformatted(response.get()), free);
    ctx->httpd_->ReplyJson(req, str.get());
    return ESP_OK;
}

esp_err_t App::DoConfigDeleteKey(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;

    const size_t kBufferSize = 1024;
    std::shared_ptr<char> buffer((char*)heap_caps_malloc(kBufferSize, MALLOC_CAP_SPIRAM),
                                 heap_caps_free);

    if (httpd_req_get_url_query_str(req, buffer.get(), kBufferSize) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get query string");
        return ESP_FAIL;
    }

    char name_space[32];
    if (GetNameSpace(req, ctx, buffer.get(), name_space, sizeof(name_space)) != ESP_OK) {
        return ESP_FAIL;
    }

    char key[32];
    if (GetKey(req, ctx, buffer.get(), key, sizeof(key)) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGD(kTag, "Opening namespace '%s'", name_space);
    NvsHandle my_handle;
    if (my_handle.Open(name_space, NVS_READWRITE) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS handle");
        return ESP_FAIL;
    }

    if (my_handle.EraseKey(key) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete key");
        return ESP_FAIL;
    }

    ctx->httpd_->Reply(req, "Key Deleted");
    return ESP_OK;
}

esp_err_t App::DoConfigDeleteNameSpace(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;

    const size_t kBufferSize = 1024;
    std::shared_ptr<char> buffer((char*)heap_caps_malloc(kBufferSize, MALLOC_CAP_SPIRAM),
                                 heap_caps_free);

    if (httpd_req_get_url_query_str(req, buffer.get(), kBufferSize) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get query string");
        return ESP_FAIL;
    }

    char name_space[32];
    if (GetNameSpace(req, ctx, buffer.get(), name_space, sizeof(name_space)) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGD(kTag, "Opening namespace '%s'", name_space);
    NvsHandle my_handle;
    if (my_handle.Open(name_space, NVS_READWRITE) != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS handle");
        return ESP_FAIL;
    }

    if (my_handle.EraseAll() != ESP_OK) {
        ctx->httpd_->SendError(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete namespace");
        return ESP_FAIL;
    }

    ctx->httpd_->Reply(req, "Namespace Deleted");
    return ESP_OK;
}
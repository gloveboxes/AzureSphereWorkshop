#pragma once

#include "dx_azure_iot.h"
#include "dx_config.h"
#include "dx_deferred_update.h"
#include "dx_intercore.h"
#include "dx_json_serializer.h"
#include "dx_terminate.h"
#include "dx_timer.h"
#include "dx_utilities.h"
#include "dx_version.h"
#include "intercore_contract.h"

#include <applibs/applications.h>
#include <applibs/log.h>
#include <applibs/powermanagement.h>

#include "onboard_sensors.h"

// clang-format off
// https://docs.microsoft.com/en-us/azure/iot-pnp/overview-iot-plug-and-play
#define IOT_PLUG_AND_PLAY_MODEL_ID "dtmi:com:example:azuresphere:labmonitor;1"
#define NETWORK_INTERFACE "wlan0"
#define SAMPLE_VERSION_NUMBER "1.0"

#define CORE_ENVIRONMENT_COMPONENT_ID "6583cf17-d321-4d72-8283-0b7c5b56442b"

// Forward declarations
static void publish_telemetry_handler(EventLoopTimer *eventLoopTimer);
static void read_telemetry_handler(EventLoopTimer *eventLoopTimer);
static void button_a_handler(EventLoopTimer *eventLoopTimer);


// Number of bytes to allocate for the JSON telemetry message for IoT Hub/Central
#define JSON_MESSAGE_BYTES 256
static char msgBuffer[JSON_MESSAGE_BYTES] = {0};

DX_USER_CONFIG dx_config;

typedef struct {
    int temperature;
    int pressure;
    int humidity;
} SENSOR;

typedef struct {
    SENSOR latest;
    SENSOR previous;
    bool updated;
    HVAC_OPERATING_MODE latest_operating_mode;
    HVAC_OPERATING_MODE previous_operating_mode;
} ENVIRONMENT;

static ENVIRONMENT env;
static ONBOARD_TELEMETRY telemetry;

#define Log_Debug(f_, ...) dx_Log_Debug((f_), ##__VA_ARGS__)
static char Log_Debug_Time_buffer[256];

/// <summary>
/// Publish sensor telemetry using the following properties for efficient IoT Hub routing
/// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-messages-d2c
/// </summary>
static DX_MESSAGE_PROPERTY *messageProperties[] = {&(DX_MESSAGE_PROPERTY){.key = "appid", .value = "hvac"},
                                                   &(DX_MESSAGE_PROPERTY){.key = "type", .value = "telemetry"},
                                                   &(DX_MESSAGE_PROPERTY){.key = "schema", .value = "1"}};

/// <summary>
/// Common content properties for publish messages to IoT Hub/Central
/// </summary>
static DX_MESSAGE_CONTENT_PROPERTIES contentProperties = {.contentEncoding = "utf-8", .contentType = "application/json"};

static DX_GPIO_BINDING gpio_operating_led = {.pin = LED2, .name = "gpio_operating_led", .direction = DX_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true};
static DX_GPIO_BINDING gpio_network_led = {.pin = NETWORK_CONNECTED_LED, .name = "network_led", .direction = DX_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true};
static DX_GPIO_BINDING gpio_button_a = {.pin = BUTTON_A, .name = "button a", .direction = DX_INPUT };

static DX_TIMER_BINDING tmr_read_telemetry = {.period = {4, 0}, .name = "tmr_read_telemetry", .handler = read_telemetry_handler};
static DX_TIMER_BINDING tmr_publish_telemetry = {.period = {5, 0}, .name = "tmr_publish_telemetry", .handler = publish_telemetry_handler};
static DX_TIMER_BINDING tmr_button_a_handler = {.period = {0, 100000000}, .name = "tmr_button_handler", .handler = button_a_handler};
// clang-format on

DX_GPIO_BINDING *gpio_binding_sets[] = {&gpio_network_led, &gpio_operating_led, &gpio_button_a};
DX_TIMER_BINDING *timer_binding_sets[] = {&tmr_publish_telemetry, &tmr_read_telemetry, &tmr_button_a_handler};

// clang-format on
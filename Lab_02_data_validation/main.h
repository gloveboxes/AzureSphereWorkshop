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

// clang-format off
// https://docs.microsoft.com/en-us/azure/iot-pnp/overview-iot-plug-and-play
#define IOT_PLUG_AND_PLAY_MODEL_ID "dtmi:com:example:azuresphere:labmonitor;1"
#define NETWORK_INTERFACE "wlan0"
#define SAMPLE_VERSION_NUMBER "1.0"

#define CORE_ENVIRONMENT_COMPONENT_ID "6583cf17-d321-4d72-8283-0b7c5b56442b"

// Forward declarations
static DX_DIRECT_METHOD_RESPONSE_CODE hvac_off_handler(JSON_Value *json, DX_DIRECT_METHOD_BINDING *directMethodBinding, char **responseMsg);
static DX_DIRECT_METHOD_RESPONSE_CODE hvac_on_handler(JSON_Value *json, DX_DIRECT_METHOD_BINDING *directMethodBinding, char **responseMsg);
static DX_DIRECT_METHOD_RESPONSE_CODE restart_hvac_handler(JSON_Value *json, DX_DIRECT_METHOD_BINDING *directMethodBinding, char **responseMsg);
static void dt_set_hvac_temperature_handler(DX_DEVICE_TWIN_BINDING *deviceTwinBinding);
static void dt_set_panel_message_handler(DX_DEVICE_TWIN_BINDING *deviceTwinBinding);
static void dt_set_publish_rate_handler(DX_DEVICE_TWIN_BINDING *deviceTwinBinding);
static void intercore_environment_receive_msg_handler(void *data_block, ssize_t message_length);
static void publish_telemetry_handler(EventLoopTimer *eventLoopTimer);
static void read_telemetry_handler(EventLoopTimer *eventLoopTimer);


// Number of bytes to allocate for the JSON telemetry message for IoT Hub/Central
#define JSON_MESSAGE_BYTES 256
static char msgBuffer[JSON_MESSAGE_BYTES] = {0};
static char display_panel_message[64];
static int target_temperature = 0.0;
DX_USER_CONFIG dx_config;
static char* hvac_state[] = {"Unknown", "Heating", "Green", "Cooling", "On", "Off"};

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

#define Log_Debug(f_, ...) dx_Log_Debug((f_), ##__VA_ARGS__)
static char Log_Debug_Time_buffer[128];

/// <summary>
/// Publish sensor telemetry using the following properties for efficient IoT Hub routing
/// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-messages-d2c
/// </summary>
static DX_MESSAGE_PROPERTY *messageProperties[] = {&(DX_MESSAGE_PROPERTY){.key = "appid", .value = "hvac"},
                                                   &(DX_MESSAGE_PROPERTY){.key = "type", .value = "telemetry"},
                                                   &(DX_MESSAGE_PROPERTY){.key = "schema", .value = "1"}};

/// <summary>
/// Publish faulty sensor telemetry using the following properties for efficient IoT Hub routing
/// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-messages-d2c
/// </summary>
static DX_MESSAGE_PROPERTY *sensorErrorProperties[] = {&(DX_MESSAGE_PROPERTY){.key = "appid", .value = "hvac"},
                                                       &(DX_MESSAGE_PROPERTY){.key = "type", .value = "SensorError"},
                                                       &(DX_MESSAGE_PROPERTY){.key = "schema", .value = "1"}};

/// <summary>
/// Common content properties for publish messages to IoT Hub/Central
/// </summary>
static DX_MESSAGE_CONTENT_PROPERTIES contentProperties = {.contentEncoding = "utf-8", .contentType = "application/json"};

// declare all bindings
static DX_DEVICE_TWIN_BINDING dt_defer_requested = { .propertyName = "DeferredUpdateRequest", .twinType = DX_DEVICE_TWIN_STRING };
static DX_DEVICE_TWIN_BINDING dt_env_humidity = {.propertyName = "Humidity", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_env_pressure = {.propertyName = "Pressure", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_env_temperature = {.propertyName = "Temperature", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_hvac_operating_mode = {.propertyName = "OperatingMode", .twinType = DX_DEVICE_TWIN_STRING};
static DX_DEVICE_TWIN_BINDING dt_hvac_panel_message = {.propertyName = "PanelMessage", .twinType = DX_DEVICE_TWIN_STRING, .handler = dt_set_panel_message_handler};
static DX_DEVICE_TWIN_BINDING dt_hvac_publish_rate = {.propertyName = "PublishRate", .twinType = DX_DEVICE_TWIN_INT, .handler = dt_set_publish_rate_handler};
static DX_DEVICE_TWIN_BINDING dt_hvac_sw_version = {.propertyName = "SoftwareVersion", .twinType = DX_DEVICE_TWIN_STRING};
static DX_DEVICE_TWIN_BINDING dt_hvac_target_temperature = {.propertyName = "TargetTemperature", .twinType = DX_DEVICE_TWIN_INT, .handler = dt_set_hvac_temperature_handler};
static DX_DEVICE_TWIN_BINDING dt_utc_connected = {.propertyName = "ConnectedUtc", .twinType = DX_DEVICE_TWIN_STRING};
static DX_DEVICE_TWIN_BINDING dt_utc_startup = {.propertyName = "StartupUtc", .twinType = DX_DEVICE_TWIN_STRING};

static DX_DIRECT_METHOD_BINDING dm_hvac_off = {.methodName = "HvacOff", .handler = hvac_off_handler};
static DX_DIRECT_METHOD_BINDING dm_hvac_on = {.methodName = "HvacOn", .handler = hvac_on_handler};
static DX_DIRECT_METHOD_BINDING dm_restart_hvac = {.methodName = "RestartHvac", .handler = restart_hvac_handler};

static DX_GPIO_BINDING gpio_operating_led = {.pin = LED2, .name = "gpio_operating_led", .direction = DX_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true};
static DX_GPIO_BINDING gpio_network_led = {.pin = NETWORK_CONNECTED_LED, .name = "network_led", .direction = DX_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true};

static DX_TIMER_BINDING tmr_read_telemetry = {.period = {4, 0}, .name = "tmr_read_telemetry", .handler = read_telemetry_handler};
static DX_TIMER_BINDING tmr_publish_telemetry = {.period = {5, 0}, .name = "tmr_publish_telemetry", .handler = publish_telemetry_handler};
// clang-format on

// All bindings referenced in the following binding sets are initialised in the InitPeripheralsAndHandlers function
DX_DEVICE_TWIN_BINDING *device_twin_bindings[] = {&dt_utc_startup,    &dt_hvac_sw_version, &dt_hvac_publish_rate,  &dt_env_temperature,     &dt_env_pressure,
                                                  &dt_env_humidity,   &dt_utc_connected,   &dt_hvac_panel_message, &dt_hvac_operating_mode, &dt_hvac_target_temperature,
                                                  &dt_defer_requested};

DX_DIRECT_METHOD_BINDING *direct_method_binding_sets[] = {&dm_hvac_on, &dm_hvac_off, &dm_restart_hvac};
DX_GPIO_BINDING *gpio_binding_sets[] = {&gpio_network_led, &gpio_operating_led};
DX_TIMER_BINDING *timer_binding_sets[] = {&tmr_publish_telemetry, &tmr_read_telemetry};


INTERCORE_BLOCK intercore_block;


DX_INTERCORE_BINDING intercore_environment_ctx = {.sockFd = -1,
                                                  .nonblocking_io = true,
                                                  .rtAppComponentId = CORE_ENVIRONMENT_COMPONENT_ID,
                                                  .interCoreCallback = intercore_environment_receive_msg_handler,
                                                  .intercore_recv_block = &intercore_block,
                                                  .intercore_recv_block_length = sizeof(intercore_block)};

// clang-format on
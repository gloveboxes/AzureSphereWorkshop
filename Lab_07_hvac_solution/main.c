/* Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 *
 * This example is built on the Azure Sphere DevX library.
 *   1. DevX is an Open Source community-maintained implementation of the Azure Sphere SDK samples.
 *   2. DevX is a modular library that simplifies common development scenarios.
 *        - You can focus on your solution, not the plumbing.
 *   3. DevX documentation is maintained at https://github.com/gloveboxes/AzureSphereDevX/wiki
 *	 4. The DevX library is not a substitute for understanding the Azure Sphere SDK Samples.
 *          - https://github.com/Azure/azure-sphere-samples
 *
 * DEVELOPER BOARD SELECTION
 *
 * The following developer boards are supported.
 *
 *	 1. AVNET Azure Sphere Starter Kit.
 *   2. AVNET Azure Sphere Starter Kit Revision 2.
 *	 3. Seeed Studio Azure Sphere MT3620 Development Kit aka Reference Design Board or rdb.
 *	 4. Seeed Studio Seeed Studio MT3620 Mini Dev Board.
 *
 * ENABLE YOUR DEVELOPER BOARD
 *
 * Each Azure Sphere developer board manufacturer maps pins differently. You need to select the
 *    configuration that matches your board.
 *
 * Follow these steps:
 *
 *	 1. Open CMakeLists.txt.
 *	 2. Uncomment the set command that matches your developer board.
 *	 3. Click File, then Save to auto-generate the CMake Cache.
 *
 ************************************************************************************************/

#include "hw/azure_sphere_learning_path.h" // Hardware definition
#include "main.h"
#include "app_exit_codes.h"

#define ENABLE_RT_ENVIROMON 1
#define ENABLE_FAULTY_SENSOR 0

 /****************************************************************************************
  * Implementation
  ****************************************************************************************/

static void report_faulty_sensor(ENVIRONMENT* env) {
    // Report faulty sesnsor - telemetry out of range
    // clang-format off
    if (dx_jsonSerialize(msgBuffer, sizeof(msgBuffer), 5,
        DX_JSON_STRING, "Sensor", "Environment",
        DX_JSON_STRING, "ErrorMessage", "Telemetry out of range",
        DX_JSON_INT, "Temperature", env->latest.temperature,
        DX_JSON_INT, "Pressure", env->latest.pressure,
        DX_JSON_INT, "Humidity", env->latest.humidity))
        // clang-format on
    {
        Log_Debug("%s\n", msgBuffer);

        // Publish sensor out of range error message.
        // The message metadata type property is set to SensorError.
        // Using IoT Hub Message Routing you could route all SensorError messages to a maintainance system.
        // https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-messages-d2c
        dx_azurePublish(msgBuffer, strlen(msgBuffer), sensorErrorProperties, NELEMS(sensorErrorProperties), &contentProperties);
    }
}

/// <summary>
/// Update temperature and pressure device twins
/// Only update if data changed to minimise costs
/// Only update if at least 15 seconds passed since the last update
/// </summary>
/// <param name="temperature"></param>
/// <param name="pressure"></param>
static void update_device_twins(ENVIRONMENT* env) {
    static int64_t previous_milliseconds = 0ll;

    int64_t now = dx_getNowMilliseconds();

    // Update twins if 10 seconds (10000 milliseconds) or more have passed since the last update
    if ((now - previous_milliseconds) > 10000) {
        previous_milliseconds = now;

        if (env->previous.temperature != env->latest.temperature) {
            env->previous.temperature = env->latest.temperature;
            // Update temperature device twin
            dx_deviceTwinReportValue(&dt_env_temperature, &env->latest.temperature);
        }

        if (env->previous.pressure != env->latest.pressure) {
            env->previous.pressure = env->latest.pressure;
            // Update pressure device twin
            dx_deviceTwinReportValue(&dt_env_pressure, &env->latest.pressure);
        }

        if (env->previous.humidity != env->latest.humidity) {
            env->previous.humidity = env->latest.humidity;
            // Update humidity device twin
            dx_deviceTwinReportValue(&dt_env_humidity, &env->latest.humidity);
        }

        if (env->latest_operating_mode != HVAC_MODE_UNKNOWN && env->latest_operating_mode != env->previous_operating_mode) {
            env->previous_operating_mode = env->latest_operating_mode;
            dx_deviceTwinReportValue(&dt_hvac_operating_mode, hvac_state[env->latest_operating_mode]);
        }
    }
}

// Validate sensor readings and publish HVAC telemetry
static void publish_telemetry_handler(EventLoopTimer* eventLoopTimer) {
    static int msgId = 0;

    if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0) {
        dx_terminate(DX_ExitCode_ConsumeEventLoopTimeEvent);
        return;
    }

    if (!dx_isAzureConnected() || !env.updated) {
        return;
    }

    // Validate sensor data to check within expected range
    if (!IN_RANGE(env.latest.temperature, -20, 50) || !IN_RANGE(env.latest.pressure, 800, 1200) || !IN_RANGE(env.latest.humidity, 0, 100)) {
        // sensor data is outside of normal operating range so report the fault
        report_faulty_sensor(&env);
    } else {
        // Serialize telemetry as JSON
        // clang-format off
        if (dx_jsonSerialize(msgBuffer, sizeof(msgBuffer), 6,
            DX_JSON_INT, "MsgId", msgId++,
            DX_JSON_INT, "Temperature", env.latest.temperature,
            DX_JSON_INT, "Pressure", env.latest.pressure,
            DX_JSON_INT, "Humidity", env.latest.humidity,
            DX_JSON_INT, "PeakUserMemoryKiB", (int)Applications_GetPeakUserModeMemoryUsageInKB(),
            DX_JSON_INT, "TotalMemoryKiB", (int)Applications_GetTotalMemoryUsageInKB()))
            // clang-format on
        {
            Log_Debug("%s\n", msgBuffer);

            // Publish telemetry message to IoT Hub/Central
            dx_azurePublish(msgBuffer, strlen(msgBuffer), messageProperties, NELEMS(messageProperties), &contentProperties);
            update_device_twins(&env);
        } else {
            Log_Debug("JSON Serialization failed: Buffer too small\n");
            dx_terminate(APP_ExitCode_Telemetry_Buffer_Too_Small);
        }
    }
}

static void read_telemetry_handler(EventLoopTimer* eventLoopTimer) {
    if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0) {
        dx_terminate(DX_ExitCode_ConsumeEventLoopTimeEvent);
        return;
    }

#if (ENABLE_RT_ENVIROMON == 1)

    intercore_block.cmd = IC_READ_SENSOR;
    dx_intercorePublish(&intercore_environment_ctx, &intercore_block, sizeof(intercore_block));

#else

    env.latest.temperature = 20 + (rand() % 40);
    env.latest.pressure = 1100;
    env.latest.humidity = 20 + (rand() % 60);
    env.updated = true;

#endif

}

/// <summary>
/// Callback handler for Inter-Core Messaging
/// </summary>
static void intercore_environment_receive_msg_handler(void* data_block, ssize_t message_length) {
    INTERCORE_BLOCK* ic_data = (INTERCORE_BLOCK*)data_block;

    switch (ic_data->cmd) {
    case IC_READ_SENSOR:
        env.latest.temperature = ic_data->temperature;
        env.latest.pressure = ic_data->pressure;
        env.latest.humidity = ic_data->humidity;
        env.latest_operating_mode = ic_data->operating_mode;
        env.updated = true;

#if (ENABLE_FAULTY_SENSOR == 1)
        env.latest.temperature += (rand() % 40);
#endif
        break;
    default:
        break;
    }
}

/// <summary>
/// Device twin to set the rate the HVAC will publish telemetry
/// </summary>
/// <param name="deviceTwinBinding"></param>
static void dt_set_publish_rate_handler(DX_DEVICE_TWIN_BINDING* deviceTwinBinding) {
    int sample_rate_seconds = *(int*)deviceTwinBinding->propertyValue;

    // validate data is sensible range before applying
    if (IN_RANGE(sample_rate_seconds, 0, 120)) {
        dx_timerChange(&tmr_publish_telemetry, &(struct timespec){sample_rate_seconds, 0});
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_COMPLETED);
    } else {
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_ERROR);
    }
}

// This device twin callback demonstrates how to manage device twins of type string.
// A reference to the string is passed that is available only for the lifetime of the callback.
// You must copy to a global char array to preserve the string outside of the callback.
// As strings are arbitrary length on a constrained device you control memory allocation.
static void dt_set_panel_message_handler(DX_DEVICE_TWIN_BINDING* deviceTwinBinding) {
    char* panel_message = (char*)deviceTwinBinding->propertyValue;

    // Is the message size less than the destination buffer size and printable characters
    if (strlen(panel_message) < sizeof(display_panel_message) && dx_isStringPrintable(panel_message)) {
        strncpy(display_panel_message, panel_message, sizeof(display_panel_message));
        Log_Debug("Virtual HVAC Display Panel Message: %s\n", display_panel_message);
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_COMPLETED);
    } else {
        Log_Debug("Local copy failed. String too long or invalid data\n");
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_ERROR);
    }
}

static void dt_set_target_temperature_handler(DX_DEVICE_TWIN_BINDING* deviceTwinBinding) {
    int _target_temperature = *(int*)deviceTwinBinding->propertyValue;
    if (IN_RANGE(_target_temperature, 0, 50)) {
        target_temperature = _target_temperature;

        intercore_block.cmd = IC_TARGET_TEMPERATURE;
        intercore_block.temperature = target_temperature;
        dx_intercorePublish(&intercore_environment_ctx, &intercore_block, sizeof(intercore_block));

        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_COMPLETED);
    } else {
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_ERROR);
    }
}

// Direct method name = HvacOn
static DX_DIRECT_METHOD_RESPONSE_CODE hvac_on_handler(JSON_Value* json, DX_DIRECT_METHOD_BINDING* directMethodBinding, char** responseMsg) {
    dx_gpioOn(&gpio_operating_led);
    return DX_METHOD_SUCCEEDED;
}

// Direct method name =HvacOff
static DX_DIRECT_METHOD_RESPONSE_CODE hvac_off_handler(JSON_Value* json, DX_DIRECT_METHOD_BINDING* directMethodBinding, char** responseMsg) {
    dx_gpioOff(&gpio_operating_led);
    return DX_METHOD_SUCCEEDED;
}

/// <summary>
/// Start Reboot Device Direct Method 'RebootDevice' {"Seconds":5}
/// </summary>
static DX_DIRECT_METHOD_RESPONSE_CODE restart_hvac_handler(JSON_Value* json, DX_DIRECT_METHOD_BINDING* directMethodBinding, char** responseMsg) {
    PowerManagement_ForceSystemReboot();
    return DX_METHOD_SUCCEEDED;
}

static void report_startup(bool connected) {
    if (connected) {
        // This is the first connect so update device start time UTC and software version
        dx_deviceTwinReportValue(&dt_utc_startup, dx_getCurrentUtc(msgBuffer, sizeof(msgBuffer)));

        // Report software version
        snprintf(msgBuffer, sizeof(msgBuffer), "Sample version: %s, DevX version: %s", SAMPLE_VERSION_NUMBER, AZURE_SPHERE_DEVX_VERSION);
        dx_deviceTwinReportValue(&dt_hvac_sw_version, msgBuffer);

        // now unregister this callback as we've reported startup time and sw version
        dx_azureUnregisterConnectionChangedNotification(report_startup);
    }
}

static void ConnectionStatus(bool connected) {
    if (connected) {
        dx_deviceTwinReportValue(&dt_utc_connected, dx_getCurrentUtc(msgBuffer, sizeof(msgBuffer)));
    }
    dx_gpioStateSet(&gpio_network_led, connected);
}

/// <summary>
/// Algorithm to determine if a deferred update can proceed
/// </summary>
/// <param name="max_deferral_time_in_minutes">The maximum number of minutes you can defer</param>
/// <returns>Return 0 to start update, return greater than zero to defer</returns>
static uint32_t DeferredUpdateCalculate(uint32_t max_deferral_time_in_minutes, SysEvent_UpdateType type, SysEvent_Status status, const char* typeDescription,
    const char* statusDescription) {
    time_t now = time(NULL);
    struct tm* t = gmtime(&now);
    char utc[40];

    // UTC +10 is good for Australia :)
    t->tm_hour += 10;
    t->tm_hour = t->tm_hour % 24;

    // If local time between 1am and 5am defer for zero minutes else defer for 15 minutes
    uint32_t requested_minutes = IN_RANGE(t->tm_hour, 1, 5) ? 0 : 10;

    // Update defer requested device twin
    snprintf(msgBuffer, sizeof(msgBuffer), "Utc: %s, Type: %s, Status: %s, Max defer minutes: %i, Requested minutes: %i", dx_getCurrentUtc(utc, sizeof(utc)),
        typeDescription, statusDescription, max_deferral_time_in_minutes, requested_minutes);

    dx_deviceTwinReportValue(&dt_defer_requested, msgBuffer);

    return requested_minutes;
}

/// <summary>
///  Initialize peripherals, device twins, direct methods, timer_binding_sets.
/// </summary>
static void InitPeripheralsAndHandlers(void) {
    dx_Log_Debug_Init(Log_Debug_Time_buffer, sizeof(Log_Debug_Time_buffer));
    dx_azureConnect(&dx_config, NETWORK_INTERFACE, IOT_PLUG_AND_PLAY_MODEL_ID);
    dx_gpioSetOpen(gpio_binding_sets, NELEMS(gpio_binding_sets));
    dx_timerSetStart(timer_binding_sets, NELEMS(timer_binding_sets));
    dx_deviceTwinSubscribe(device_twin_bindings, NELEMS(device_twin_bindings));
    dx_directMethodSubscribe(direct_method_binding_sets, NELEMS(direct_method_binding_sets));

    dx_intercoreConnect(&intercore_environment_ctx);

    dx_deferredUpdateRegistration(DeferredUpdateCalculate, NULL);

    dx_azureRegisterConnectionChangedNotification(report_startup);
    dx_azureRegisterConnectionChangedNotification(ConnectionStatus);

    // initialize previous environment sensor variables
    env.previous.temperature = env.previous.pressure = env.previous.humidity = INT32_MAX;
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void) {
    dx_timerSetStop(timer_binding_sets, NELEMS(timer_binding_sets));
    dx_deviceTwinUnsubscribe();
    dx_directMethodUnsubscribe();
    dx_gpioSetClose(gpio_binding_sets, NELEMS(gpio_binding_sets));
    dx_timerEventLoopStop();
}

int main(int argc, char* argv[]) {
    dx_registerTerminationHandler();

    if (!dx_configParseCmdLineArguments(argc, argv, &dx_config)) {
        return dx_getTerminationExitCode();
    }

    InitPeripheralsAndHandlers();

    // Main loop
    while (!dx_isTerminationRequired()) {
        int result = EventLoop_Run(dx_timerGetEventLoop(), -1, true);
        // Continue if interrupted by signal, e.g. due to breakpoint being set.
        if (result == -1 && errno != EINTR) {
            dx_terminate(DX_ExitCode_Main_EventLoopFail);
        }
    }

    ClosePeripheralsAndHandlers();
    Log_Debug("Application exiting.\n");
    return dx_getTerminationExitCode();
}
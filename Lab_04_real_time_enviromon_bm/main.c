#include "intercore.h"
#include "intercore_contract.h"

#if defined(OEM_AVNET)
#include "IMU_lib/imu_temp_pressure.h"
#endif

#include "hw/azure_sphere_learning_path.h"
#include "utils.h"

#include "os_hal_uart.h"
#include "os_hal_gpio.h"
#include "os_hal_gpt.h"
#include "nvic.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define IN_RANGE(number, low, high) (low <= number && high >= number)

os_hal_gpio_pin ledRgb[] = {LED_RED, LED_GREEN, LED_BLUE};

INTERCORE_BLOCK ic_outbound_data;
INTERCORE_BLOCK *ic_inbound_data;

typedef struct {
    int last_temperature;
    int target_temperature;
    bool target_temperature_set;
    int previous_led;
    int current_led;
} HVAC_MODE;

HVAC_MODE hvac_mode;

uint8_t mbox_local_buf[MBOX_BUFFER_LEN_MAX];
BufferHeader *outbound, *inbound;
volatile u8 blockDeqSema;
volatile u8 blockFifoSema;
volatile bool refresh_data_trigger;

struct os_gpt_int gpt0_int;
struct os_gpt_int gpt3_int;

/* Bitmap for IRQ enable. bit_0 and bit_1 are used to communicate with HL_APP */
uint32_t mbox_irq_status = 0x3;
size_t payloadStart = 20; /* UUID 16B, Reserved 4B */

u32 mbox_shared_buf_size = 0;

static const uint8_t uart_port_num = OS_HAL_UART_ISU3;

/******************************************************************************/
/* Timers */
/******************************************************************************/
static const uint8_t gpt_task_scheduler = OS_HAL_GPT0;
static const uint32_t gpt_task_scheduler_timer_val = 1; /* 1ms */

/******************************************************************************/
/* Applicaiton Hooks */
/******************************************************************************/
/* Hook for "printf". */
void _putchar(char character)
{
    mtk_os_hal_uart_put_char(uart_port_num, character);
    if (character == '\n')
        mtk_os_hal_uart_put_char(uart_port_num, '\r');
}

bool initialize_hardware(void)
{
    mtk_os_hal_gpio_set_direction(LED_RED, OS_HAL_GPIO_DIR_OUTPUT);
    mtk_os_hal_gpio_set_direction(LED_GREEN, OS_HAL_GPIO_DIR_OUTPUT);
    mtk_os_hal_gpio_set_direction(LED_BLUE, OS_HAL_GPIO_DIR_OUTPUT);

    mtk_os_hal_gpio_set_output(LED_RED, OS_HAL_GPIO_DATA_HIGH);
    mtk_os_hal_gpio_set_output(LED_GREEN, OS_HAL_GPIO_DATA_HIGH);
    mtk_os_hal_gpio_set_output(LED_BLUE, OS_HAL_GPIO_DATA_HIGH);

#if defined(OEM_AVNET)

    bool status = lp_imu_initialize();

    // wait 100 milliseconds
    Gpt3_WaitUs(100000);

    if (status) {
        // Prime the temperature and humidity sensors
        // Observed the first few readings on startup may return NaN
        for (size_t i = 0; i < 6; i++) {
            if (!isnan(lp_get_temperature_lps22h()) && !isnan(lp_get_pressure())) {
                break;
            }
            // else wait 100 milliseconds
            Gpt3_WaitUs(100000);
        }

        ic_outbound_data.temperature = round(lp_get_temperature_lps22h());
        ic_outbound_data.pressure = round(lp_get_pressure());
    }
    return status;
}
#else
    return true;
}
#endif

static void send_intercore_msg(void *data, size_t length)
{
    uint32_t dataSize;
    memcpy((void *)mbox_local_buf, &hlAppId, sizeof(hlAppId)); // copy high level appid to first 20 bytes
    memcpy(mbox_local_buf + payloadStart, data, length);
    dataSize = payloadStart + length;
    EnqueueData(inbound, outbound, mbox_shared_buf_size, mbox_local_buf, dataSize);
}

/// <summary>
/// Set the temperature status led.
/// Red if HVAC needs to be turned on to get to desired temperature.
/// Blue to turn on cooler.
/// Green equals just right, no action required.
/// </summary>
void set_hvac_operating_mode(int temperature)
{
    if (!hvac_mode.target_temperature_set) {
        return;
    }

    hvac_mode.current_led = temperature == hvac_mode.target_temperature  ? HVAC_MODE_GREEN
                            : temperature > hvac_mode.target_temperature ? HVAC_MODE_COOLING
                                                                         : HVAC_MODE_HEATING;

    if (hvac_mode.previous_led != hvac_mode.current_led) {
        // minus one as first item is HVAC_MODE_UNKNOWN
        mtk_os_hal_gpio_set_output(ledRgb[hvac_mode.previous_led - 1], true);
        hvac_mode.previous_led = hvac_mode.current_led;
    }

    ic_outbound_data.operating_mode = hvac_mode.current_led;
    // minus one as first item is HVAC_MODE_UNKNOWN
    mtk_os_hal_gpio_set_output(ledRgb[hvac_mode.current_led - 1], false);
}

static void process_inbound_message()
{
    u32 mbox_local_buf_len;
    int result;

    mbox_local_buf_len = MBOX_BUFFER_LEN_MAX;
    result = DequeueData(outbound, inbound, mbox_shared_buf_size, mbox_local_buf, &mbox_local_buf_len);

    if (result == 0 && mbox_local_buf_len > payloadStart) {

        ic_inbound_data = (INTERCORE_BLOCK *)(mbox_local_buf + payloadStart);

        switch (ic_inbound_data->cmd) {
        case IC_READ_SENSOR:
            send_intercore_msg(&ic_outbound_data, sizeof(INTERCORE_BLOCK));
            break;
        case IC_TARGET_TEMPERATURE:
            if (IN_RANGE(ic_inbound_data->temperature, -20, 80)) {
                hvac_mode.target_temperature_set = true;
                hvac_mode.target_temperature = ic_inbound_data->temperature;
                set_hvac_operating_mode(hvac_mode.last_temperature);
            }
            break;
        default:
            break;
        }
    }
}

// sensor read
#if defined(OEM_AVNET)
static void refresh_data(void)
{
    int rand_number;

    ic_outbound_data.cmd = IC_READ_SENSOR;

    ic_outbound_data.temperature = round(lp_get_temperature_lps22h());
    ic_outbound_data.pressure = round(lp_get_pressure());

    rand_number = rand() % 20;
    ic_outbound_data.humidity = 40.0 + rand_number;

    hvac_mode.last_temperature = ic_outbound_data.temperature;

    set_hvac_operating_mode(ic_outbound_data.temperature);
}
#else
void refresh_data(void)
{
    int rand_number;

    ic_outbound_data.cmd = IC_READ_SENSOR;

    rand_number = (rand() % 10);
    ic_outbound_data.temperature = (float)(15.0 + rand_number);

    rand_number = (rand() % 100);
    ic_outbound_data.pressure = (float)(950.0 + rand_number);

    rand_number = rand() % 40;
    ic_outbound_data.humidity = 40.0 + rand_number;

    hvac_mode.last_temperature = ic_outbound_data.temperature;

    set_hvac_operating_mode(ic_outbound_data.temperature);
}
#endif

static void task_scheduler(void *cb_data)
{
    static size_t refresh_data_tick_counter = SIZE_MAX;

    if (refresh_data_tick_counter++ >= 2000) // 2 seconds
    {
        refresh_data_tick_counter = 0;
        refresh_data_trigger = true;
    }
}

_Noreturn void RTCoreMain(void)
{
    /* Init Vector Table */
    NVIC_SetupVectorTable();

    srand((unsigned int)time(NULL)); // seed the random number generator for fake telemetry

    /* Init GPT */
    gpt0_int.gpt_cb_hdl = task_scheduler;
    gpt0_int.gpt_cb_data = NULL;
    mtk_os_hal_gpt_init();

    /* configure GPT0 clock speed (as 1KHz) */
    /* and register GPT0 user interrupt callback handle and user data. */
    mtk_os_hal_gpt_config(gpt_task_scheduler, false, &gpt0_int);

    /* configure GPT0 timeout as 1ms and repeat mode. */
    mtk_os_hal_gpt_reset_timer(gpt_task_scheduler, gpt_task_scheduler_timer_val, true);

    initialise_intercore_comms();
    initialize_hardware();

    /* start timer */
    mtk_os_hal_gpt_start(gpt_task_scheduler);

    for (;;) {
        if (blockDeqSema > 0) {
            process_inbound_message();
        }

        if (refresh_data_trigger) {
            refresh_data_trigger = false;
            refresh_data();
        }
    }
}

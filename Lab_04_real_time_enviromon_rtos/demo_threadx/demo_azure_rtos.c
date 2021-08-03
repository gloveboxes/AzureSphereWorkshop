/*************************************************************************************************************************************
 * The intercore communications labs require multiple instances of VS Code to be running
 *
 * It is recommended to install the VS Code Peacock extension for the intercore communications labs.
 * The Peacock extension allows you to change the color of your Visual Studio Code workspace.
 * Ideal when you have multiple VS Code instances
 * Install the Peacock extension from https://marketplace.visualstudio.com/items?itemName=johnpapa.vscode-peacock
 *
 * The following colours have been set:
 * The VS Code instance attached to the Real-Time core will be red. Real-time is red, as in racing red.
 * The VS Code instance attached to the High-level core is blue. High-level is blue, as in sky is high and blue.
 * You can change the default colours to match your preferences.
 *
 *   DEVELOPER BOARD SELECTION
 *
 *   The following developer boards are supported.
 *
 *	   1. AVNET Azure Sphere Starter Kit.
 *     2. AVNET Azure Sphere Starter Kit Revision 2.
 *	   3. Seeed Studio Azure Sphere MT3620 Development Kit aka Reference Design Board or rdb.
 *	   4. Seeed Studio Seeed Studio MT3620 Mini Dev Board.
 *
 *   ENABLE YOUR DEVELOPER BOARD
 *
 *   Each Azure Sphere developer board manufacturer maps ledRgb differently. You need to select the configuration that matches your board.
 *
 *   Follow these steps:
 *
 *	   1. Open CMakeLists.txt.
 *	   2. Uncomment the set command that matches your developer board.
 *	   3. Click File, then Save to save the CMakeLists.txt file which will auto generate the CMake Cache.
 *
 *
 * Intercore messaging.
 *
 * There needs to be a shared understanding of the data structure being shared between the real-time and high-level apps
 * This shared understanding is declared in the intercore_contract.h file.  This file can be found in the IntercoreContract directory.
 *
*************************************************************************************************************************************/

#include "../IMU_lib/imu_temp_pressure.h"
//#include "hw/azure_sphere_learning_path.h"
#include "intercore_contract.h"
#include "mt3620-intercore.h"
#include "os_hal_gpio.h"
#include "os_hal_uart.h"
#include "printf.h"
#include "tx_api.h"
#include <math.h>
#include <stdbool.h>
#include <time.h>

#define DEMO_STACK_SIZE 1024
#define DEMO_BYTE_POOL_SIZE 9120
#define DEMO_BLOCK_POOL_SIZE 100
#define DEMO_QUEUE_SIZE 100

// 1 tick = 10ms. It is configurable.
#define MS_TO_TICK(ms)  ((ms) * (TX_TIMER_TICKS_PER_SECOND) / 1000)

// forward signatures
void set_hvac_operating_mode(int temperature);

// resources for inter core messaging
static uint8_t buf[256];
static uint32_t dataSize;
static BufferHeader* outbound, * inbound;
static uint32_t sharedBufSize = 0;
static const size_t payloadStart = 20;
static int sensorSampleRateInSeconds = 500; // initialize to 5 seconds 500 ticks at 10ms a tick

INTERCORE_BLOCK ic_control_block;
INTERCORE_BLOCK enviroment_control_block;

enum LEDS { RED, GREEN, BLUE };

typedef struct {
	int last_temperature;
	int target_temperature;
	bool target_temperature_set;
	int previous_led;
	int current_led;
} HVAC_MODE;

HVAC_MODE hvac_mode;

int ledRgb[] = { OS_HAL_GPIO_8, OS_HAL_GPIO_9, OS_HAL_GPIO_10 };

bool highLevelReady = false;

// Define the ThreadX object control blocks...
TX_THREAD               tx_hardware_Thread;
TX_THREAD               tx_Intercore_Thread;
TX_THREAD               tx_hardware_init_thread;

TX_EVENT_FLAGS_GROUP    event_flags_0;
TX_BYTE_POOL            byte_pool_0;
TX_BLOCK_POOL           block_pool_0;

TX_TIMER                msTimer;
TX_EVENT_FLAGS_GROUP    hardware_event_flags_0;
TX_EVENT_FLAGS_GROUP    Intercore_event_flags_0;

UCHAR memory_area[DEMO_BYTE_POOL_SIZE];

static volatile bool hardwareInitOK = false;

// Define thread prototypes.
void hardware_init_thread(ULONG thread_input);
void intercore_thread(ULONG thread_input);
void read_sensor_thread(ULONG thread_input);
void timer_scheduler(ULONG input);


int main() {
	tx_kernel_enter(); // Enter the Azure RTOS kernel.
}

// Define what the initial system looks like.
void tx_application_define(void* first_unused_memory) {
	CHAR* pointer;
	UINT status = TX_SUCCESS;

	/* Create a byte memory pool from which to allocate the thread stacks.  */
	tx_byte_pool_create(&byte_pool_0, "byte pool 0", memory_area, DEMO_BYTE_POOL_SIZE);

	// create event flags
	status = tx_event_flags_create(&hardware_event_flags_0, "Hardware Event");                   // Hardware events fire every 5 ms
	if (status != TX_SUCCESS) {
		printf("failed to create hardware_event_flags\r\n");
	}

	status = tx_event_flags_create(&Intercore_event_flags_0, "Intercore Event");                     // Intercore events fire every 10 ms
	if (status != TX_SUCCESS) {
		printf("failed to create Intercore_event_flags\r\n");
	}

	/* Allocate the stack for thread 0.  */
	tx_byte_allocate(&byte_pool_0, (VOID**)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
	/* Create the main thread.  */
	tx_thread_create(&tx_hardware_Thread, "read sensor thread", read_sensor_thread, 0,
		pointer, DEMO_STACK_SIZE, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);

	tx_byte_allocate(&byte_pool_0, (VOID**)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
	/* Create the intercore msg thread.  */
	tx_thread_create(&tx_Intercore_Thread, "Intercore Thread", intercore_thread, 0,
		pointer, DEMO_STACK_SIZE, 4, 4, TX_NO_TIME_SLICE, TX_AUTO_START);

	tx_byte_allocate(&byte_pool_0, (VOID**)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
	// Create a hardware init thread.
	tx_thread_create(&tx_hardware_init_thread, "hardware init thread", hardware_init_thread, 0,
		pointer, DEMO_STACK_SIZE, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
}

// https://embeddedartistry.com/blog/2017/02/17/implementing-malloc-with-threadx/
// overrides for malloc and free required for srand and rand
void* malloc(size_t size) {
	void* ptr = NULL;

	if (size > 0) {
		// We simply wrap the threadX call into a standard form
		uint8_t r = tx_byte_allocate(&byte_pool_0, &ptr, size, TX_WAIT_FOREVER);

		if (r != TX_SUCCESS) {
			ptr = NULL;
		}
	}
	//else NULL if there was no size

	return ptr;
}

void free(void* ptr) {
	if (ptr) {
		//We simply wrap the threadX call into a standard form
		tx_byte_release(ptr);
	}
}

// initialize hardware here.
#if defined(OEM_AVNET)
bool initialize_hardware(void) {
	bool status = (lp_imu_initialize());
	tx_thread_sleep(MS_TO_TICK(100));

	if (status) {
		// Prime the temperature and humidity sensors
		// Observed the first few readings on startup may return NaN
		for (size_t i = 0; i < 6; i++) {
			if (!isnan(lp_get_temperature_lps22h()) && !isnan(lp_get_pressure())) {
				break;
			}
			tx_thread_sleep(MS_TO_TICK(100));
		}

		enviroment_control_block.temperature = round(lp_get_temperature_lps22h());
		enviroment_control_block.pressure = round(lp_get_pressure());
	}

	// Open the red, green, and blue gpio ledRgb
	for (size_t i = 0; i < 3; i++) {
		mtk_os_hal_gpio_set_direction(ledRgb[i], OS_HAL_GPIO_DIR_OUTPUT);
		mtk_os_hal_gpio_set_output(ledRgb[i], true);
	}


	return status;
}
#else
bool initialize_hardware(void) {
	return true;
}
#endif


// Using default threadX 10ms tick period
void timer_scheduler(ULONG input) {
	static size_t intercoreTickCounter = 0;
	static size_t readSensorTickCounter = SIZE_MAX;
	ULONG status = TX_SUCCESS;

	if (hardwareInitOK == true) {
		readSensorTickCounter++;
		if (readSensorTickCounter >= sensorSampleRateInSeconds) {
			readSensorTickCounter = 0;
			status = tx_event_flags_set(&hardware_event_flags_0, 0x1, TX_OR);
			if (status != TX_SUCCESS) {
				printf("failed to set hardware event flags\r\n");
			}
		}

		intercoreTickCounter++;
		if (intercoreTickCounter >= 25)  // 250ms = 0.25 seconds.
		{
			intercoreTickCounter = 0;
			status = tx_event_flags_set(&Intercore_event_flags_0, 0x1, TX_OR);
			if (status != TX_SUCCESS) {
				printf("failed to set Intercore event flags\r\n");
			}
		}
	}
}

void send_intercore_msg(void) {
	memcpy((void*)&buf[payloadStart], (void*)&enviroment_control_block, sizeof(enviroment_control_block));
	dataSize = payloadStart + sizeof(enviroment_control_block);

	EnqueueData(inbound, outbound, sharedBufSize, buf, dataSize);
}

/*************************************************************************************************************************************
* This thread monitors intercore messages.
* There needs to be a shared understanding of the data structure being shared between the real-time and high-level apps
* This shared understanding is declared in the intercore_contract.h file.  This file can be found in the IntercoreContract directory.
* Update to match your requirements
*************************************************************************************************************************************/
void intercore_thread(ULONG thread_input) {
	UINT status = TX_SUCCESS;
	ULONG actual_flags;
	bool queuedMessages = true;

	if (GetIntercoreBuffers(&outbound, &inbound, &sharedBufSize) == -1) {
		return; // kill the thread
	}

	while (true) {
		status = tx_event_flags_get(&Intercore_event_flags_0, 0x1, TX_OR_CLEAR, &actual_flags, TX_WAIT_FOREVER);

		if ((status != TX_SUCCESS) || (actual_flags != 0x1)) { break; }

		queuedMessages = true;

		while (queuedMessages) {

			dataSize = sizeof(buf);

			int r = DequeueData(outbound, inbound, sharedBufSize, buf, &dataSize);

			if (r == 0 && dataSize > payloadStart) {
				memcpy((void*)&ic_control_block, (void*)&buf[payloadStart], sizeof(ic_control_block));

				switch (ic_control_block.cmd) {
				case IC_READ_SENSOR:
					send_intercore_msg();
					break;
				case IC_TARGET_TEMPERATURE:
					hvac_mode.target_temperature_set = true;
					hvac_mode.target_temperature = ic_control_block.temperature;
					set_hvac_operating_mode(hvac_mode.last_temperature);					
					break;
				default:
					break;
				}
			} else {
				queuedMessages = false;
			}
		}
	}
}

/// <summary>
/// Set the temperature status led. 
/// Red if HVAC needs to be turned on to get to desired temperature. 
/// Blue to turn on cooler. 
/// Green equals just right, no action required.
/// </summary>
void set_hvac_operating_mode(int temperature) {

	if (!hvac_mode.target_temperature_set) { return; }

	hvac_mode.current_led = temperature == hvac_mode.target_temperature ? HVAC_MODE_GREEN
			: temperature > hvac_mode.target_temperature ? HVAC_MODE_COOLING
			: HVAC_MODE_HEATING;

	if (hvac_mode.previous_led != hvac_mode.current_led) {
		// minus one as first item is HVAC_MODE_UNKNOWN
		mtk_os_hal_gpio_set_output(ledRgb[hvac_mode.previous_led - 1], true);
		hvac_mode.previous_led = hvac_mode.current_led;
	}

	enviroment_control_block.operating_mode = hvac_mode.current_led;
	// minus one as first item is HVAC_MODE_UNKNOWN
	mtk_os_hal_gpio_set_output(ledRgb[hvac_mode.current_led - 1], false);
}

// sensor read
#if defined(OEM_AVNET)
void read_sensor_thread(ULONG thread_input) {
	ULONG actual_flags;
	int rand_number;
	UINT status;

	srand((unsigned int)time(NULL)); // seed the random number generator for fake telemetry

	while (true) {
		status = tx_event_flags_get(&hardware_event_flags_0, 0x1, TX_OR_CLEAR, &actual_flags, TX_WAIT_FOREVER);

		if ((status != TX_SUCCESS) || (actual_flags != 0x1)) { break; }

		enviroment_control_block.cmd = IC_READ_SENSOR;

		enviroment_control_block.temperature = round(lp_get_temperature_lps22h());
		enviroment_control_block.pressure = round(lp_get_pressure());

		rand_number = rand() % 20;
		enviroment_control_block.humidity = 40.0 + rand_number;

		hvac_mode.last_temperature = enviroment_control_block.temperature;

		set_hvac_operating_mode(enviroment_control_block.temperature);
	}
}
#else
void read_sensor_thread(ULONG thread_input) {
	ULONG actual_flags;
	int rand_number;
	UINT status;

	srand((unsigned int)time(NULL)); // seed the random number generator for fake telemetry

	while (true) {
		// waits here until flag set in inter core thread
		status = tx_event_flags_get(&hardware_event_flags_0, 0x1, TX_OR_CLEAR, &actual_flags, TX_WAIT_FOREVER);

		if ((status != TX_SUCCESS) || (actual_flags != 0x1)) { break; }

		enviroment_control_block.cmd = IC_READ_SENSOR;

		rand_number = (rand() % 10);
		enviroment_control_block.temperature = (float)(15.0 + rand_number);

		rand_number = (rand() % 100);
		enviroment_control_block.pressure = (float)(950.0 + rand_number);

		rand_number = (rand() % 20);
		enviroment_control_block.humidity = (float)(40.0 + rand_number);

		hvac_mode.last_temperature = enviroment_control_block.temperature;

		set_hvac_operating_mode(enviroment_control_block.temperature);
	}
}
#endif


// only purpose in life is to initialize the hardware.
void hardware_init_thread(ULONG thread_input) {
	// printf("Hardware Init Thread - start: %u\r\n", millis());
	UINT status = TX_SUCCESS;

	// Initialize the hardware
	// hardwareInitOK = initialize_hardware();

	if (initialize_hardware()) {
		// start the timer.
		hardwareInitOK = true;
		status = tx_timer_create(&msTimer, "10ms Timer", timer_scheduler, 0, 1, 1, TX_AUTO_ACTIVATE);
		if (status != TX_SUCCESS) {
			printf("failed to create timer\r\n");
		} else {
			printf("timer created ok\r\n");
		}
	}

	printf("Hardware Init - %s\r\n", hardwareInitOK ? "OK" : "FAIL");
}
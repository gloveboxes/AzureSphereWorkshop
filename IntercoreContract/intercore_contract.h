#pragma once

typedef enum
{
	IC_UNKNOWN,
	IC_READ_SENSOR,
	IC_TARGET_TEMPERATURE
} INTERCORE_CMD;

typedef enum
{
	HVAC_MODE_UNKNOWN,
	HVAC_MODE_HEATING,
	HVAC_MODE_GREEN,
	HVAC_MODE_COOLING
} HVAC_OPERATING_MODE;

typedef struct
{
	INTERCORE_CMD cmd;
	int temperature;
	int pressure;
	int humidity;
	HVAC_OPERATING_MODE operating_mode;	
} INTERCORE_BLOCK;

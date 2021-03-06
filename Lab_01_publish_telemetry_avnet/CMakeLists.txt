#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

###################################################################################################################

# Select your developer board by removing the # tag from the beginning of the line

# The default board selected is the AVNET Azure Sphere Starter Kit Revision 1.

# If you are NOT using the AVNET Revision 1 board be sure to comment out the AVNET board

set(AVNET TRUE "AVNET Azure Sphere Starter Kit Revision 1 ")    
# set(AVNET_REV_2 TRUE "AVNET Azure Sphere Starter Kit Revision 2 ")             
# set(SEEED_STUDIO_RDB TRUE "Seeed Studio Azure Sphere MT3620 Development Kit (aka Reference Design Board or RDB)")
# set(SEEED_STUDIO_MINI TRUE "Seeed Studio Azure Sphere MT3620 Mini Dev Board")

###################################################################################################################

cmake_minimum_required (VERSION 3.10)

project (hvac_solution C)

azsphere_configure_tools(TOOLS_REVISION "21.07")
azsphere_configure_api(TARGET_API_SET "10")

add_subdirectory("AzureSphereDevX" out)

if(AVNET OR AVNET_REV_2)
    add_subdirectory("Drivers/AVNET/HL" drivers)
endif(AVNET OR AVNET_REV_2)

# Create executable
add_executable (${PROJECT_NAME} main.c onboard_sensors.c)
target_link_libraries (${PROJECT_NAME} applibs pthread gcc_s c azure_sphere_devx)
target_include_directories(${PROJECT_NAME} PUBLIC AzureSphereDevX/include )
target_include_directories(${PROJECT_NAME} PUBLIC ../IntercoreContract)

if(AVNET OR AVNET_REV_2)
    target_link_libraries (${PROJECT_NAME} avnet_drivers)
    target_include_directories(${PROJECT_NAME} PUBLIC Drivers/AVNET/HL)
endif(AVNET OR AVNET_REV_2)


if(AVNET)
    add_compile_definitions(OEM_AVNET_REV_1)
    add_compile_definitions(OEM_AVNET)
    azsphere_target_hardware_definition(${PROJECT_NAME} TARGET_DIRECTORY "../HardwareDefinitions/avnet_mt3620_sk" TARGET_DEFINITION "azure_sphere_learning_path.json")
endif(AVNET)

if(AVNET_REV_2)
    add_compile_definitions(OEM_AVNET_REV_2)
    add_compile_definitions(OEM_AVNET)
    azsphere_target_hardware_definition(${PROJECT_NAME} TARGET_DIRECTORY "../HardwareDefinitions/avnet_mt3620_sk_rev2" TARGET_DEFINITION "azure_sphere_learning_path.json")
endif(AVNET_REV_2)

if (SEEED_STUDIO_RDB)
    add_compile_definitions(OEM_SEEED_STUDIO_RDB)
    add_compile_definitions(OEM_SEEED_STUDIO)
    azsphere_target_hardware_definition(${PROJECT_NAME} TARGET_DIRECTORY "../HardwareDefinitions/mt3620_rdb" TARGET_DEFINITION "azure_sphere_learning_path.json")
endif()

if (SEEED_STUDIO_MINI)
    add_compile_definitions(OEM_SEEED_STUDIO_MINI)
    add_compile_definitions(OEM_SEEED_STUDIO)
    azsphere_target_hardware_definition(${PROJECT_NAME} TARGET_DIRECTORY "../HardwareDefinitions/seeed_mt3620_mdb" TARGET_DEFINITION "azure_sphere_learning_path.json")
endif(SEEED_STUDIO_MINI)


azsphere_target_add_image_package(${PROJECT_NAME})
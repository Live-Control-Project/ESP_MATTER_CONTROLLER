# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake) 


    if("${IDF_TARGET}" STREQUAL "esp32" OR "${IDF_TARGET}" STREQUAL "")
        set(ENV{ESP_MATTER_DEVICE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/components/esp-matter/device_hal/device/esp32_devkit_c")
    elseif("${IDF_TARGET}" STREQUAL "esp32c3")
        set(ENV{ESP_MATTER_DEVICE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/components/esp-matter/device_hal/device/esp32c3_devkit_m")
    elseif("${IDF_TARGET}" STREQUAL "esp32s3")
         set(ENV{ESP_MATTER_DEVICE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/components/esp-matter/device_hal/device/esp32s3_devkit_c")
    elseif("${IDF_TARGET}" STREQUAL "esp32c6")
         set(ENV{ESP_MATTER_DEVICE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/components/esp-matter/device_hal/device/esp32c6_devkit_c")
    else()
        message(FATAL_ERROR "Unsupported IDF_TARGET")
    endif()

set(ESP_MATTER_DEVICE_PATH "$ENV{ESP_MATTER_DEVICE_PATH}")
message(STATUS "Current IDF_TARGET = ${IDF_TARGET}")

include("${ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake")

get_filename_component(ESP_MATTER_COMPONETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/components/esp-matter" ABSOLUTE)

set(EXTRA_COMPONENT_DIRS
components
"${ESP_MATTER_COMPONETS_PATH}/device_hal/button_driver"
"${ESP_MATTER_COMPONETS_PATH}/device_hal"
"${ESP_MATTER_COMPONETS_PATH}/examples/common"  
    ${extra_components_dirs_append})

project(esp_matter_controller)

# set(ENABLE_MANAGED_COMPONENTS_CACHE 1)

idf_build_set_property(__BUILD_COMPONENT_DEPGRAPH_ENABLED 1)


idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)

# We don't need Thread Network Commissioning Driver
idf_build_set_property(COMPILE_OPTIONS "-D_NO_GENERIC_THREAD_NETWORK_COMMISSIONING_DRIVER_" APPEND)

# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security;-Wformat=0" APPEND)

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(BOOTLOADER_EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/bootloader_components" APPEND)
# ESP-SR prints Unicode while Ninja captures stdout using Windows' ANSI encoding.
# Apply UTF-8 to build tools even when invoking idf.py directly.
if(WIN32)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_CUSTOM
        "\"${CMAKE_COMMAND}\" -E env PYTHONIOENCODING=utf-8")
endif()
project(HisuOS VERSION 0.1.0)
include(cmake/battery_audio.cmake)
include(cmake/feedback_audio.cmake)

if(CONFIG_USJ_ENABLE_USB_SERIAL_JTAG OR CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED)
    message(FATAL_ERROR "GPIO20 drives the LCD backlight. Disable USB Serial/JTAG and select no secondary console in sdkconfig before building.")
endif()

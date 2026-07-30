cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Use the embedded toolchain by default. This must be set before project() so
# CLion's regular CMake profiles do not fall back to the host MinGW compiler.
# An explicitly supplied toolchain still takes precedence.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_CURRENT_LIST_DIR}/cmake/gcc-arm-none-eabi.cmake"
        CACHE FILEPATH "CMake toolchain file")
endif()
# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME H723ZG)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# CubeMX may regenerate its default linker script. Keep application-specific
# DMA memory placement in a separate linker script that CubeMX does not own.
set(APP_LINKER_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/linker/STM32H723xG_app.ld")
string(REPLACE
    "${CMAKE_CURRENT_LIST_DIR}/STM32H723xG_flash.ld"
    "${APP_LINKER_SCRIPT}"
    CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS}")

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
        App/Src/task_ui.c
        App/Inc/task_ui.h
        Drivers/AT24CS32/Src/at24cs32.c
        Drivers/AT24CS32/Inc/at24cs32.h
        Drivers/ESP32_PID/Src/esp32_pid_protocol.c
        Drivers/ESP32_PID/Inc/esp32_pid_protocol.h
        Drivers/ICM45686/Src/icm45686.c
        Drivers/ICM45686/Inc/icm45686.h
        Drivers/ST7789/Src/st7789.c
        Drivers/ST7789/Inc/st7789.h
        Drivers/DRV8870/Src/pid.c
        Drivers/DRV8870/Inc/pid.h
        Drivers/DRV8870/Src/pid_storage.c
        Drivers/DRV8870/Inc/pid_storage.h
        Drivers/DRV8870/Src/line_follower.c
        Drivers/DRV8870/Inc/line_follower.h
        Drivers/interrupt/Src/interrupt.c
        Drivers/interrupt/Inc/interrupt.h)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Drivers/DRV8870/Src/drv8870.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App/Inc
    Drivers/AT24CS32/Inc
    Drivers/ESP32_PID/Inc
    Drivers/ICM45686/Inc
    Drivers/ST7789/Inc
    Drivers/DRV8870/Inc
    Drivers/interrupt/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

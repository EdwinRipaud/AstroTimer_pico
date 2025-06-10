cmake_minimum_required(VERSION 3.13)
# set static environment variables
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_SDK_PATH ../../../../pico/pico-sdk)
set(FREERTOS_KERNEL_PATH ../../../../pico/FreeRTOS-Kernel)
set(PICO_BOARD pico2_w)
set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 3000)

# set wifi credentials
set(WIFI_PASSWORD "")
set(WIFI_SSID "AstroTimer_pico")

# include Pico SDK and FreeRTOS Kernel
include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

# set project name
set(PROGRAM_NAME AstroTimer_pico)
project(${PROGRAM_NAME} C CXX ASM)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# check for File System Builder
set(SIMPLE_FS_BUILDER_EXE ${CMAKE_CURRENT_SOURCE_DIR}/../Tools/SimpleFSBuilder/build/SimpleFSBuilder)
if (NOT EXISTS ${SIMPLE_FS_BUILDER_EXE})
    message(FATAL_ERROR "Missing ${SIMPLE_FS_BUILDER_EXE}. Please build it before building this project.")
endif()

# function to convert 'www' files to .fs
function(add_resource_folder target name path)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.fs ${CMAKE_CURRENT_BINARY_DIR}/__rerun_${name}.fs
        COMMAND ${SIMPLE_FS_BUILDER_EXE}
        ARGS ${path} ${CMAKE_CURRENT_BINARY_DIR}/${name}.fs
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating ${name}.fs")

    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o0
        COMMAND ${CMAKE_LINKER}
        ARGS -r -b binary ${name}.fs -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o0
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}.fs ${CMAKE_CURRENT_BINARY_DIR}/__rerun_${name}.fs
        COMMENT "Wrapping ${name}.fs")

    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND ${CMAKE_OBJCOPY}
        ARGS --rename-section .data=.rodata
             ${CMAKE_CURRENT_BINARY_DIR}/${name}.o0 ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}.o0
        COMMENT "Renaming ${name}")

    add_custom_target(${name}-fs DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    add_dependencies(${target} ${name}-fs)
    target_link_libraries(${target} ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
endfunction()

# executable to compile
add_executable(${PROGRAM_NAME}
    main.c
    dhcpserver/dhcpserver.c
    dnsserver/dnsserver.c
    httpserver.c
    server_settings.c
    json_parser.c
    timer_settings.c
    timer_core.c
    stream.c
    api_callbacks.c
    )

# create File System
add_resource_folder(${PROGRAM_NAME} www www)

# set default parameters
target_compile_definitions(${PROGRAM_NAME} PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    configNUMBER_OF_CORES=2
    NO_SYS=0
    )

target_include_directories(${PROGRAM_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    )

target_link_libraries(${PROGRAM_NAME}
    pico_stdlib
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_iperf
    FreeRTOS-Kernel-Heap4
    hardware_adc
    )

# enable USB output and disable UART output
pico_enable_stdio_usb(${PROGRAM_NAME} 1)
pico_enable_stdio_uart(${PROGRAM_NAME} 0)

pico_add_extra_outputs(${PROGRAM_NAME})

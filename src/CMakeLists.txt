cmake_minimum_required(VERSION 2.6)
project(ippusbxd)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -Wextra -pedantic -pedantic-errors")


# Compiler specific configuration
if (${CMAKE_C_COMPILER_ID} STREQUAL "GNU") 
    # Nothing
elseif( ${CMAKE_C_COMPILER_ID} STREQUAL "Clang") 
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Weverything -Wno-documentation -Wno-padded")
    # Warnings below this should be enabled. They are currently off because they
    # are REALLY noisy (bad thing)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-cast-align")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-sign-conversion")
endif()


set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})
find_package(LIBUSB REQUIRED)
set(LIBS ${LIBS} ${LIBSUSB_LIBRARIES})
include_directories(${LIBUSB_INCLUDE_DIR})


add_executable(ippusbxd
ippusbxd.c
http.c
tcp.c
usb.c
logging.c
)
target_link_libraries(ippusbxd ${LIBUSB_LIBRARIES})


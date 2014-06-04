cmake_minimum_required(VERSION 2.6)
project(ippusbd)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -Wextra")


set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})
find_package(LIBUSB REQUIRED)
set(LIBS ${LIBS} ${LIBSUSB_LIBRARIES})
include_directories(${LIBUSB_INCLUDE_DIR})


add_executable(ippusbd
ippusbd.c
http.c
tcp.c
usb.c
)
target_link_libraries(ippusbd ${LIBUSB_LIBRARIES})


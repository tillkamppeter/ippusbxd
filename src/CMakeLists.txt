cmake_minimum_required(VERSION 2.6)
project(ippusbd)


add_subdirectory(usb)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra")

include_directories(${LIBUSB_INCLUDE_DIR})
add_executable(ippusbd
	ippusbd.c
	http/http.c
	usb/usb.c
	)
target_link_libraries(ippusbd ${LIBUSB_LIBRARIES})

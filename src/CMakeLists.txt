cmake_minimum_required(VERSION 2.6)
project(ippusbxd)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -o2 -std=c99 -Wall -Wextra -pedantic -pedantic-errors")


# Compiler specific configuration
if (${CMAKE_C_COMPILER_ID} STREQUAL "GNU") 
    # Nothing
elseif( ${CMAKE_C_COMPILER_ID} STREQUAL "Clang") 
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Weverything -Wno-documentation -Wno-padded")
    # Compiler cannot detect that our logging functions really do use string
    # literals. So be careful to never-ever call printf with a user provided
    # format string since you will not get a warning. Coverity is smart enough
    # on this matter.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format-nonliteral")

    # Warnings below this should be enabled. They are currently off because they
    # are REALLY noisy (bad thing)
    #set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-sign-conversion")
endif()

# Pthreads
find_package(Threads REQUIRED)

# Libusb
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})
find_package(LIBUSB REQUIRED)
include_directories(${LIBUSB_INCLUDE_DIR})


add_executable(ippusbxd
ippusbxd.c
http.c
tcp.c
usb.c
logging.c
options.c
)
target_link_libraries(ippusbxd ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(ippusbxd ${LIBUSB_LIBRARIES})


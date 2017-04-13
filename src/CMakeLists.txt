cmake_minimum_required(VERSION 2.6)
project(ippusbxd)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -o2 -g -std=c99 -Wall -Wextra -pedantic -pedantic-errors")


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
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})

# Pthreads
find_package(Threads REQUIRED)

# Avahi
find_package(AVAHICOMMON REQUIRED)
find_package(AVAHICLIENT REQUIRED)

# Libusb
find_package(LIBUSB REQUIRED)
include_directories(${LIBUSB_INCLUDE_DIR})


add_executable(ippusbxd
ippusbxd.c
http.c
tcp.c
usb.c
logging.c
options.c
dnssd.c
)
target_link_libraries(ippusbxd ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(ippusbxd ${LIBUSB_LIBRARIES})
target_link_libraries(ippusbxd ${AVAHICOMMON_LIBRARIES})
target_link_libraries(ippusbxd ${AVAHICLIENT_LIBRARIES})


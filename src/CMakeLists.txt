cmake_minimum_required(VERSION 2.6)
project(ippusbd)

add_subdirectory(usb)

add_executable(ippusbd ippusbd.c)

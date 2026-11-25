cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define la variable de entorno VITASDK")
  endif()
endif()

project(gyroicons C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O2 -std=gnu99")

add_executable(gyroicons
  src/main.c
)

target_link_libraries(gyroicons
  SceLibKernel_stub
  SceMotion_stub
  taihen_stub
)

set_target_properties(gyroicons PROPERTIES LINK_FLAGS "-nostartfiles")
vita_create_self(gyroicons.suprx gyroicons CONFIG ${CMAKE_SOURCE_DIR}/gyroicons.yml UNSAFE)

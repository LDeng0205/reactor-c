cmake_minimum_required(VERSION 3.13)
project(ActionDelay LANGUAGES C)

# Require C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEFAULT_BUILD_TYPE Release)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE ${DEFAULT_BUILD_TYPE} CACHE STRING "Choose the type of build." FORCE)
endif()
set(CoreLib core)

# Check which system we are running on to select the correct platform support
# file and assign the file's path to LF_PLATFORM_FILE
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(LF_PLATFORM_FILE ${CoreLib}/platform/lf_linux_support.c)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(LF_PLATFORM_FILE ${CoreLib}/platform/lf_macos_support.c)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(LF_PLATFORM_FILE ${CoreLib}/platform/lf_windows_support.c)
    set(CMAKE_SYSTEM_VERSION 10.0)
    message("Using Windows SDK version ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
else()
    message(FATAL_ERROR "Your platform is not supported! The C target supports Linux, MacOS and Windows.")
endif()

include_directories(${CoreLib})
include_directories(${CoreLib}/platform)
include_directories(${CoreLib}/federated)

set(LF_MAIN_TARGET ActionDelay)
# Declare a new executable target and list all its sources
add_executable( ${LF_MAIN_TARGET} ActionDelay.cpp ${LF_PLATFORM_FILE} ctarget.c)

enable_language(CXX)
set_source_files_properties( ctarget.c PROPERTIES LANGUAGE CXX)
set_source_files_properties(${LF_PLATFORM_FILE} PROPERTIES LANGUAGE CXX)

install(TARGETS ${LF_MAIN_TARGET}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})


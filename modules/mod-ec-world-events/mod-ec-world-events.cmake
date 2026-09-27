#
# EC World Events Module - CMake Configuration
# Dynamic raid events with Playerbots integration for AzerothCore
#

message(STATUS "AUTOLOAD: mod-ec-world-events")

# Define the module source files
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ec_world_event_loader.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECWorldEventManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECWorldEventScript.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECWorldEventBridge.cpp")

message(STATUS "  EC World Events: Configuring module")

# Check if mod-playerbots exists and include its headers for the modules target
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src")
    message(STATUS "  EC World Events: mod-playerbots detected, enabling integration")

    # Add include directories for playerbots headers
    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Bot
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Ai
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Ai/Base
    )
else()
    message(STATUS "  EC World Events: mod-playerbots NOT found, bot spawning will be disabled")
endif()

# Check for mod-ec-realtime-bridge (shared TCP socket)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src")
    message(STATUS "  EC World Events: mod-ec-realtime-bridge detected, enabling bridge events")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_REALTIME_BRIDGE=1
    )
else()
    message(STATUS "  EC World Events: mod-ec-realtime-bridge NOT found, events disabled")
endif()

# Check for mod-alive-world integration (gossip, bot reactions)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-alive-world/src")
    message(STATUS "  EC World Events: mod-alive-world detected, enabling gossip integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-alive-world/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_ALIVE_WORLD=1
    )
else()
    message(STATUS "  EC World Events: mod-alive-world NOT found, gossip disabled")
endif()

message(STATUS "  EC World Events: Configuration complete")

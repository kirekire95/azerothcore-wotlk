#
# EC Guard System - CMake Configuration
# GTA-style wanted level system for AzerothCore
#

message(STATUS "AUTOLOAD: mod-ec-guard-system")

# Define the module
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ec_guard_system_loader.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECGuardSystemManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECGuardSystemScript.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECGuardSystemBridge.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECZoneState.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECZoneSecurityState.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECZoneEnvironment.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECZonePreparation.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECBountyHunterManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECWitnessSystem.cpp")

# Optional: Check if mod-playerbots exists for integration
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src")
    message(STATUS "  EC Guard System: mod-playerbots detected, enabling bot response")
    target_compile_definitions(modules PRIVATE EC_GUARD_SYSTEM_HAS_PLAYERBOTS)
else()
    message(STATUS "  EC Guard System: mod-playerbots NOT found, bot response disabled")
endif()

# Check for mod-ec-realtime-bridge (shared TCP socket)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src")
    message(STATUS "  EC Guard System: mod-ec-realtime-bridge detected, enabling bridge events")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_REALTIME_BRIDGE=1
    )
else()
    message(STATUS "  EC Guard System: mod-ec-realtime-bridge NOT found, events disabled")
endif()

# Check for mod-alive-world integration (gossip, bot reactions to crimes)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-alive-world/src")
    message(STATUS "  EC Guard System: mod-alive-world detected, enabling AliveWorld integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-alive-world/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_ALIVE_WORLD=1
    )
else()
    message(STATUS "  EC Guard System: mod-alive-world NOT found, AliveWorld integration disabled")
endif()

# Check for mod-ec-world-events integration (elite response events)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-world-events/src")
    message(STATUS "  EC Guard System: mod-ec-world-events detected, enabling elite defense events")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-world-events/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_WORLD_EVENTS=1
    )
else()
    message(STATUS "  EC Guard System: mod-ec-world-events NOT found, elite defense events disabled")
endif()

# Check for mod-gtapolice integration (GTA-style police pursuit)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-gtapolice/src")
    message(STATUS "  EC Guard System: mod-gtapolice detected, enabling police auto-dispatch")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-gtapolice/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_GTA_POLICE=1
    )
else()
    message(STATUS "  EC Guard System: mod-gtapolice NOT found, police auto-dispatch disabled")
endif()

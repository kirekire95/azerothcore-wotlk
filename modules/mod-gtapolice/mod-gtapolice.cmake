#
# mod-gtapolice - GTA-style police pursuit, arrest, and jail system
#

message(STATUS "AUTOLOAD: mod-gtapolice")

# Source files
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/mod_gtapolice.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPoliceConfig.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPoliceBridge.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPoliceManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPoliceCarAI.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPoliceOfficerAI.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPoliceCrewAI.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPoliceCommands.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GTAPolicePaynSpray.cpp")

# Check for mod-ec-realtime-bridge (shared TCP socket for events)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src")
    message(STATUS "  mod-gtapolice: mod-ec-realtime-bridge detected, enabling bridge events")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_REALTIME_BRIDGE=1
    )
else()
    message(STATUS "  mod-gtapolice: mod-ec-realtime-bridge NOT found, bridge events disabled")
endif()

# Check for mod-ec-guard-system (wanted level integration)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-guard-system/src")
    message(STATUS "  mod-gtapolice: mod-ec-guard-system detected, enabling wanted level integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-guard-system/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_GUARD_SYSTEM=1
    )
else()
    message(STATUS "  mod-gtapolice: mod-ec-guard-system NOT found, using fallback wanted aura")
endif()

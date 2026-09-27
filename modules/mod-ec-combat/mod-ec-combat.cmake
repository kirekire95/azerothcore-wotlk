#
# EC Combat Module - CMake Configuration
# Kill feed, arena/BG tracking, PvP statistics for AzerothCore
#

message(STATUS "AUTOLOAD: mod-ec-combat")

# Define the module
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ec_combat_loader.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECCombatManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECCombatScript.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ECCombatBridge.cpp")

# Check for mod-ec-realtime-bridge (shared TCP socket)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src")
    message(STATUS "  EC Combat: mod-ec-realtime-bridge detected, enabling bridge events")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_REALTIME_BRIDGE=1
    )
else()
    message(STATUS "  EC Combat: mod-ec-realtime-bridge NOT found, events disabled")
endif()

# Check for mod-alive-world integration (bot reactions to kills)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-alive-world/src")
    message(STATUS "  EC Combat: mod-alive-world detected, enabling AliveWorld integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-alive-world/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_ALIVE_WORLD=1
    )
else()
    message(STATUS "  EC Combat: mod-alive-world NOT found, AliveWorld integration disabled")
endif()

# Check for mod-ec-guard-system integration (wanted level on PvP kills)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-guard-system/src")
    message(STATUS "  EC Combat: mod-ec-guard-system detected, enabling guard integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-guard-system/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_GUARD_SYSTEM=1
    )
else()
    message(STATUS "  EC Combat: mod-ec-guard-system NOT found, guard integration disabled")
endif()

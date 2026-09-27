#
# mod-ec-realtime-bridge - Shared Realtime Communication Bridge
# Copyright (C) 2025 EC
#
# This file is included by modules/CMakeLists.txt
#
# Provides a shared TCP socket infrastructure for all EC modules
# to communicate with the Node.js backend (Azeroth Command Console).
#
# Architecture:
#   C++ (AzerothCore) ──TCP/JSON──> Node.js (RealtimeBridgeService)
#                                         │
#                                         ├── GraphQL Subscriptions
#                                         └── Event Handlers
#
# Usage by other modules:
#   #ifdef MOD_EC_REALTIME_BRIDGE
#   #include "RealtimeBridgeSocket.h"
#   sRealtimeBridge->EmitEvent(EventNamespace::MY_NS, "event_type", jsonPayload);
#   #endif
#

message(STATUS "AUTOLOAD: mod-ec-realtime-bridge")

# ============================================================================
# SOURCE FILE REGISTRATION
# ============================================================================
# Register all source files for compilation with AzerothCore

AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ec_realtime_bridge_loader.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/RealtimeBridgeTypes.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/RealtimeBridgeSocket.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/RealtimeBridgeScripts.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ScholarshipManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/PlayerbotsBridgeAdapter.cpp")

message(STATUS "  EC Realtime Bridge: Registered 6 source files")

# ============================================================================
# PUBLIC HEADERS & DEFINITIONS
# ============================================================================
# This module should be loaded early so other modules can use it.
# Add include directory so other modules can #include our headers.

target_include_directories(modules
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src
)

# Define that the bridge is available for conditional compilation.
# Other modules check: #ifdef MOD_EC_REALTIME_BRIDGE
target_compile_definitions(modules
    PUBLIC
        MOD_EC_REALTIME_BRIDGE=1
)

message(STATUS "  EC Realtime Bridge: Configuration complete")

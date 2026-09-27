#
# mod-alive-world - Alive World System for AzerothCore
# Copyright (C) 2025 EC
#
# This file is included by modules/CMakeLists.txt
#
# Creates a living, breathing game world where bots perceive, remember,
# and react to world events with AI-powered decision making.
#
# Features:
#   - Event Collection: Tracks combat, movement, chat, zone changes
#   - World State: Aggregates zone populations, faction control, threats
#   - Bot Memory: Per-bot memory with relationship tracking
#   - Intelligence Tiers: Tiered AI from basic reactions to full LLM
#   - AI Decisions: Context-aware action planning for bots
#
# Communication Architecture:
#   All communication with Node.js backend uses mod-ec-realtime-bridge.
#   The BridgeAdapter handles bidirectional messaging over a single TCP socket:
#     - Events (C++ → Node.js): Push real-time state changes
#     - Queries (Node.js → C++): Request data with requestId, receive response
#     - Commands (Node.js → C++): Execute bot actions, set goals
#
# Dependencies:
#   - mod-playerbots (required): Bot control and spawning
#   - mod-ec-realtime-bridge (required): Unified TCP communication
#   - mod-ollama-chat (optional): LLM integration for Tier 3+
#   - mod-ec-world-events (optional): Dynamic event participation
#   - mod-ec-guard-system (optional): Crime event reactions
#

message(STATUS "AUTOLOAD: mod-alive-world")

# ============================================================================
# SOURCE FILE REGISTRATION
# ============================================================================
# Register all source files for compilation with AzerothCore

# Core system files
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/alive_world_loader.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/AliveWorldManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/AliveWorldScripts.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/AliveWorldBridgeAdapter.cpp")

# Event and state management
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/EventCollector.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/WorldStateManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/AliveWorldEventHooks.cpp")

# Bot intelligence
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/BotMemoryManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/IntelligenceTierManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/AIDecisionEngine.cpp")

# Behavior and gossip systems
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/BehaviorExecutor.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/StrategyParser.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GossipManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GossipTemplateManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/EmoteReactionSystem.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GoalSystem.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GoalManager.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/SpeechPatternManager.cpp")

# Economy system
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/EconomySystem.cpp")

message(STATUS "  Alive World: Registered 20 source files")

# ============================================================================
# OPTIONAL DEPENDENCIES
# ============================================================================

message(STATUS "  Alive World: Configuring dependencies")

# Check for mod-playerbots integration (required for bot control)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src")
    message(STATUS "  Alive World: mod-playerbots detected, enabling bot integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Bot
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Ai
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Ai/Base
            ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Strategy
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_PLAYERBOTS=1
    )
else()
    message(STATUS "  Alive World: mod-playerbots NOT found - bot integration disabled")
endif()

# Check for mod-ollama-chat integration (required for LLM)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ollama-chat/src")
    message(STATUS "  Alive World: mod-ollama-chat detected, enabling LLM integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ollama-chat/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_OLLAMA_CHAT=1
    )
else()
    message(STATUS "  Alive World: mod-ollama-chat NOT found - LLM features disabled")
endif()

# Check for mod-ec-world-events integration
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-world-events/src")
    message(STATUS "  Alive World: mod-ec-world-events detected, enabling world events integration")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-world-events/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_WORLD_EVENTS=1
    )
else()
    message(STATUS "  Alive World: mod-ec-world-events NOT found - world events integration disabled")
endif()

# ============================================================================
# mod-ec-realtime-bridge (REQUIRED - unified communication)
# ============================================================================
# All communication between C++ and Node.js goes through the realtime bridge.
# This replaces the old HTTP server approach with a single TCP socket that handles:
#   - Event streaming (C++ -> Node.js): gossip, tier changes, goals, zone state
#   - Request/Response queries (Node.js -> C++): get bots, get gossip, get stats
#   - Command execution (Node.js -> C++): bot control, goal setting
#
# The BridgeAdapter (AliveWorldBridgeAdapter.cpp) handles all message routing.
# ============================================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src")
    message(STATUS "  Alive World: mod-ec-realtime-bridge detected - using unified bridge communication")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-realtime-bridge/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_REALTIME_BRIDGE=1
    )
else()
    message(WARNING "  Alive World: mod-ec-realtime-bridge NOT found - THIS IS REQUIRED!")
    message(WARNING "  Alive World: Without the bridge, no communication with Node.js backend is possible.")
endif()

# Check for mod-ec-guard-system integration (crime events, wanted levels)
if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-ec-guard-system/src")
    message(STATUS "  Alive World: mod-ec-guard-system detected, enabling crime event reactions")

    target_include_directories(modules
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/mod-ec-guard-system/src
    )

    target_compile_definitions(modules
        PRIVATE
            MOD_EC_GUARD_SYSTEM=1
    )
else()
    message(STATUS "  Alive World: mod-ec-guard-system NOT found - crime reactions disabled")
endif()

message(STATUS "  Alive World: Configuration complete")

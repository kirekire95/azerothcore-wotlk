/*
 * mod-ec-realtime-bridge - TCP Socket Communication
 * Copyright (C) 2025 EC
 *
 * Shared TCP socket singleton for all EC modules to communicate
 * with the Node.js backend. Provides:
 *   - Automatic connection and reconnection
 *   - Event queue with backpressure handling
 *   - Command routing to registered handlers
 *   - Authentication handshake
 *   - Heartbeat monitoring
 *
 * Protocol:
 *   - Messages are newline-delimited JSON
 *   - Each message has "ns" (namespace) and "type" fields
 *   - Supports bidirectional communication
 */

#ifndef EC_REALTIME_BRIDGE_SOCKET_H
#define EC_REALTIME_BRIDGE_SOCKET_H

#include "RealtimeBridgeTypes.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace ECRealtimeBridge
{

// ============================================================================
// SOCKET CONFIGURATION
// ============================================================================

struct SocketConfig
{
    std::string host = "127.0.0.1";
    uint16_t port = 8092;
    std::string sharedSecret = "";
    uint32_t reconnectIntervalMs = 5000;
    uint32_t heartbeatIntervalMs = 10000;
    uint32_t connectionTimeoutMs = 5000;
    uint32_t maxQueueSize = 10000;
    uint32_t maxMessageSize = 1048576;  // 1MB
    bool enabled = true;
};

// ============================================================================
// REALTIME BRIDGE SOCKET
// ============================================================================

class RealtimeBridgeSocket
{
public:
    static RealtimeBridgeSocket* Instance();

    // Lifecycle
    void Initialize();
    void Shutdown();
    bool IsConnected() const { return _connected.load(); }
    bool IsAuthenticated() const { return _authenticated.load(); }
    bool IsEnabled() const { return _config.enabled; }

    // Configuration
    void LoadConfig();
    void SetConfig(const SocketConfig& config) { _config = config; }
    const SocketConfig& GetConfig() const { return _config; }

    // Event emission (C++ -> Node.js)
    void EmitEvent(const BridgeEvent& event);
    void EmitEvent(EventNamespace ns, const std::string& type, const std::string& jsonPayload = "{}");

    // Command handling (Node.js -> C++)
    void RegisterHandler(EventNamespace ns, CommandHandler handler);
    void UnregisterHandler(EventNamespace ns);
    void ProcessPendingCommands();

    // Response/ACK methods (for request/response pattern)
    void SendResponse(EventNamespace ns, const std::string& requestId, const std::string& jsonData);
    void SendErrorResponse(EventNamespace ns, const std::string& requestId, const std::string& error, int code = 400);
    void SendAck(EventNamespace ns, const std::string& requestId, bool success = true);

    // Statistics
    BridgeStats GetStats() const;
    void ResetStats();

    // Debug
    void SetDebugMode(bool enabled) { _debugMode = enabled; }
    bool IsDebugMode() const { return _debugMode; }
    void DumpState();

private:
    RealtimeBridgeSocket();
    ~RealtimeBridgeSocket();

    // Disable copy
    RealtimeBridgeSocket(const RealtimeBridgeSocket&) = delete;
    RealtimeBridgeSocket& operator=(const RealtimeBridgeSocket&) = delete;

    // Network threads
    void ConnectionThread();
    void SendThread();
    void ReceiveThread();

    // Connection management
    bool Connect();
    void Disconnect();
    void ScheduleReconnect();
    void SendAuthHandshake();

    // Message handling
    bool SendMessage(const std::string& json);
    void HandleIncomingMessage(const std::string& json);
    BridgeCommand ParseCommand(const std::string& json);

    // Heartbeat
    void SendHeartbeat();

private:
    // Configuration
    SocketConfig _config;
    bool _debugMode = false;

    // Socket
    int _socketFd = -1;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _authenticated{false};
    std::atomic<bool> _shouldRun{false};

    // Threads
    std::unique_ptr<std::thread> _connectionThread;
    std::unique_ptr<std::thread> _sendThread;
    std::unique_ptr<std::thread> _receiveThread;

    // Outgoing event queue
    std::queue<BridgeEvent> _eventQueue;
    mutable std::mutex _eventQueueMutex;
    std::condition_variable _eventQueueCV;

    // Incoming command queue
    std::queue<BridgeCommand> _commandQueue;
    std::mutex _commandQueueMutex;

    // Registered command handlers (one per namespace)
    std::map<EventNamespace, CommandHandler> _handlers;
    std::mutex _handlersMutex;

    // Statistics
    std::atomic<uint64_t> _eventsSent{0};
    std::atomic<uint64_t> _eventsDropped{0};
    std::atomic<uint64_t> _commandsReceived{0};
    std::atomic<uint64_t> _commandsProcessed{0};
    std::atomic<uint64_t> _bytesOut{0};
    std::atomic<uint64_t> _bytesIn{0};
    std::atomic<uint64_t> _reconnectCount{0};
    std::chrono::steady_clock::time_point _startTime;

    // Heartbeat
    std::chrono::steady_clock::time_point _lastHeartbeat;
    std::chrono::steady_clock::time_point _lastDaemonResponse;
};

#define sRealtimeBridge ECRealtimeBridge::RealtimeBridgeSocket::Instance()

} // namespace ECRealtimeBridge

#endif // EC_REALTIME_BRIDGE_SOCKET_H

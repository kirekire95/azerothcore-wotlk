/*
 * mod-ec-realtime-bridge - TCP Socket Implementation
 * Copyright (C) 2025 EC
 */

#include "RealtimeBridgeSocket.h"
#include "TcpEndpointConnector.h"
#include "Config.h"
#include "Log.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace ECRealtimeBridge
{

// ============================================================================
// SINGLETON
// ============================================================================

RealtimeBridgeSocket* RealtimeBridgeSocket::Instance()
{
    static RealtimeBridgeSocket instance;
    return &instance;
}

RealtimeBridgeSocket::RealtimeBridgeSocket()
    : _socketFd(-1)
{
    _startTime = std::chrono::steady_clock::now();
}

RealtimeBridgeSocket::~RealtimeBridgeSocket()
{
    Shutdown();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void RealtimeBridgeSocket::LoadConfig()
{
    _config.enabled = sConfigMgr->GetOption<bool>("ECRealtimeBridge.Enable", true);
    _config.host = sConfigMgr->GetOption<std::string>("ECRealtimeBridge.Host", "127.0.0.1");
    _config.port = sConfigMgr->GetOption<uint16_t>("ECRealtimeBridge.Port", 8092);
    _config.sharedSecret = sConfigMgr->GetOption<std::string>("ECRealtimeBridge.SharedSecret", "");
    _config.reconnectIntervalMs = sConfigMgr->GetOption<uint32_t>("ECRealtimeBridge.ReconnectIntervalMs", 5000);
    _config.heartbeatIntervalMs = sConfigMgr->GetOption<uint32_t>("ECRealtimeBridge.HeartbeatIntervalMs", 10000);
    _config.connectionTimeoutMs = sConfigMgr->GetOption<uint32_t>("ECRealtimeBridge.ConnectionTimeoutMs", 5000);
    _config.maxQueueSize = sConfigMgr->GetOption<uint32_t>("ECRealtimeBridge.MaxQueueSize", 10000);
    _config.maxMessageSize = sConfigMgr->GetOption<uint32_t>("ECRealtimeBridge.MaxMessageSize", 1048576);
    _debugMode = sConfigMgr->GetOption<bool>("ECRealtimeBridge.Debug", false);

    LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Config loaded - {}:{}, enabled={}",
        _config.host, _config.port, _config.enabled ? "yes" : "no");
}

void RealtimeBridgeSocket::Initialize()
{
    LoadConfig();

    if (!_config.enabled)
    {
        LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Disabled by configuration");
        return;
    }

    LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Initializing socket to {}:{}",
        _config.host, _config.port);

    _shouldRun = true;
    _startTime = std::chrono::steady_clock::now();

    // Start network threads
    _connectionThread = std::make_unique<std::thread>(&RealtimeBridgeSocket::ConnectionThread, this);
    _sendThread = std::make_unique<std::thread>(&RealtimeBridgeSocket::SendThread, this);
    _receiveThread = std::make_unique<std::thread>(&RealtimeBridgeSocket::ReceiveThread, this);

    LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Socket threads started");
}

void RealtimeBridgeSocket::Shutdown()
{
    if (!_shouldRun)
        return;

    LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Shutting down...");

    // Send shutdown event
    EmitEvent(EventNamespace::SYSTEM, "shutdown");

    _shouldRun = false;
    _eventQueueCV.notify_all();

    // Wait for threads
    if (_connectionThread && _connectionThread->joinable())
        _connectionThread->join();
    if (_sendThread && _sendThread->joinable())
        _sendThread->join();
    if (_receiveThread && _receiveThread->joinable())
        _receiveThread->join();

    Disconnect();

    LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Shutdown complete");
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool RealtimeBridgeSocket::Connect()
{
    if (_socketFd >= 0)
        Disconnect();

    // 2026-09-27: Resolve service names again on every connection attempt.
    std::string connectionError;
    _socketFd = ConnectResolvedTcp(_config.host, _config.port, _config.connectionTimeoutMs,
        _shouldRun, connectionError);
    if (_socketFd < 0)
    {
        LOG_ERROR("module.realtime-bridge", "ECRealtimeBridge: Cannot connect to {}:{}: {}",
            _config.host, _config.port, connectionError);
        return false;
    }

    _connected = true;
    _lastDaemonResponse = std::chrono::steady_clock::now();

    LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Connected to {}:{}",
        _config.host, _config.port);

    // Send authentication handshake
    SendAuthHandshake();

    // Send startup event
    EmitEvent(EventNamespace::SYSTEM, "startup");

    return true;
}

void RealtimeBridgeSocket::Disconnect()
{
    if (_socketFd >= 0)
    {
        close(_socketFd);
        _socketFd = -1;
    }

    _connected = false;
    _authenticated = false;
}

void RealtimeBridgeSocket::ScheduleReconnect()
{
    Disconnect();
    _reconnectCount++;

    if (_debugMode)
    {
        LOG_DEBUG("module.realtime-bridge", "ECRealtimeBridge: Scheduling reconnect in {}ms",
            _config.reconnectIntervalMs);
    }
}

void RealtimeBridgeSocket::SendAuthHandshake()
{
    if (_config.sharedSecret.empty())
    {
        _authenticated = true;
        return;
    }

    std::ostringstream ss;
    ss << "{\"ns\":\"system\",\"type\":\"auth\","
       << "\"secret\":\"" << EscapeJsonString(_config.sharedSecret) << "\""
       << "}";

    if (SendMessage(ss.str()))
    {
        if (_debugMode)
        {
            LOG_DEBUG("module.realtime-bridge", "ECRealtimeBridge: Auth handshake sent");
        }
    }
}

// ============================================================================
// EVENT EMISSION
// ============================================================================

void RealtimeBridgeSocket::EmitEvent(const BridgeEvent& event)
{
    if (!_config.enabled)
        return;

    std::lock_guard<std::mutex> lock(_eventQueueMutex);

    // Check queue size for backpressure
    if (_eventQueue.size() >= _config.maxQueueSize)
    {
        _eventsDropped++;
        if (_debugMode && (_eventsDropped % 100) == 1)
        {
            LOG_WARN("module.realtime-bridge",
                "ECRealtimeBridge: Queue full, dropping events (dropped: {})",
                _eventsDropped.load());
        }
        return;
    }

    _eventQueue.push(event);
    _eventQueueCV.notify_one();
}

void RealtimeBridgeSocket::EmitEvent(EventNamespace ns, const std::string& type,
    const std::string& jsonPayload)
{
    EmitEvent(BridgeEvent(ns, type, jsonPayload));
}

// ============================================================================
// COMMAND HANDLING
// ============================================================================

void RealtimeBridgeSocket::RegisterHandler(EventNamespace ns, CommandHandler handler)
{
    std::lock_guard<std::mutex> lock(_handlersMutex);
    _handlers[ns] = handler;

    LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Registered handler for namespace '{}'",
        NamespaceToString(ns));
}

void RealtimeBridgeSocket::UnregisterHandler(EventNamespace ns)
{
    std::lock_guard<std::mutex> lock(_handlersMutex);
    _handlers.erase(ns);
}

void RealtimeBridgeSocket::ProcessPendingCommands()
{
    std::queue<BridgeCommand> toProcess;

    {
        std::lock_guard<std::mutex> lock(_commandQueueMutex);
        std::swap(toProcess, _commandQueue);
    }

    while (!toProcess.empty())
    {
        BridgeCommand cmd = toProcess.front();
        toProcess.pop();

        CommandHandler handler = nullptr;
        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            auto it = _handlers.find(cmd.ns);
            if (it != _handlers.end())
                handler = it->second;
        }

        if (handler)
        {
            try
            {
                handler(cmd);
                _commandsProcessed++;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("module.realtime-bridge",
                    "ECRealtimeBridge: Handler exception for {}: {}",
                    NamespaceToString(cmd.ns), e.what());
            }
        }
        else if (_debugMode)
        {
            LOG_DEBUG("module.realtime-bridge",
                "ECRealtimeBridge: No handler for namespace '{}'",
                NamespaceToString(cmd.ns));
        }
    }
}

// ============================================================================
// NETWORK THREADS
// ============================================================================

void RealtimeBridgeSocket::ConnectionThread()
{
    while (_shouldRun)
    {
        if (!_connected)
        {
            if (Connect())
            {
                LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Connection established");
            }
            else
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(_config.reconnectIntervalMs));
            }
        }
        else
        {
            // Check for heartbeat timeout
            auto now = std::chrono::steady_clock::now();
            auto lastResponse = _lastDaemonResponse;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastResponse).count();

            if (elapsed > _config.heartbeatIntervalMs * 3)
            {
                LOG_WARN("module.realtime-bridge",
                    "ECRealtimeBridge: Heartbeat timeout, reconnecting...");
                ScheduleReconnect();
            }

            // Send heartbeat
            auto lastHb = _lastHeartbeat;
            auto hbElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastHb).count();

            if (hbElapsed >= _config.heartbeatIntervalMs)
            {
                SendHeartbeat();
                _lastHeartbeat = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
}

void RealtimeBridgeSocket::SendThread()
{
    while (_shouldRun)
    {
        BridgeEvent event;

        {
            std::unique_lock<std::mutex> lock(_eventQueueMutex);
            _eventQueueCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !_eventQueue.empty() || !_shouldRun;
            });

            if (!_shouldRun)
                break;

            if (_eventQueue.empty())
                continue;

            event = _eventQueue.front();
            _eventQueue.pop();
        }

        if (!_connected)
            continue;

        std::string json = event.ToJson();
        if (SendMessage(json))
        {
            _eventsSent++;
            _bytesOut += json.size();
        }
    }
}

void RealtimeBridgeSocket::ReceiveThread()
{
    std::string buffer;
    buffer.reserve(_config.maxMessageSize);
    char readBuf[4096];

    while (_shouldRun)
    {
        if (!_connected)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Read from socket
        ssize_t bytesRead = recv(_socketFd, readBuf, sizeof(readBuf), MSG_DONTWAIT);

        if (bytesRead > 0)
        {
            _bytesIn += bytesRead;
            _lastDaemonResponse = std::chrono::steady_clock::now();

            buffer.append(readBuf, bytesRead);

            // Check for max message size
            if (buffer.size() > _config.maxMessageSize)
            {
                LOG_WARN("module.realtime-bridge",
                    "ECRealtimeBridge: Buffer overflow, clearing");
                buffer.clear();
                continue;
            }

            // Process complete messages (newline-delimited)
            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos)
            {
                std::string message = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

                if (!message.empty())
                {
                    HandleIncomingMessage(message);
                }
            }
        }
        else if (bytesRead == 0)
        {
            // Connection closed
            LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Connection closed by server");
            ScheduleReconnect();
        }
        else if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            LOG_ERROR("module.realtime-bridge", "ECRealtimeBridge: Receive error: {}",
                strerror(errno));
            ScheduleReconnect();
        }
        else
        {
            // No data available, brief sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// ============================================================================
// MESSAGE HANDLING
// ============================================================================

bool RealtimeBridgeSocket::SendMessage(const std::string& json)
{
    if (!_connected || _socketFd < 0)
        return false;

    std::string message = json + "\n";
    ssize_t totalSent = 0;
    size_t remaining = message.size();

    int retryCount = 0;
    static constexpr int MAX_SEND_RETRIES = 500; // 500ms max wait

    while (remaining > 0)
    {
        ssize_t sent = send(_socketFd, message.c_str() + totalSent, remaining, MSG_NOSIGNAL);

        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (++retryCount > MAX_SEND_RETRIES)
                {
                    LOG_ERROR("module.realtime-bridge",
                        "ECRealtimeBridge: Send stalled after %d retries, reconnecting", MAX_SEND_RETRIES);
                    ScheduleReconnect();
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            LOG_ERROR("module.realtime-bridge", "ECRealtimeBridge: Send error: {}",
                strerror(errno));
            ScheduleReconnect();
            return false;
        }

        retryCount = 0; // Reset on successful send
        totalSent += sent;
        remaining -= sent;
    }

    return true;
}

void RealtimeBridgeSocket::HandleIncomingMessage(const std::string& json)
{
    _commandsReceived++;

    if (_debugMode)
    {
        LOG_DEBUG("module.realtime-bridge", "ECRealtimeBridge: Received: {}",
            json.substr(0, 200));
    }

    BridgeCommand cmd = ParseCommand(json);

    // Handle system commands internally
    if (cmd.ns == EventNamespace::SYSTEM)
    {
        if (cmd.type == "auth_ok")
        {
            _authenticated = true;
            LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: Authentication successful");
            return;
        }
        else if (cmd.type == "auth_fail")
        {
            _authenticated = false;
            LOG_ERROR("module.realtime-bridge", "ECRealtimeBridge: Authentication failed");
            return;
        }
        else if (cmd.type == "heartbeat")
        {
            // Just update last response time (already done in receive)
            return;
        }
    }

    // Queue command for processing
    {
        std::lock_guard<std::mutex> lock(_commandQueueMutex);
        _commandQueue.push(cmd);
    }
}

BridgeCommand RealtimeBridgeSocket::ParseCommand(const std::string& json)
{
    BridgeCommand cmd;

    std::string nsStr = ExtractJsonString(json, "ns");
    cmd.ns = StringToNamespace(nsStr);
    cmd.type = ExtractJsonString(json, "type");
    cmd.requestId = ExtractJsonString(json, "requestId");
    cmd.jsonPayload = json;

    if (_debugMode && !cmd.requestId.empty())
    {
        LOG_DEBUG("module.realtime-bridge",
            "ECRealtimeBridge: Parsed command with requestId='{}' type='{}'",
            cmd.requestId, cmd.type);
    }

    return cmd;
}

void RealtimeBridgeSocket::SendHeartbeat()
{
    BridgeStats stats = GetStats();

    std::ostringstream ss;
    ss << "{\"ns\":\"system\",\"type\":\"heartbeat\","
       << "\"stats\":{"
       << "\"eventsSent\":" << stats.eventsSent << ","
       << "\"commandsReceived\":" << stats.commandsReceived << ","
       << "\"queueSize\":" << stats.queueSize << ","
       << "\"uptimeSeconds\":" << stats.uptimeSeconds
       << "}}";

    EmitEvent(EventNamespace::SYSTEM, "heartbeat", ss.str());
}

// ============================================================================
// RESPONSE/ACK METHODS
// ============================================================================

void RealtimeBridgeSocket::SendResponse(EventNamespace ns, const std::string& requestId,
    const std::string& jsonData)
{
    if (requestId.empty())
        return;

    std::ostringstream ss;
    ss << "{\"ns\":\"" << NamespaceToString(ns) << "\","
       << "\"type\":\"response\","
       << "\"requestId\":\"" << EscapeJsonString(requestId) << "\","
       << "\"data\":" << jsonData << "}";

    BridgeEvent event(ns, "response", ss.str());
    event.jsonPayload = ss.str();  // Override default payload construction

    // Send directly as the full message is already constructed
    if (_connected && _socketFd >= 0)
    {
        std::string fullJson = "{\"ns\":\"" + std::string(NamespaceToString(ns)) + "\","
            + "\"type\":\"response\","
            + "\"requestId\":\"" + EscapeJsonString(requestId) + "\","
            + "\"data\":" + jsonData + "}";
        SendMessage(fullJson);
        _eventsSent++;
    }
}

void RealtimeBridgeSocket::SendErrorResponse(EventNamespace ns, const std::string& requestId,
    const std::string& error, int code)
{
    if (requestId.empty())
        return;

    std::string fullJson = "{\"ns\":\"" + std::string(NamespaceToString(ns)) + "\","
        + "\"type\":\"error\","
        + "\"requestId\":\"" + EscapeJsonString(requestId) + "\","
        + "\"error\":\"" + EscapeJsonString(error) + "\","
        + "\"code\":" + std::to_string(code) + "}";

    if (_connected && _socketFd >= 0)
    {
        SendMessage(fullJson);
        _eventsSent++;
    }
}

void RealtimeBridgeSocket::SendAck(EventNamespace ns, const std::string& requestId, bool success)
{
    if (requestId.empty())
        return;

    std::string fullJson = "{\"ns\":\"" + std::string(NamespaceToString(ns)) + "\","
        + "\"type\":\"ack\","
        + "\"requestId\":\"" + EscapeJsonString(requestId) + "\","
        + "\"success\":" + (success ? "true" : "false") + "}";

    if (_connected && _socketFd >= 0)
    {
        SendMessage(fullJson);
        _eventsSent++;
    }
}

// ============================================================================
// STATISTICS
// ============================================================================

BridgeStats RealtimeBridgeSocket::GetStats() const
{
    BridgeStats stats;
    stats.eventsSent = _eventsSent.load();
    stats.eventsDropped = _eventsDropped.load();
    stats.commandsReceived = _commandsReceived.load();
    stats.commandsProcessed = _commandsProcessed.load();
    stats.bytesOut = _bytesOut.load();
    stats.bytesIn = _bytesIn.load();
    stats.reconnectCount = _reconnectCount.load();
    stats.connected = _connected.load();
    stats.authenticated = _authenticated.load();
    stats.maxQueueSize = _config.maxQueueSize;

    {
        std::lock_guard<std::mutex> lock(_eventQueueMutex);
        stats.queueSize = static_cast<uint32_t>(_eventQueue.size());
    }

    auto now = std::chrono::steady_clock::now();
    stats.uptimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - _startTime).count();

    return stats;
}

void RealtimeBridgeSocket::ResetStats()
{
    _eventsSent = 0;
    _eventsDropped = 0;
    _commandsReceived = 0;
    _commandsProcessed = 0;
    _bytesOut = 0;
    _bytesIn = 0;
}

void RealtimeBridgeSocket::DumpState()
{
    BridgeStats stats = GetStats();

    LOG_INFO("module.realtime-bridge", "=== ECRealtimeBridge State Dump ===");
    LOG_INFO("module.realtime-bridge", "Connected: {}, Authenticated: {}",
        stats.connected ? "yes" : "no", stats.authenticated ? "yes" : "no");
    LOG_INFO("module.realtime-bridge", "Host: {}:{}", _config.host, _config.port);
    LOG_INFO("module.realtime-bridge", "Events Sent: {}, Dropped: {}",
        stats.eventsSent, stats.eventsDropped);
    LOG_INFO("module.realtime-bridge", "Commands Received: {}, Processed: {}",
        stats.commandsReceived, stats.commandsProcessed);
    LOG_INFO("module.realtime-bridge", "Bytes Out: {}, In: {}",
        stats.bytesOut, stats.bytesIn);
    LOG_INFO("module.realtime-bridge", "Queue Size: {}/{}", stats.queueSize, stats.maxQueueSize);
    LOG_INFO("module.realtime-bridge", "Reconnects: {}", stats.reconnectCount);
    LOG_INFO("module.realtime-bridge", "Uptime: {}s", stats.uptimeSeconds);

    // List registered handlers
    {
        std::lock_guard<std::mutex> lock(_handlersMutex);
        LOG_INFO("module.realtime-bridge", "Registered Handlers: {}", _handlers.size());
        for (const auto& [ns, handler] : _handlers)
        {
            LOG_INFO("module.realtime-bridge", "  - {}", NamespaceToString(ns));
        }
    }
}

} // namespace ECRealtimeBridge

/*
 * EC Event Scheduler - Implementation
 */

#include "ECEventScheduler.h"
#include "Log.h"
#include "Timer.h"

#include <memory>

namespace ECShared
{
    // =========================================================================
    // GLOBAL INSTANCE
    // =========================================================================

    static std::unique_ptr<EventScheduler> s_globalScheduler;

    EventScheduler* GetGlobalScheduler()
    {
        if (!s_globalScheduler)
        {
            s_globalScheduler = std::make_unique<EventScheduler>();
        }
        return s_globalScheduler.get();
    }

    // =========================================================================
    // EVENT SCHEDULER IMPLEMENTATION
    // =========================================================================

    EventScheduler::EventScheduler()
        : m_currentTime(getMSTime())
    {
    }

    uint32 EventScheduler::GenerateEventId()
    {
        return m_nextEventId++;
    }

    // =========================================================================
    // SCHEDULING
    // =========================================================================

    uint32 EventScheduler::Schedule(uint32 delayMs, ScheduledEventType type,
                                     std::function<void()> callback)
    {
        return Schedule(delayMs, type, EventPriority::NORMAL, EventGroup::NONE, callback);
    }

    uint32 EventScheduler::Schedule(uint32 delayMs, ScheduledEventType type,
                                     EventPriority priority, EventGroup group,
                                     std::function<void()> callback)
    {
        ScheduledEvent event;
        event.eventId = GenerateEventId();
        event.type = type;
        event.priority = priority;
        event.group = group;
        event.executeTime = m_currentTime + delayMs;
        event.repeatInterval = 0;
        event.repeatCount = 0;
        event.currentRepeat = 0;
        event.callback = std::move(callback);
        event.debugName = EventTypeToString(type);

        AddEvent(std::move(event));

        return event.eventId;
    }

    uint32 EventScheduler::ScheduleRepeating(uint32 intervalMs, ScheduledEventType type,
                                              std::function<void()> callback, uint32 maxRepeats)
    {
        return ScheduleRepeating(intervalMs, intervalMs, type, EventPriority::NORMAL,
                                  EventGroup::NONE, callback, maxRepeats);
    }

    uint32 EventScheduler::ScheduleRepeating(uint32 initialDelayMs, uint32 intervalMs,
                                              ScheduledEventType type, EventPriority priority,
                                              EventGroup group, std::function<void()> callback,
                                              uint32 maxRepeats)
    {
        ScheduledEvent event;
        event.eventId = GenerateEventId();
        event.type = type;
        event.priority = priority;
        event.group = group;
        event.executeTime = m_currentTime + initialDelayMs;
        event.repeatInterval = intervalMs;
        event.repeatCount = maxRepeats;
        event.currentRepeat = 0;
        event.callback = std::move(callback);
        event.debugName = EventTypeToString(type);

        AddEvent(std::move(event));

        return event.eventId;
    }

    uint32 EventScheduler::ScheduleWithContext(uint32 delayMs, ScheduledEventType type,
                                                uint32 contextId, std::function<void()> callback)
    {
        ScheduledEvent event;
        event.eventId = GenerateEventId();
        event.type = type;
        event.priority = EventPriority::NORMAL;
        event.group = EventGroup::ZONE_SPECIFIC;
        event.executeTime = m_currentTime + delayMs;
        event.repeatInterval = 0;
        event.repeatCount = 0;
        event.currentRepeat = 0;
        event.callback = std::move(callback);
        event.contextId = contextId;
        event.debugName = EventTypeToString(type);

        AddEvent(std::move(event));

        return event.eventId;
    }

    void EventScheduler::AddEvent(ScheduledEvent&& event)
    {
        uint32 eventId = event.eventId;
        ScheduledEventType type = event.type;
        EventGroup group = event.group;
        uint32 contextId = event.contextId;

        // Add to queue
        m_eventQueue.push(std::move(event));

        // Track in lookup maps
        // Note: We don't store pointers since priority_queue doesn't guarantee stability
        // Instead, we track IDs and check cancelled flag during execution

        if (type != ScheduledEventType::NONE)
        {
            m_eventsByType[type].insert(eventId);
        }

        if (group != EventGroup::NONE)
        {
            m_eventsByGroup[group].insert(eventId);
        }

        if (contextId != 0)
        {
            m_eventsByContext[contextId].insert(eventId);
        }

        // Update stats
        m_stats.totalScheduled++;
        m_stats.currentPending++;
        if (m_stats.currentPending > m_stats.peakPending)
        {
            m_stats.peakPending = m_stats.currentPending;
        }

        if (m_debugLogging)
        {
            LOG_DEBUG("module", "ECScheduler: Scheduled event %u [%s] for time %llu",
                eventId, EventTypeToString(type), event.executeTime);
        }
    }

    // =========================================================================
    // CANCELLATION
    // =========================================================================

    bool EventScheduler::Cancel(uint32 eventId)
    {
        // Mark as cancelled in tracking maps
        bool found = false;

        for (auto& [type, ids] : m_eventsByType)
        {
            if (ids.erase(eventId) > 0)
            {
                found = true;
                break;
            }
        }

        for (auto& [group, ids] : m_eventsByGroup)
        {
            ids.erase(eventId);
        }

        for (auto& [ctx, ids] : m_eventsByContext)
        {
            ids.erase(eventId);
        }

        if (found)
        {
            m_cancelledCount++;
            m_stats.totalCancelled++;
            m_stats.currentPending--;
            m_needsRebuild = (m_cancelledCount >= REBUILD_THRESHOLD);

            if (m_debugLogging)
            {
                LOG_DEBUG("module", "ECScheduler: Cancelled event %u", eventId);
            }
        }

        return found;
    }

    uint32 EventScheduler::CancelByType(ScheduledEventType type)
    {
        auto it = m_eventsByType.find(type);
        if (it == m_eventsByType.end())
            return 0;

        uint32 count = static_cast<uint32>(it->second.size());

        for (uint32 eventId : it->second)
        {
            // Remove from other tracking maps
            for (auto& [group, ids] : m_eventsByGroup)
            {
                ids.erase(eventId);
            }
            for (auto& [ctx, ids] : m_eventsByContext)
            {
                ids.erase(eventId);
            }
        }

        it->second.clear();
        m_cancelledCount += count;
        m_stats.totalCancelled += count;
        m_stats.currentPending -= count;
        m_needsRebuild = (m_cancelledCount >= REBUILD_THRESHOLD);

        if (m_debugLogging)
        {
            LOG_DEBUG("module", "ECScheduler: Cancelled %u events of type %s",
                count, EventTypeToString(type));
        }

        return count;
    }

    uint32 EventScheduler::CancelByGroup(EventGroup group)
    {
        auto it = m_eventsByGroup.find(group);
        if (it == m_eventsByGroup.end())
            return 0;

        uint32 count = static_cast<uint32>(it->second.size());

        for (uint32 eventId : it->second)
        {
            // Remove from other tracking maps
            for (auto& [type, ids] : m_eventsByType)
            {
                ids.erase(eventId);
            }
            for (auto& [ctx, ids] : m_eventsByContext)
            {
                ids.erase(eventId);
            }
        }

        it->second.clear();
        m_cancelledCount += count;
        m_stats.totalCancelled += count;
        m_stats.currentPending -= count;
        m_needsRebuild = (m_cancelledCount >= REBUILD_THRESHOLD);

        if (m_debugLogging)
        {
            LOG_DEBUG("module", "ECScheduler: Cancelled %u events in group %s",
                count, EventGroupToString(group));
        }

        return count;
    }

    uint32 EventScheduler::CancelByContext(uint32 contextId)
    {
        auto it = m_eventsByContext.find(contextId);
        if (it == m_eventsByContext.end())
            return 0;

        uint32 count = static_cast<uint32>(it->second.size());

        for (uint32 eventId : it->second)
        {
            // Remove from other tracking maps
            for (auto& [type, ids] : m_eventsByType)
            {
                ids.erase(eventId);
            }
            for (auto& [group, ids] : m_eventsByGroup)
            {
                ids.erase(eventId);
            }
        }

        it->second.clear();
        m_cancelledCount += count;
        m_stats.totalCancelled += count;
        m_stats.currentPending -= count;
        m_needsRebuild = (m_cancelledCount >= REBUILD_THRESHOLD);

        if (m_debugLogging)
        {
            LOG_DEBUG("module", "ECScheduler: Cancelled %u events with context %u",
                count, contextId);
        }

        return count;
    }

    void EventScheduler::CancelAll()
    {
        // Clear all tracking
        uint32 count = m_stats.currentPending;

        m_eventsByType.clear();
        m_eventsByGroup.clear();
        m_eventsByContext.clear();

        // Clear queue by swapping with empty
        std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>,
                            std::greater<ScheduledEvent>> empty;
        std::swap(m_eventQueue, empty);

        m_cancelledCount = 0;
        m_needsRebuild = false;
        m_stats.totalCancelled += count;
        m_stats.currentPending = 0;

        if (m_debugLogging)
        {
            LOG_DEBUG("module", "ECScheduler: Cancelled all %u events", count);
        }
    }

    // =========================================================================
    // QUERY
    // =========================================================================

    bool EventScheduler::IsScheduled(uint32 eventId) const
    {
        for (const auto& [type, ids] : m_eventsByType)
        {
            if (ids.count(eventId) > 0)
                return true;
        }
        return false;
    }

    uint32 EventScheduler::GetTimeUntil(uint32 eventId) const
    {
        // This is O(n) unfortunately since we can't directly access queue elements
        // Consider using a secondary map if this is called frequently
        return 0;  // Simplified - would need queue iteration
    }

    uint32 EventScheduler::GetPendingCount() const
    {
        return m_stats.currentPending;
    }

    uint32 EventScheduler::GetPendingCountByType(ScheduledEventType type) const
    {
        auto it = m_eventsByType.find(type);
        return (it != m_eventsByType.end()) ? static_cast<uint32>(it->second.size()) : 0;
    }

    uint32 EventScheduler::GetPendingCountByGroup(EventGroup group) const
    {
        auto it = m_eventsByGroup.find(group);
        return (it != m_eventsByGroup.end()) ? static_cast<uint32>(it->second.size()) : 0;
    }

    // =========================================================================
    // EXECUTION
    // =========================================================================

    void EventScheduler::Update(uint32 diffMs)
    {
        m_currentTime = getMSTime();
        m_stats.lastUpdateTime = m_currentTime;
        m_stats.eventsLastUpdate = 0;

        // Rebuild queue if too many cancellations
        RebuildQueueIfNeeded();

        uint32 processedCount = 0;

        while (!m_eventQueue.empty())
        {
            // Check if we've processed enough this update
            if (m_maxEventsPerUpdate > 0 && processedCount >= m_maxEventsPerUpdate)
                break;

            // Peek at top event
            const ScheduledEvent& top = m_eventQueue.top();

            // Check if it's time to execute
            if (top.executeTime > m_currentTime)
                break;

            // Pop and process
            ScheduledEvent event = m_eventQueue.top();
            m_eventQueue.pop();

            // Check if cancelled (by checking tracking maps)
            bool isCancelled = true;
            auto typeIt = m_eventsByType.find(event.type);
            if (typeIt != m_eventsByType.end() && typeIt->second.count(event.eventId) > 0)
            {
                isCancelled = false;
            }

            if (isCancelled)
            {
                continue;  // Skip cancelled events
            }

            // Remove from tracking (before execution in case callback schedules new events)
            if (typeIt != m_eventsByType.end())
            {
                typeIt->second.erase(event.eventId);
            }

            auto groupIt = m_eventsByGroup.find(event.group);
            if (groupIt != m_eventsByGroup.end())
            {
                groupIt->second.erase(event.eventId);
            }

            if (event.contextId != 0)
            {
                auto ctxIt = m_eventsByContext.find(event.contextId);
                if (ctxIt != m_eventsByContext.end())
                {
                    ctxIt->second.erase(event.eventId);
                }
            }

            // Execute callback
            if (event.callback)
            {
                try
                {
                    event.callback();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("module", "ECScheduler: Exception in event %u [%s]: %s",
                        event.eventId, event.debugName.c_str(), e.what());
                }
                catch (...)
                {
                    LOG_ERROR("module", "ECScheduler: Unknown exception in event %u [%s]",
                        event.eventId, event.debugName.c_str());
                }
            }

            processedCount++;
            m_stats.totalExecuted++;
            m_stats.currentPending--;

            if (m_debugLogging)
            {
                LOG_DEBUG("module", "ECScheduler: Executed event %u [%s]",
                    event.eventId, event.debugName.c_str());
            }

            // Handle repeat
            if (event.repeatInterval > 0)
            {
                event.currentRepeat++;

                // Check if we should repeat
                bool shouldRepeat = (event.repeatCount == 0) ||  // Infinite
                                    (event.currentRepeat < event.repeatCount);

                if (shouldRepeat)
                {
                    // Schedule next occurrence
                    event.eventId = GenerateEventId();  // New ID for tracking
                    event.executeTime = m_currentTime + event.repeatInterval;

                    AddEvent(std::move(event));
                    m_stats.totalRepeats++;
                }
            }
        }

        m_stats.eventsLastUpdate = processedCount;
    }

    void EventScheduler::ProcessAll()
    {
        // Force process all remaining events (for shutdown)
        uint32 originalMax = m_maxEventsPerUpdate;
        m_maxEventsPerUpdate = 0;  // Unlimited

        // Set time to far future to process all
        m_currentTime = std::numeric_limits<uint64>::max();

        while (!m_eventQueue.empty())
        {
            ScheduledEvent event = m_eventQueue.top();
            m_eventQueue.pop();

            if (event.callback)
            {
                try
                {
                    event.callback();
                }
                catch (...) { }
            }
        }

        // Clear all tracking
        m_eventsByType.clear();
        m_eventsByGroup.clear();
        m_eventsByContext.clear();
        m_stats.currentPending = 0;

        m_maxEventsPerUpdate = originalMax;
        m_currentTime = getMSTime();
    }

    void EventScheduler::RebuildQueueIfNeeded()
    {
        if (!m_needsRebuild)
            return;

        // Build set of valid event IDs
        std::set<uint32> validIds;
        for (const auto& [type, ids] : m_eventsByType)
        {
            validIds.insert(ids.begin(), ids.end());
        }

        // Rebuild queue with only valid events
        std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>,
                            std::greater<ScheduledEvent>> newQueue;

        // Extract all events and re-add valid ones
        std::vector<ScheduledEvent> events;
        while (!m_eventQueue.empty())
        {
            events.push_back(m_eventQueue.top());
            m_eventQueue.pop();
        }

        for (auto& event : events)
        {
            if (validIds.count(event.eventId) > 0)
            {
                newQueue.push(std::move(event));
            }
        }

        m_eventQueue = std::move(newQueue);
        m_cancelledCount = 0;
        m_needsRebuild = false;

        if (m_debugLogging)
        {
            LOG_DEBUG("module", "ECScheduler: Rebuilt queue, %u events remaining",
                static_cast<uint32>(m_eventQueue.size()));
        }
    }

    void EventScheduler::ResetStats()
    {
        m_stats = Stats();
        m_stats.currentPending = GetPendingCount();
    }

} // namespace ECShared

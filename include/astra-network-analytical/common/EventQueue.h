/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#pragma once

#include "common/EventList.h"
#include "common/Type.h"

namespace NetworkAnalytical {

enum class EventPeekStatus { Ready, Empty, Busy, PastEvent };

struct EventPeekResult {
    EventPeekStatus status;
    /// Nanoseconds; meaningful only when status is Ready, zero otherwise.
    EventTime time;
};

enum class EventStepStatus { Executed, Empty, Busy, PastTime, TimeMismatch };

/**
 * EventQueue manages scheduled EventLists.
 */
class EventQueue {
  public:
    /**
     * Constructor.
     */
    EventQueue() noexcept;

    /**
     * Get current event time of the event queue.
     *
     * @return current event time
     */
    [[nodiscard]] EventTime get_current_time() const noexcept;

    /**
     * Add const to current event time.
     * Used for waiting idle queue more faster.
     * @return current event time + 100
     */
    void add_current_time() noexcept;

    /**
     * Check all registered events are invoked.
     * i.e., check if the event queue is empty.
     *
     * @return true if the event queue is empty, false otherwise
     */
    [[nodiscard]] bool finished() const noexcept;

    /**
     * Proceed the event queue.
     * i.e., first update the current event time to the next registered event
     * time, and then invoke all events registered at the current updated event
     * time.
     */
    void proceed() noexcept;

    /**
     * Inspect the next native callback time without changing the queue or clock.
     * Single-caller safe-point API: dispatch reentry is Busy, not Empty.
     */
    [[nodiscard]] EventPeekResult peek_next_time() const noexcept;

    /**
     * Invoke exactly one native callback at expected_time (nanoseconds).
     * Rejected calls leave the queue and clock unchanged. The remaining FIFO
     * suffix stays pending, including callbacks newly scheduled at this time.
     * New API reentry returns Busy. Calling legacy proceed/add_current_time
     * inside this callback terminates; callback exceptions retain the native
     * noexcept contract. This API does not provide concurrent queue access.
     */
    [[nodiscard]] EventStepStatus step_at(EventTime expected_time) noexcept;

    /**
     * Schedule an event with a given event time.
     *
     * @param event_time time of event
     * @param callback callback function pointer
     * @param callback_arg argument of the callback function
     */
    void schedule_event(EventTime event_time, Callback callback, CallbackArg callback_arg) noexcept;

  private:
    enum class DispatchMode { Idle, LegacyProceed, SingleStep };

    /// Synchronous reentry guard, not a thread synchronization primitive.
    DispatchMode dispatch_mode = DispatchMode::Idle;

    /// current time of the event queue
    EventTime current_time;

    /// list of EventLists
    std::list<EventList> event_queue;
};

}  // namespace NetworkAnalytical

/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "common/EventQueue.h"
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace NetworkAnalytical;

namespace {

struct Action {
    std::function<void()> run;
};

void invoke_action(void* arg) {
    static_cast<Action*>(arg)->run();
}

void schedule(EventQueue& queue, EventTime time, Action& action) {
    queue.schedule_event(time, invoke_action, &action);
}

using Record = std::pair<EventTime, int>;

// This fixture supplies callbacks and observations, not a second queue model.
std::vector<Record> collect_sequence(bool single_step) {
    EventQueue queue;
    std::vector<Record> records;
    Action tail{[&] { records.emplace_back(queue.get_current_time(), 19); }};
    Action future{[&] { records.emplace_back(queue.get_current_time(), 11); }};
    Action second_append{[&] { records.emplace_back(queue.get_current_time(), 17); }};
    Action first_append{[&] {
        records.emplace_back(queue.get_current_time(), 13);
        schedule(queue, 10, second_append);
    }};
    Action first{[&] {
        records.emplace_back(queue.get_current_time(), 3);
        schedule(queue, 10, first_append);
        schedule(queue, 20, future);
    }};
    Action second{[&] { records.emplace_back(queue.get_current_time(), 7); }};
    schedule(queue, 30, tail);
    schedule(queue, 10, first);
    schedule(queue, 10, second);
    while (!queue.finished()) {
        if (single_step) {
            const auto next = queue.peek_next_time();
            EXPECT_EQ(next.status, EventPeekStatus::Ready);
            EXPECT_EQ(queue.step_at(next.time), EventStepStatus::Executed);
        } else {
            queue.proceed();
        }
    }
    EXPECT_EQ(queue.get_current_time(), 30);
    return records;
}

TEST(EventQueueStep, EmptyDoesNotAdvanceAndCanResume) {
    EventQueue queue;
    EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::Empty);
    EXPECT_EQ(queue.peek_next_time().time, 0);
    EXPECT_EQ(queue.step_at(100), EventStepStatus::Empty);
    EXPECT_EQ(queue.get_current_time(), 0);
    int visits = 0;
    Action action{[&] { ++visits; }};
    schedule(queue, 7, action);
    EXPECT_EQ(queue.peek_next_time().time, 7);
    EXPECT_EQ(queue.get_current_time(), 0);
    EXPECT_EQ(queue.step_at(7), EventStepStatus::Executed);
    EXPECT_EQ(visits, 1);
    EXPECT_TRUE(queue.finished());
    EXPECT_EQ(queue.step_at(100), EventStepStatus::Empty);
    EXPECT_EQ(queue.get_current_time(), 7);
    schedule(queue, 7, action);
    EXPECT_EQ(queue.step_at(7), EventStepStatus::Executed);
    EXPECT_EQ(visits, 2);
}

TEST(EventQueueStep, WrongGrantPreservesTimeAndPendingCallback) {
    EventQueue queue;
    int visits = 0;
    Action action{[&] { ++visits; }};
    schedule(queue, 5, action);
    EXPECT_EQ(queue.step_at(4), EventStepStatus::TimeMismatch);
    EXPECT_EQ(queue.step_at(6), EventStepStatus::TimeMismatch);
    EXPECT_EQ(queue.get_current_time(), 0);
    EXPECT_EQ(visits, 0);
    EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::Ready);
    EXPECT_EQ(queue.peek_next_time().time, 5);
    EXPECT_EQ(queue.step_at(5), EventStepStatus::Executed);
    EXPECT_EQ(queue.step_at(4), EventStepStatus::PastTime);
    EXPECT_EQ(queue.get_current_time(), 5);
    EXPECT_EQ(visits, 1);
}

TEST(EventQueueStep, LegacyClockJumpExposesPastHeadWithoutConsumingIt) {
    EventQueue queue;
    int visits = 0;
    Action action{[&] { ++visits; }};
    schedule(queue, 10, action);
    queue.add_current_time();
    EXPECT_EQ(queue.get_current_time(), 1000000);
    EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::PastEvent);
    EXPECT_EQ(queue.peek_next_time().time, 0);
    EXPECT_EQ(queue.step_at(10), EventStepStatus::PastTime);
    EXPECT_EQ(queue.step_at(1000000), EventStepStatus::PastTime);
    EXPECT_EQ(visits, 0);
    EXPECT_FALSE(queue.finished());
    EXPECT_EQ(queue.get_current_time(), 1000000);
}

TEST(EventQueueStep, SameTimeSuffixRemainsFifoAcrossSteps) {
    EventQueue queue;
    std::vector<int> visits;
    Action appended{[&] { visits.push_back(3); }};
    Action first{[&] {
        visits.push_back(1);
        schedule(queue, 10, appended);
    }};
    Action second{[&] { visits.push_back(2); }};
    Action future{[&] { visits.push_back(4); }};
    schedule(queue, 20, future);
    schedule(queue, 10, first);
    schedule(queue, 10, second);
    EXPECT_EQ(queue.step_at(10), EventStepStatus::Executed);
    EXPECT_EQ(visits, (std::vector<int>{1}));
    EXPECT_EQ(queue.peek_next_time().time, 10);
    EXPECT_EQ(queue.step_at(10), EventStepStatus::Executed);
    EXPECT_EQ(visits, (std::vector<int>{1, 2}));
    EXPECT_EQ(queue.step_at(10), EventStepStatus::Executed);
    EXPECT_EQ(visits, (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(queue.peek_next_time().time, 20);
    EXPECT_EQ(queue.get_current_time(), 10);
    EXPECT_EQ(queue.step_at(20), EventStepStatus::Executed);
    EXPECT_EQ(visits, (std::vector<int>{1, 2, 3, 4}));
}

TEST(EventQueueStep, LastCallbackCanAppendAtItsOwnTime) {
    EventQueue queue;
    int visits = 0;
    Action appended{[&] { ++visits; }};
    Action first{[&] {
        ++visits;
        schedule(queue, 0, appended);
    }};
    schedule(queue, 0, first);
    EXPECT_EQ(queue.step_at(0), EventStepStatus::Executed);
    EXPECT_EQ(visits, 1);
    EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::Ready);
    EXPECT_EQ(queue.peek_next_time().time, 0);
    EXPECT_EQ(queue.step_at(0), EventStepStatus::Executed);
    EXPECT_EQ(visits, 2);
    EXPECT_TRUE(queue.finished());
}

TEST(EventQueueStep, MaximumNativeTimeIsNotNarrowedOrIncremented) {
    EventQueue queue;
    const auto maximum = std::numeric_limits<EventTime>::max();
    Action action{[&] { EXPECT_EQ(queue.get_current_time(), maximum); }};
    schedule(queue, maximum, action);
    EXPECT_EQ(queue.peek_next_time().time, maximum);
    EXPECT_EQ(queue.step_at(maximum), EventStepStatus::Executed);
    EXPECT_EQ(queue.get_current_time(), maximum);
    EXPECT_EQ(queue.step_at(maximum), EventStepStatus::Empty);
}

TEST(EventQueueStep, NewApiReentryIsBusyInsideSingleStep) {
    EventQueue queue;
    int visits = 0;
    Action action{[&] {
        ++visits;
        EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::Busy);
        EXPECT_EQ(queue.peek_next_time().time, 0);
        EXPECT_EQ(queue.step_at(10), EventStepStatus::Busy);
        EXPECT_EQ(queue.get_current_time(), 10);
    }};
    schedule(queue, 10, action);
    EXPECT_EQ(queue.step_at(10), EventStepStatus::Executed);
    EXPECT_EQ(visits, 1);
    EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::Empty);
}

TEST(EventQueueStep, NewApiReentryIsBusyInsideOriginalProceed) {
    EventQueue queue;
    int visits = 0;
    Action action{[&] {
        ++visits;
        EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::Busy);
        EXPECT_EQ(queue.step_at(10), EventStepStatus::Busy);
    }};
    schedule(queue, 10, action);
    schedule(queue, 10, action);
    queue.proceed();
    EXPECT_EQ(visits, 2);
    EXPECT_EQ(queue.peek_next_time().status, EventPeekStatus::Empty);
}

TEST(EventQueueStep, LegacyOnlyClockMutationRetainsOriginalBehavior) {
    EventQueue queue;
    Action action{[&] { queue.add_current_time(); }};
    schedule(queue, 10, action);
    queue.proceed();
    EXPECT_EQ(queue.get_current_time(), 1000010);
    EXPECT_TRUE(queue.finished());
}

TEST(EventQueueStep, StepThenProceedDrainsOnlyTheRemainingGroup) {
    EventQueue queue;
    int visits = 0;
    Action action{[&] { ++visits; }};
    schedule(queue, 5, action);
    schedule(queue, 5, action);
    schedule(queue, 5, action);
    schedule(queue, 7, action);
    EXPECT_EQ(queue.step_at(5), EventStepStatus::Executed);
    EXPECT_EQ(visits, 1);
    queue.proceed();
    EXPECT_EQ(visits, 3);
    EXPECT_EQ(queue.get_current_time(), 5);
    EXPECT_EQ(queue.peek_next_time().time, 7);
    EXPECT_EQ(queue.step_at(7), EventStepStatus::Executed);
    EXPECT_EQ(visits, 4);
}

TEST(EventQueueStep, CompleteSequenceMatchesIndependentExpectedOrderAndOriginalProceed) {
    const std::vector<Record> expected{{10, 3}, {10, 7}, {10, 13}, {10, 17}, {20, 11}, {30, 19}};
    EXPECT_EQ(collect_sequence(false), expected);
    EXPECT_EQ(collect_sequence(true), expected);
}

TEST(EventQueueStepDeathTest, LegacyProceedCannotConsumeAnActiveStep) {
    EventQueue queue;
    Action action{[&] { queue.proceed(); }};
    schedule(queue, 0, action);
    EXPECT_DEATH(static_cast<void>(queue.step_at(0)), "");
}

TEST(EventQueueStepDeathTest, LegacyClockMutationCannotAdvanceAnActiveStep) {
    EventQueue queue;
    Action action{[&] { queue.add_current_time(); }};
    schedule(queue, 0, action);
    EXPECT_DEATH(static_cast<void>(queue.step_at(0)), "");
}

TEST(EventQueueStepDeathTest, CallbackExceptionKeepsNativeNoexceptContract) {
    EventQueue queue;
    Action action{[] { throw std::runtime_error("callback failure"); }};
    schedule(queue, 0, action);
    EXPECT_DEATH(static_cast<void>(queue.step_at(0)), "");
}

}  // namespace

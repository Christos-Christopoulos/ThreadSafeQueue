#pragma once

#include <array>
#include <string>
#include<chrono>
#include <atomic>
#include <thread>
#include <optional>
#include <assert.h>

template<typename QueueItemT, size_t bufferSize>
class LockFreeQueue {

    using SleepGranularity = std::chrono::nanoseconds;
public:

    LockFreeQueue() = default;
    ~LockFreeQueue() = default;

    void push(QueueItemT bufferItem) {

        size_t newTail{};
        bool keepTrying{ true };
        SleepGranularity sleepDuration{ 0 };
        size_t pushIndex{};

        _pendingActions.fetch_add(2, std::memory_order_relaxed); // There are 2 pending action, to 
                                                                 // succesfully push and pop the data.

        do {
            while (!_canUpdate.exchange(false, std::memory_order_acquire)) { /*Wait for access and check index positions.*/ }

            newTail = (_tail.load(std::memory_order_relaxed) + 1) % bufferSize; // Calculate the new tail

            if (newTail != _head.load(std::memory_order_relaxed) &&    // When _tail + 1 == _head we can not add.
                !_hasData.at(_tail.load(std::memory_order_relaxed)
                                  ).load(std::memory_order_acquire)) { // Check that there is no data at the tail index

                pushIndex = _tail.load(std::memory_order_relaxed);
                _tail.store(newTail, std::memory_order_relaxed);
                keepTrying = false;
            }

            _canUpdate.store(true, std::memory_order_relaxed); // Allow access to the critical section for other 
                                                               // threads to update the indexes

            if (keepTrying) {// If we keep retrying, try not to overload the CPU
                sleepDuration = backOff(sleepDuration);
            }
        } while (keepTrying); // Do work until tail is updated and we can push the data

        _buffer.at(pushIndex) = bufferItem;

        _pendingData.fetch_add(1, std::memory_order_relaxed); // We added data in the queue.

        _pendingActions.fetch_add(-1, std::memory_order_relaxed); // We succesfully pushed data.

        _hasData.at(pushIndex).store(true, std::memory_order_release);
    }

    bool pop(QueueItemT& popedData) {

        std::optional<size_t> popIndex;
        bool keepTrying{ true };
        SleepGranularity sleepDuration{ 0 };

        do {
            while (!_canUpdate.exchange(false, std::memory_order_acquire)) { /*Wait for access and check index positions.*/ }

            if (_head.load(std::memory_order_relaxed) != 
                _tail.load(std::memory_order_relaxed)) { // When head == tail we can not remove

                if (_hasData.at(_head.load(std::memory_order_relaxed)
                                     ).load(std::memory_order_acquire)) { // Check that the producer thread managed 
                                                                          // to commit data to the _head index.

                    popIndex = _head.load(std::memory_order_relaxed);

                    _head.store((_head.load(std::memory_order_relaxed) + 1) % bufferSize, 
                                std::memory_order_relaxed); // Update the head;

                    keepTrying = false;
                }
            }
            else {
                keepTrying = false;
            }

            _canUpdate.store(true, std::memory_order_relaxed); // Allow access to the critical section for other 
                                                               // threads to update the indexes

            if (keepTrying) { // If we keep retrying, try not to overload the CPU
                sleepDuration = backOff(sleepDuration);
            }
        } while (keepTrying); // Do work until head is updated and there is no more data to pop in the queue

        if (popIndex.has_value()) {

            popedData = _buffer.at(*popIndex);
            _hasData.at(*popIndex).store(false, std::memory_order_release);

            assert(_pendingData.load(std::memory_order_relaxed) > 0); // We should have _pendingData > 0
            _pendingData.fetch_add(-1, std::memory_order_relaxed); // We removed data from the queue.

            assert(_pendingActions.load(std::memory_order_relaxed) > 0); // We should have _pendingActions > 0
            _pendingActions.fetch_add(-1, std::memory_order_relaxed); // We succesfully poped data.

            return true; // We succesfully poped data.
        }

        return false; // There was no new data available.
    }

    bool hasData() { // Check if there is any data in the queue.
        return _pendingData.load(std::memory_order_relaxed) > 0;
    }

    bool hasWork() { // Check if there are any pending actions.
        return _pendingActions.load(std::memory_order_relaxed) > 0;
    }

private:
    SleepGranularity backOff(SleepGranularity sleepDuration) {

        if (sleepDuration < _maxSleepDuration) {
            sleepDuration += sleepDurationStep;
        }

        std::this_thread::sleep_for(sleepDuration);

        return sleepDuration;
    }

    std::array<QueueItemT, bufferSize> _buffer{}; // the ring buffer
    std::array<std::atomic_bool, bufferSize> _hasData{ false }; // the buffer tracking data
    std::atomic<size_t> _head{ 0 }; // consumer index
    std::atomic<size_t> _tail{ 0 }; // producer index
    std::atomic<size_t> _pendingActions{ 0 }; // count pending actions
    std::atomic<size_t> _pendingData{ 0 }; // count pending data in the queue
    std::atomic_bool _canUpdate{ true }; // critical section protection
    SleepGranularity sleepDurationStep{ 1 };
    SleepGranularity _maxSleepDuration{ 100 };
};
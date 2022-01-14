#pragma once

#include <array>
#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <optional>

template<typename QueueItemT, size_t bufferSize>
class LockFreeQueue {

    using SleepGranularity = std::chrono::nanoseconds;
public:

    LockFreeQueue() = default;
    ~LockFreeQueue() = default;

    // Make the queue non copyable.
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    /** Push data into the queue.
     *
     *  The purpose of the "push" function is to push data into the queue.
     *
     *  If there is no space, the thread will return false and will not
     *  wait for space to become available.
     *
     *  However, once a space has been claimed, access to the queue is
     *  released and other threads can use the queue while the data is
     *  copied into the claimed space.
     *
     *  @arg bufferItem - the data to be pushed into the queue.
     */
    bool push(QueueItemT bufferItem) {

        size_t newTail{};
        bool keepTrying{ true };
        SleepGranularity sleepDuration{ sleepDurationStart };
        std::optional<size_t> pushIndex{ std::nullopt };

        _pendingData.fetch_add(2, std::memory_order_relaxed); // We are going to add data in the queue.
                                                              // We intentionally use 2 here in order to 
                                                              // also use the atomic to prevent memory 
                                                              // reordering in what follows.

        do
        {
            if (_canUpdate.exchange(false, std::memory_order_acquire)) { // Gain access and check index positions.

                newTail = (_tail.load(std::memory_order_relaxed) + 1) % bufferSize; // Calculate the new tail

                if (newTail != _head.load(std::memory_order_relaxed)) { // When _tail + 1 == _head we can not add.

                    if (!_isBusy.at(_tail.load(std::memory_order_relaxed)
                                         ).exchange(true, std::memory_order_relaxed)) { // Check that the index is not busy

                        pushIndex = _tail.load(std::memory_order_relaxed);
                        _tail.store(newTail, std::memory_order_relaxed);

                        keepTrying = false;
                    }
                }
                else {
                    keepTrying = false;
                }

                _canUpdate.store(true, std::memory_order_release); // Allow access to the critical section for other 
                                                                   // threads to update the indexes
            }

            if (keepTrying) {
                sleepDuration = backOff(sleepDuration); // If we keep retrying, try not to overload the CPU
            }
        } while (keepTrying); // Do work until tail is updated and we can push the data

        if (pushIndex.has_value()) {

            _buffer.at(*pushIndex) = bufferItem;

            _pendingData.fetch_sub(1, std::memory_order_acq_rel); // We succesfully pushed data. Decrement the counter to 
                                                                  // what it should be and also use the atomic to prevent 
                                                                  // memory reordering.
                                                                  // Use memory_order_acq_rel to prevent read/writes move.

            _isBusy.at(*pushIndex).store(false, std::memory_order_relaxed); // Flag that we are done with the index

            return true; // We succesfully placed the data in the queue.
        }

        _pendingData.fetch_sub(2, std::memory_order_relaxed); // We failed to add data in the queue.

        return false; // We did not have space to put the data into the queue.
    }

    /** Pop data from the queue.
     *
     *  The purpose of the "pop" function is to extract data from the queue.
     *
     *  The assigned thread will claim the space containing the next available
     *  data. If there is no data, the thread will return false and will not
     *  wait for data to become available.
     *
     *  However, once a space has been claimed, access to the queue is
     *  released and other threads can use the queue while the data is
     *  returned back to the caller.
     *
     *  @arg popedData - the location to put the extracted data into.
     *
     *  @return return true if there was data available to return back
     *          to the caller, otherwise return false.
     */
    bool pop(QueueItemT& popedData) {

        bool keepTrying{ true };
        SleepGranularity sleepDuration{ sleepDurationStart };
        std::optional<size_t> popIndex{ std::nullopt };

        do
        {
            if (_canUpdate.exchange(false, std::memory_order_acquire)) { // Gain access and check index positions.

                if (_head.load(std::memory_order_relaxed) !=
                    _tail.load(std::memory_order_relaxed)) { // When head == tail we can not remove

                    if (!_isBusy.at(_head.load(std::memory_order_relaxed)
                                         ).exchange(true, std::memory_order_relaxed)) { // Check that the producer thread managed 
                                                                                        // to commit data to the _head index and the 
                                                                                        // index is now not busy.

                        popIndex = _head.load(std::memory_order_relaxed);

                        _head.store((_head.load(std::memory_order_relaxed) + 1) % bufferSize,
                            std::memory_order_relaxed); // Update the head;

                        keepTrying = false;
                    }
                }
                else {
                    keepTrying = false;
                }

                _canUpdate.store(true, std::memory_order_release); // Allow access to the critical section for other 
                                                                   // threads to update the indexes
            }

            if (keepTrying) {
                sleepDuration = backOff(sleepDuration); // If we keep retrying, try not to overload the CPU
            }
        } while (keepTrying); // Do work until head is updated and there is no more data to pop in the queue

        if (popIndex.has_value()) {

            popedData = _buffer.at(*popIndex);

            _pendingData.fetch_sub(1, std::memory_order_acq_rel); // We removed data from the queue.
                                                                  // Use memory_order_acq_rel to prevent read/writes move.

            _isBusy.at(*popIndex).store(false, std::memory_order_relaxed); // Flag that we are done with the index

            return true; // We succesfully poped data.
        }

        return false; // There was no new data available.
    }

    /** Check if there is data in the queue.
     *
     * @return true if there is data in the queue, false otherwise.
     */
    bool hasData() {
        return _pendingData.load(std::memory_order_acquire) != 0;
    }

private:
    /** Put the thread to sleep.
     *
     *  The purpose of the "backOff" function is to put the thread to sleep.
     *  The sleep duration increases linearly until a threashold is reached.
     *
     *  @arg sleepDuration - the current value of the sleep duration.
     *
     *  @return the updated value of the sleep duration
     */
    SleepGranularity backOff(SleepGranularity sleepDuration) {

        std::this_thread::yield();

        if (sleepDuration < sleepDurationStep) {

            return sleepDuration + sleepDurationStep;
        }

        std::this_thread::sleep_for(sleepDuration);

        return (sleepDuration < _maxSleepDuration)?
                    sleepDuration + sleepDurationStep: // increment the sleep duration if we are below the max threashhold
                    sleepDurationStart;                // otherwise reset the sleep duration to sleepDurationStart
    }

    std::array<QueueItemT, bufferSize> _buffer{}; // the ring buffer
    std::array<std::atomic_bool, bufferSize> _isBusy{ false }; // the buffer if there is work pending on each index
    std::atomic<size_t> _head{ 0 }; // consumer index
    std::atomic<size_t> _tail{ 0 }; // producer index
    std::atomic<long long> _pendingData{ 0 }; // count pending data in the queue
    std::atomic_bool _canUpdate{ true }; // critical section protection

    SleepGranularity sleepDurationStart{ -10 }; // the initial value of the sleepDuration. Adding sleepDurationStep, 
                                                // until it reaches 1, translates to how many times we are going to 
                                                // spin before going to sleep. eg, sleepDurationStart = -10 and 
                                                // sleepDurationStep = 1 => we are going to spin 10 times before 
                                                // going to sleep for some value of sleepDuration.

    SleepGranularity sleepDurationStep{ 1 };    // the value by which we increment sleepDurationStart every time we spin.
    SleepGranularity _maxSleepDuration{ 1 };    // the maximum time the thread can go to sleep for.
};
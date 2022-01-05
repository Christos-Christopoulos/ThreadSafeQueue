#include <LockFreeQueue.h>
#include <iostream>
#include <mutex>
#include <vector>
#include <memory>

std::chrono::nanoseconds backOff(std::chrono::nanoseconds sleepDuration) {

    if (sleepDuration < std::chrono::nanoseconds{100}) {
        sleepDuration += std::chrono::nanoseconds{1};
    }

    std::this_thread::yield();
    std::this_thread::sleep_for(sleepDuration);

    return sleepDuration;
}

class Printer
{
public:
    Printer() = default;
    ~Printer() = default;

    void print(std::string msg) {
        std::unique_lock<std::mutex> locker(_mu);
        std::cout << msg << std::endl;
    }
private:
    std::mutex _mu;
};

struct QueueChecker {
    QueueChecker(std::atomic_bool& ok) : _ok{ ok }
    {}

    ~QueueChecker() {
        if (!_poped.load(std::memory_order_acquire)) {
            std::cout << "Destroyed unpoped!\n";
            _ok.store(false, std::memory_order_relaxed);
        }
    }

    void poped() {
        if (_poped.exchange(true, std::memory_order_acq_rel)) {
            std::cout << "Poped Twice!\n";
            _ok.store(false, std::memory_order_relaxed);
        }
    }

private:
    std::atomic_bool _poped{ false };
    std::atomic_bool& _ok;
};

class DataGenerator
{
public:
    DataGenerator() = default;
    ~DataGenerator() = default;

    std::shared_ptr<QueueChecker> generate(std::atomic_bool& ok) {
        std::unique_lock<std::mutex> locker(_mu);
        return std::make_shared<QueueChecker>(ok);
    }
private:
    std::mutex _mu;
};

template<typename QueueT>
class Producer
{
public:
    Producer(QueueT& queue, std::atomic_bool& ok, std::atomic_bool& run, DataGenerator& dataGenerator)
        : _queue{ queue }, _ok{ ok }, _run{ run }, _dataGenerator{ dataGenerator }
    {}

    ~Producer() = default;

    void run() {
        std::chrono::nanoseconds sleepDuration{ 0 };
        while (_run.load(std::memory_order_relaxed)) {
            auto data = _dataGenerator.generate(_ok);
            while (!_queue.push(data)) { /*Keep trying until we succeed*/
                if(!_run.load(std::memory_order_relaxed)){
                    data->poped();
                    break;
                }
                sleepDuration = backOff(sleepDuration);
            };
            sleepDuration = std::chrono::nanoseconds{ 0 };
        }
    }
private:
    QueueT& _queue;
    std::atomic_bool& _ok;
    std::atomic_bool& _run;
    DataGenerator& _dataGenerator;
};

template<typename QueueT, typename TData>
class Consumer
{
public:
    Consumer(QueueT& queue, std::atomic_bool& run, std::atomic<unsigned long long>& dataCounter)
        : _queue{ queue }, _run{ run }, _dataCounter{ dataCounter }
    {}

    ~Consumer() = default;

    void run() {
        std::chrono::nanoseconds sleepDuration{ 0 };
        while (_run.load(std::memory_order_relaxed)) {
            TData data{};
            if (_queue.pop(data)) {
                data->poped();
                _dataCounter.fetch_add(1, std::memory_order_relaxed);
                sleepDuration = std::chrono::nanoseconds{ 0 };
            }
            else {
                sleepDuration = backOff(sleepDuration);
            }
        }

        // Clear any leftover data in the queue.
        while (_queue.hasData()) {
            TData data{};
            if (_queue.pop(data)) {
                data->poped();
                _dataCounter.fetch_add(1, std::memory_order_relaxed);
                sleepDuration = std::chrono::nanoseconds{ 0 };
            }
            else{
                sleepDuration = backOff(sleepDuration);
            }
        }
    }
private:
    QueueT& _queue;
    std::atomic_bool& _run;
    std::atomic<unsigned long long>& _dataCounter;
};

bool RunLockFreeQueueTest() {
    using DataT = std::shared_ptr<QueueChecker>;
    using QueueT = LockFreeQueue<DataT, 100>;

    QueueT queue{};
    std::atomic_bool ok{ true };
    DataGenerator dataGenerator{};

    std::atomic<unsigned long long> dataCounter{0};

    std::atomic_bool runProducer{ true };
    std::atomic_bool runConsumer{ true };

    auto runRoutine =
        [run = std::ref(runProducer)]()
    {
        std::this_thread::sleep_for(std::chrono::seconds{ 5 });
        run.get().store(false);
    };

    auto producerRoutine =
        [queue = std::ref(queue), ok = std::ref(ok), run = std::ref(runProducer), dataGenerator = std::ref(dataGenerator)]()
    {
        Producer<QueueT> producer(queue, ok, run, dataGenerator);
        producer.run();
    };

    auto consumerRoutine =
        [queue = std::ref(queue), run = std::ref(runConsumer), dataCounter = std::ref(dataCounter)]()
    {
        Consumer<QueueT, DataT> consumer(queue, run, dataCounter);
        consumer.run();
    };

    std::vector<std::thread> producerThreads{};
    std::vector<std::thread> consumerThreads{};

    size_t numberOfProducers{ 8 };
    size_t numberOfConsumers{ 8 };

    for (size_t i = 0; i < numberOfProducers; ++i) {
        producerThreads.emplace_back(producerRoutine);
    }

    for (size_t i = 0; i < numberOfConsumers; ++i) {
        consumerThreads.emplace_back(consumerRoutine);
    }

    std::thread runThread{ runRoutine };
    runThread.join();

    for (size_t i = 0; i < producerThreads.size(); ++i) {
        producerThreads.at(i).join();
    }

    runConsumer = false;

    for (size_t i = 0; i < consumerThreads.size(); ++i) {
        consumerThreads.at(i).join();
    }

    bool okSnap = ok.load(std::memory_order_consume);
    bool hasDataSnap = queue.hasData();

    std::cout << "Count of poped data: " << dataCounter.load(std::memory_order_acquire) << std::endl;

    std::cout << "okSnap: " << okSnap << "\n"
              << "hasDataSnap: " << hasDataSnap << std::endl;

    return okSnap && // Verify we poped all the data in the queue
        !hasDataSnap; // We should have no pending data in the queue.
}

int main() {
    std::cout << "Test Started!" << std::endl;

    for (int i = 0; i < 2*12; ++i){
        if (!RunLockFreeQueueTest()) {

            std::cout << "Test Failed!" << std::endl;
            return 1;
        }
    }

    std::cout << "Test Passed!" << std::endl;
    return 0;
}
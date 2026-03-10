#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kd39::infrastructure::mq::detail {

struct TopicQueue {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> messages;
};

class LocalStreamBus {
public:
    void Publish(const std::string& topic, const std::string& payload) {
        auto queue = QueueFor(topic);
        {
            std::scoped_lock lock(queue->mu);
            queue->messages.push_back(payload);
        }
        queue->cv.notify_all();
    }

    std::vector<std::string> Pop(const std::string& topic, std::size_t max_count) {
        std::vector<std::string> out;
        auto queue = QueueFor(topic);
        std::scoped_lock lock(queue->mu);
        while (!queue->messages.empty() && out.size() < max_count) {
            out.push_back(queue->messages.front());
            queue->messages.pop_front();
        }
        return out;
    }

private:
    std::shared_ptr<TopicQueue> QueueFor(const std::string& topic) {
        std::scoped_lock lock(mu_);
        auto& slot = topics_[topic];
        if (!slot) {
            slot = std::make_shared<TopicQueue>();
        }
        return slot;
    }

    std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<TopicQueue>> topics_;
};

inline LocalStreamBus& GetBus() {
    static LocalStreamBus bus;
    return bus;
}

}  // namespace kd39::infrastructure::mq::detail

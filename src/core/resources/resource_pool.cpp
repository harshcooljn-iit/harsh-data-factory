#include "flowforge/resources/resource_pool.hpp"

#include <algorithm>

namespace flowforge::resources {

ResourcePool::ResourcePool(domain::ResourcePool capacity) : capacity_(capacity) {}

bool ResourcePool::try_reserve(const domain::ResourceRequirements& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool fits = used_cpu_ + req.cpu_cores <= capacity_.total_cpu_cores &&
                      used_memory_mb_ + req.memory_mb <= capacity_.total_memory_mb &&
                      used_gpu_ + req.gpu_count <= capacity_.total_gpu_count;
    if (!fits) {
        return false;
    }
    used_cpu_ += req.cpu_cores;
    used_memory_mb_ += req.memory_mb;
    used_gpu_ += req.gpu_count;
    return true;
}

void ResourcePool::release(const domain::ResourceRequirements& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    used_cpu_ = std::max(0, used_cpu_ - req.cpu_cores);
    used_memory_mb_ = std::max<std::int64_t>(0, used_memory_mb_ - req.memory_mb);
    used_gpu_ = std::max(0, used_gpu_ - req.gpu_count);
}

bool ResourcePool::can_ever_fit(const domain::ResourceRequirements& req) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return domain::fits_within(req, capacity_);
}

domain::ResourcePool ResourcePool::capacity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_;
}

domain::ResourcePool ResourcePool::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return domain::ResourcePool{capacity_.total_cpu_cores - used_cpu_,
                                capacity_.total_memory_mb - used_memory_mb_,
                                capacity_.total_gpu_count - used_gpu_};
}

std::string ResourcePool::describe() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return "cpu " + std::to_string(used_cpu_) + "/" +
           std::to_string(capacity_.total_cpu_cores) + ", mem " +
           std::to_string(used_memory_mb_) + "/" +
           std::to_string(capacity_.total_memory_mb) + "MB, gpu " +
           std::to_string(used_gpu_) + "/" + std::to_string(capacity_.total_gpu_count) +
           " in use";
}

}  // namespace flowforge::resources

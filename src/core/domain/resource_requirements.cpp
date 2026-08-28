#include "flowforge/domain/resource_requirements.hpp"

#include <string>

namespace flowforge::domain {

bool fits_within(const ResourceRequirements& req, const ResourcePool& pool) noexcept {
    return req.cpu_cores <= pool.total_cpu_cores && req.memory_mb <= pool.total_memory_mb &&
           req.gpu_count <= pool.total_gpu_count;
}

std::string describe(const ResourceRequirements& req) {
    std::string out = "cpu=" + std::to_string(req.cpu_cores);
    out += ", mem=" + std::to_string(req.memory_mb) + "MB";
    out += ", gpu=" + std::to_string(req.gpu_count);
    return out;
}

}  // namespace flowforge::domain

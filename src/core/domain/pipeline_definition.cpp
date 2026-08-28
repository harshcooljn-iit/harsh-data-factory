#include "flowforge/domain/pipeline_definition.hpp"

#include <algorithm>

namespace flowforge::domain {

const TaskDefinition* PipelineDefinition::find_task(const TaskId& id) const noexcept {
    const auto it = std::find_if(tasks.begin(), tasks.end(),
                                 [&](const TaskDefinition& t) { return t.id == id; });
    return it == tasks.end() ? nullptr : &*it;
}

bool PipelineDefinition::has_task(const TaskId& id) const noexcept {
    return find_task(id) != nullptr;
}

}  // namespace flowforge::domain

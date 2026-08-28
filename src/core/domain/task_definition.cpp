#include "flowforge/domain/task_definition.hpp"

namespace flowforge::domain {

ResolvedCommand TaskDefinition::resolve_command() const {
    ResolvedCommand cmd;
    cmd.program = program;
    cmd.argv.reserve(arguments.size() + 2);
    cmd.argv.push_back(program);
    if (type == TaskType::kPython && !script.empty()) {
        cmd.argv.push_back(script);
    }
    for (const auto& arg : arguments) {
        cmd.argv.push_back(arg);
    }
    return cmd;
}

}  // namespace flowforge::domain

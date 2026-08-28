#pragma once

#include <string>
#include <utility>
#include <vector>

#include "flowforge/dag/dag.hpp"
#include "flowforge/domain/pipeline_definition.hpp"

namespace flowforge::test {

// Small fluent helper for assembling a PipelineDefinition + its Dag in tests.
class PipelineBuilder {
public:
    explicit PipelineBuilder(std::string name = "test") {
        pipeline_.name = std::move(name);
        pipeline_.base_directory = ".";
    }

    PipelineBuilder& task(const std::string& id,
                          const std::string& program = "prog",
                          int priority = 0) {
        domain::TaskDefinition t;
        t.id = id;
        t.name = id;
        t.type = domain::TaskType::kExecutable;
        t.program = program.empty() ? id : program;
        t.priority = priority;
        t.cache_enabled = false;
        pipeline_.tasks.push_back(std::move(t));
        return *this;
    }

    PipelineBuilder& cpu(const std::string& id, int cores) {
        for (auto& t : pipeline_.tasks) {
            if (t.id == id) {
                t.resources.cpu_cores = cores;
            }
        }
        return *this;
    }

    PipelineBuilder& retries(const std::string& id, int max_retries) {
        for (auto& t : pipeline_.tasks) {
            if (t.id == id) {
                t.retry.max_retries = max_retries;
            }
        }
        return *this;
    }

    PipelineBuilder& edge(const std::string& from, const std::string& to) {
        pipeline_.edges.push_back({from, to});
        return *this;
    }

    domain::PipelineDefinition& pipeline() { return pipeline_; }

    dag::Dag build_dag() {
        std::vector<std::string> nodes;
        for (const auto& t : pipeline_.tasks) {
            nodes.push_back(t.id);
        }
        std::vector<std::pair<std::string, std::string>> edges;
        for (const auto& e : pipeline_.edges) {
            edges.emplace_back(e.from, e.to);
        }
        auto result = dag::Dag::build(std::move(nodes), std::move(edges));
        return std::move(*result.dag);
    }

private:
    domain::PipelineDefinition pipeline_;
};

}  // namespace flowforge::test

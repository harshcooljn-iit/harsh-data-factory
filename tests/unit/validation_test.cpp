#include <gtest/gtest.h>

#include "flowforge/resources/resource_pool.hpp"
#include "flowforge/validation/pipeline_validator.hpp"

namespace {

using namespace flowforge::domain;
using namespace flowforge::validation;

TaskDefinition exec_task(std::string id, std::string program = "/bin/echo") {
    TaskDefinition t;
    t.id = std::move(id);
    t.name = t.id;
    t.type = TaskType::kExecutable;
    t.program = std::move(program);
    return t;
}

PipelineDefinition base_pipeline() {
    PipelineDefinition p;
    p.name = "p";
    p.base_directory = "/tmp";
    return p;
}

ValidationOptions no_exec_check() {
    ValidationOptions o;
    o.check_executables = false;
    return o;
}

TEST(Validation, AcceptsAWellFormedPipeline) {
    auto p = base_pipeline();
    p.tasks = {exec_task("a"), exec_task("b")};
    p.edges = {{"a", "b"}};
    const auto report = validate_pipeline(p, no_exec_check());
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? ""
                                                       : report.issues.front().to_string());
}

TEST(Validation, FlagsDuplicateTaskIds) {
    auto p = base_pipeline();
    p.tasks = {exec_task("dup"), exec_task("dup")};
    const auto report = validate_pipeline(p, no_exec_check());
    EXPECT_FALSE(report.ok());
    EXPECT_EQ(report.errors().front().task_id, "dup");
}

TEST(Validation, FlagsUnknownDependencyEndpoint) {
    auto p = base_pipeline();
    p.tasks = {exec_task("a")};
    p.edges = {{"a", "ghost"}};
    const auto report = validate_pipeline(p, no_exec_check());
    EXPECT_FALSE(report.ok());
}

TEST(Validation, FlagsCycleWithReadableMessage) {
    auto p = base_pipeline();
    p.tasks = {exec_task("a"), exec_task("b")};
    p.edges = {{"a", "b"}, {"b", "a"}};
    const auto report = validate_pipeline(p, no_exec_check());
    ASSERT_FALSE(report.ok());
    EXPECT_NE(report.errors().front().message.find("cycle"), std::string::npos);
}

TEST(Validation, ReportsMissingInterpreterWithTaskAndField) {
    auto p = base_pipeline();
    TaskDefinition py;
    py.id = "train";
    py.name = "train";
    py.type = TaskType::kPython;
    py.program = "/definitely/not/here/python3";
    py.script = "train.py";
    p.tasks = {py};

    ValidationOptions opts;
    opts.check_executables = true;
    const auto report = validate_pipeline(p, opts);
    ASSERT_FALSE(report.ok());
    const auto err = report.errors().front();
    EXPECT_EQ(err.task_id, "train");
    EXPECT_EQ(err.field, "interpreter");
    EXPECT_NE(err.message.find("Python interpreter not found"), std::string::npos);
}

TEST(Validation, ResolvesInterpreterOnPath) {
    auto p = base_pipeline();
    TaskDefinition py;
    py.id = "t";
    py.name = "t";
    py.type = TaskType::kPython;
    py.program = "sh";  // definitely on PATH
    py.script = "";     // separate error, but interpreter resolves fine
    p.tasks = {py};

    const auto report = validate_pipeline(p);
    bool interpreter_error = false;
    for (const auto& i : report.errors()) {
        if (i.field == "interpreter") {
            interpreter_error = true;
        }
    }
    EXPECT_FALSE(interpreter_error);
}

TEST(Validation, RejectsTaskThatCannotFitResourcePool) {
    auto p = base_pipeline();
    auto t = exec_task("big");
    t.resources.cpu_cores = 64;
    p.tasks = {t};

    flowforge::resources::ResourcePool pool(ResourcePool{8, 0, 0});
    ValidationOptions opts = no_exec_check();
    opts.resource_pool = &pool;
    const auto report = validate_pipeline(p, opts);
    ASSERT_FALSE(report.ok());
    EXPECT_NE(report.errors().front().message.find("exceeds machine capacity"),
              std::string::npos);
}

TEST(Validation, WarnsOnDuplicateArtifactName) {
    auto p = base_pipeline();
    auto t = exec_task("a");
    t.outputs = {{"out", "a.txt", false}, {"out", "b.txt", false}};
    p.tasks = {t};
    const auto report = validate_pipeline(p, no_exec_check());
    EXPECT_TRUE(report.ok());  // warning only
    EXPECT_TRUE(report.has_warnings());
}

TEST(Validation, AllowsExecutableProducedByAnUpstreamTask) {
    // A compile step emits bin/tool; a later task runs bin/tool.
    auto p = base_pipeline();
    TaskDefinition build;
    build.id = "build";
    build.name = "build";
    build.type = TaskType::kExecutable;
    build.program = "/bin/echo";  // stand-in for a compiler
    build.outputs = {{"tool", "bin/tool", false}};

    TaskDefinition use;
    use.id = "use";
    use.name = "use";
    use.type = TaskType::kExecutable;
    use.program = "bin/tool";  // does not exist yet at validate time

    p.tasks = {build, use};
    p.edges = {{"build", "use"}};

    ValidationOptions opts;
    opts.check_executables = true;  // would normally flag bin/tool as missing
    const auto report = validate_pipeline(p, opts);
    for (const auto& i : report.errors()) {
        EXPECT_NE(i.field, "executable") << i.to_string();
    }
    EXPECT_TRUE(report.ok());
}

TEST(Validation, StillFlagsMissingExecutableNotProducedByAnAncestor) {
    // 'use' does not depend on 'build', so bin/tool won't exist in time.
    auto p = base_pipeline();
    TaskDefinition build;
    build.id = "build";
    build.name = "build";
    build.type = TaskType::kExecutable;
    build.program = "/bin/echo";
    build.outputs = {{"tool", "bin/tool", false}};

    TaskDefinition use;
    use.id = "use";
    use.name = "use";
    use.type = TaskType::kExecutable;
    use.program = "bin/tool";

    p.tasks = {build, use};  // no edge between them

    ValidationOptions opts;
    opts.check_executables = true;
    const auto report = validate_pipeline(p, opts);
    EXPECT_FALSE(report.ok());
}

TEST(Validation, RejectsZeroCpuRequest) {
    auto p = base_pipeline();
    auto t = exec_task("a");
    t.resources.cpu_cores = 0;
    p.tasks = {t};
    const auto report = validate_pipeline(p, no_exec_check());
    EXPECT_FALSE(report.ok());
}

}  // namespace

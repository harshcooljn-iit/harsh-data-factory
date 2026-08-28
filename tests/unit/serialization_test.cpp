#include <gtest/gtest.h>

#include "flowforge/serialization/pipeline_document.hpp"

namespace {

using namespace flowforge::serialization;
using namespace flowforge::domain;

constexpr const char* kSample = R"JSON({
  "schema_version": 1,
  "name": "ml_pipeline",
  "description": "demo",
  "max_concurrency": 3,
  "defaults": { "python_interpreter": "python3.14", "cache": true },
  "tasks": [
    {
      "id": "prepare", "name": "Prepare", "type": "python",
      "script": "prepare.py", "arguments": ["input.csv", "clean.csv"],
      "outputs": [ { "name": "clean", "path": "clean.csv" } ]
    },
    {
      "id": "train", "type": "python", "script": "train.py",
      "interpreter": "python3",
      "arguments": ["clean.csv", "model.bin"],
      "inputs": ["clean.csv"],
      "retry": { "max_retries": 2, "base_delay_ms": 500 },
      "resources": { "cpu_cores": 2, "memory_mb": 512 },
      "timeout_ms": 60000, "priority": 5,
      "depends_on": ["prepare"]
    }
  ],
  "dependencies": [ { "from": "prepare", "to": "train" } ]
})JSON";

TEST(Serialization, ParsesValidDocument) {
    const auto result = load_pipeline_from_json(kSample, "/tmp/base");
    ASSERT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front().to_string());
    const auto& p = *result.pipeline;
    EXPECT_EQ(p.name, "ml_pipeline");
    EXPECT_EQ(p.max_concurrency, 3);
    EXPECT_EQ(p.base_directory, "/tmp/base");
    ASSERT_EQ(p.tasks.size(), 2u);

    const auto& prep = p.tasks[0];
    EXPECT_EQ(prep.type, TaskType::kPython);
    EXPECT_EQ(prep.program, "python3.14");  // from defaults
    EXPECT_EQ(prep.script, "prepare.py");
    ASSERT_EQ(prep.outputs.size(), 1u);
    EXPECT_EQ(prep.outputs[0].name, "clean");

    const auto& train = p.tasks[1];
    EXPECT_EQ(train.name, "train");  // defaulted from id
    EXPECT_EQ(train.program, "python3");
    EXPECT_EQ(train.retry.max_retries, 2);
    EXPECT_EQ(train.retry.base_delay.count(), 500);
    EXPECT_EQ(train.resources.cpu_cores, 2);
    ASSERT_TRUE(train.timeout.has_value());
    EXPECT_EQ(train.timeout->count(), 60000);
    EXPECT_EQ(train.priority, 5);
    ASSERT_EQ(train.inputs.size(), 1u);
    EXPECT_EQ(train.inputs[0].name, "clean.csv");

    // depends_on + explicit dependency: both edges recorded (dedup happens in DAG).
    EXPECT_GE(p.edges.size(), 1u);
}

TEST(Serialization, ReportsInvalidJson) {
    const auto result = load_pipeline_from_json("{ not json", "/tmp");
    EXPECT_FALSE(result.ok());
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors.front().message.find("JSON"), std::string::npos);
}

TEST(Serialization, ReportsMissingRequiredFieldsWithLocation) {
    const auto result = load_pipeline_from_json(
        R"({"name":"x","tasks":[{"id":"a","type":"python"}]})", "/tmp");
    ASSERT_FALSE(result.ok());
    bool saw_script = false;
    for (const auto& e : result.errors) {
        if (e.location == "tasks[0].script") {
            saw_script = true;
        }
    }
    EXPECT_TRUE(saw_script);
}

TEST(Serialization, ReportsUnknownTaskType) {
    const auto result = load_pipeline_from_json(
        R"({"name":"x","tasks":[{"id":"a","type":"ruby","script":"x"}]})", "/tmp");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().location, "tasks[0].type");
}

TEST(Serialization, RejectsUnsupportedSchemaVersion) {
    const auto result = load_pipeline_from_json(
        R"({"schema_version":999,"name":"x","tasks":[{"id":"a","type":"executable","executable":"/bin/true"}]})",
        "/tmp");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().location, "schema_version");
}

TEST(Serialization, RoundTripsThroughDump) {
    const auto first = load_pipeline_from_json(kSample, "/tmp/base");
    ASSERT_TRUE(first.ok());
    const std::string dumped = dump_pipeline(*first.pipeline);
    const auto second = load_pipeline_from_json(dumped, "/tmp/base");
    ASSERT_TRUE(second.ok()) << dumped;

    const auto& a = *first.pipeline;
    const auto& b = *second.pipeline;
    ASSERT_EQ(a.tasks.size(), b.tasks.size());
    for (std::size_t i = 0; i < a.tasks.size(); ++i) {
        EXPECT_EQ(a.tasks[i].id, b.tasks[i].id);
        EXPECT_EQ(a.tasks[i].program, b.tasks[i].program);
        EXPECT_EQ(a.tasks[i].script, b.tasks[i].script);
        EXPECT_EQ(a.tasks[i].arguments, b.tasks[i].arguments);
        EXPECT_EQ(a.tasks[i].retry.max_retries, b.tasks[i].retry.max_retries);
        EXPECT_EQ(a.tasks[i].resources.cpu_cores, b.tasks[i].resources.cpu_cores);
        EXPECT_EQ(a.tasks[i].priority, b.tasks[i].priority);
    }
}

}  // namespace

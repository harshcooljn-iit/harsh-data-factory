#include "flowforge/artifacts/artifact_manager.hpp"

namespace fs = std::filesystem;

namespace flowforge::artifacts {

namespace {
fs::path resolve_against(const fs::path& base, const std::string& maybe_relative) {
    if (maybe_relative.empty()) {
        return base;
    }
    const fs::path p(maybe_relative);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (base / p).lexically_normal();
}
}  // namespace

ArtifactManager::ArtifactManager(fs::path pipeline_base_dir)
    : base_dir_(std::move(pipeline_base_dir)) {}

fs::path ArtifactManager::resolve_working_dir(const domain::TaskDefinition& task) const {
    return resolve_against(base_dir_, task.working_directory);
}

fs::path ArtifactManager::resolve_artifact_path(const domain::TaskDefinition& task,
                                                const domain::ArtifactDecl& decl) const {
    return resolve_against(resolve_working_dir(task), decl.path);
}

Artifact ArtifactManager::probe(const domain::TaskDefinition& task,
                                const domain::ArtifactDecl& decl,
                                bool force_checksum) const {
    return probe_artifact(decl.name, resolve_artifact_path(task, decl),
                          decl.checksum || force_checksum);
}

std::vector<Artifact> ArtifactManager::probe_inputs(const domain::TaskDefinition& task,
                                                    bool force_checksum) const {
    std::vector<Artifact> out;
    out.reserve(task.inputs.size());
    for (const auto& decl : task.inputs) {
        out.push_back(probe(task, decl, force_checksum));
    }
    return out;
}

std::vector<Artifact> ArtifactManager::probe_outputs(const domain::TaskDefinition& task,
                                                     bool force_checksum) const {
    std::vector<Artifact> out;
    out.reserve(task.outputs.size());
    for (const auto& decl : task.outputs) {
        out.push_back(probe(task, decl, force_checksum));
    }
    return out;
}

std::vector<std::string> ArtifactManager::missing_inputs(
    const domain::TaskDefinition& task) const {
    std::vector<std::string> missing;
    for (const auto& decl : task.inputs) {
        if (!probe_artifact(decl.name, resolve_artifact_path(task, decl), false).exists) {
            missing.push_back(decl.name);
        }
    }
    return missing;
}

std::vector<std::string> ArtifactManager::missing_outputs(
    const domain::TaskDefinition& task) const {
    std::vector<std::string> missing;
    for (const auto& decl : task.outputs) {
        if (!probe_artifact(decl.name, resolve_artifact_path(task, decl), false).exists) {
            missing.push_back(decl.name);
        }
    }
    return missing;
}

}  // namespace flowforge::artifacts

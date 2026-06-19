#pragma once
// PortTypes.hpp -- the single source of truth for the node-editor TYPE SYSTEM.
//
// Two derivations from a port's raw C++ type name (DataType::name), both used everywhere so the editor never
// drifts: the CONNECTION id (ports connect iff their canonical id matches) and the human TYPE LABEL (the grey
// "[Vector]" text shown next to a port). Both are DERIVED from the real type -- never hardcoded per-port.
//
// CONSOLIDATION: the five real-valued vector representations that fragmented the editor (glm::vec3,
// Eigen::Vector3f/3d, Eigen::VectorXf/Xd) all map to ONE id "vector", so e.g. Compose Vector3 (Eigen::Vector3f)
// now connects to Dot Product (glm::vec3); the matching getInput<T> data coercion (Node.hpp) converts between
// them. Genuinely-different types KEEP their own identity (complex vectors, matrices, quaternions, point clouds,
// handles, joint_config, strings, models) -- the type system still enforces real incompatibilities.
#include <string>

namespace krs::ports {

// The CONNECTION id. QtNodes connects an out->in pair only when these match (NodeDelegate::dataType uses this).
inline std::string canonicalTypeId(const std::string& tn) {
    if (tn == "double" || tn == "float" || tn == "int") return "number";   // numbers interchange (getInput coerces)
    if (tn == "bool") return "bool";
    if (tn == "glm::vec3" || tn == "Eigen::Vector3f" || tn == "Eigen::Vector3d"
        || tn == "Eigen::VectorXf" || tn == "Eigen::VectorXd") return "vector";   // unified real vectors
    // a rigid pose: the quat-native RigidTransform and a raw glm::mat4 transform interoperate (getInput coerces).
    if (tn == "RigidTransform" || tn == "krs::RigidTransform" || tn == "glm::mat4") return "transform";
    return tn;   // genuinely-distinct: complex vectors, matrices, quat, point clouds, handles, joint_config, ...
}

// The human-readable TYPE LABEL (grey text next to the port). DERIVED from the real type, never hardcoded.
inline std::string canonicalTypeLabel(const std::string& tn) {
    const std::string id = canonicalTypeId(tn);
    if (id == "number")    return "Scalar";
    if (id == "bool")      return "Boolean";
    if (id == "vector")    return "Vector";
    if (id == "enum")      return "Option";
    if (id == "transform") return "Transform";   // RigidTransform / glm::mat4 (a rigid pose)
    // genuinely-distinct types: a readable label per family.
    if (tn == "Eigen::VectorXcf" || tn == "Eigen::VectorXcd") return "Complex Vector";
    if (tn == "Eigen::MatrixXf"  || tn == "Eigen::MatrixXd")  return "Matrix";
    if (tn == "Eigen::MatrixXcf")                             return "Complex Matrix";
    if (tn == "glm::quat")                                    return "Quaternion";
    if (tn == "std::vector<glm::vec3>")                       return "Point Cloud";
    if (tn == "joint_config")                                return "Joint Config";
    if (tn == "entt::entity")                                return "Entity";
    if (tn == "entt::registry*" || tn == "registry")         return "Registry";
    if (tn == "std::string")                                 return "Text";
    if (tn == "Image")                                       return "Image";
    if (tn == "Plane")                                       return "Plane";
    if (tn == "CollisionData")                               return "Collision";
    if (tn == "Blackboard")                                  return "Blackboard";
    if (tn == "InferenceModel")                              return "Model";
    if (tn == "StateSpaceModel")                             return "State-Space Model";
    if (tn == "LQRResult")                                   return "LQR Result";
    if (tn == "BTStatus")                                    return "BT Status";
    if (tn == "Execution")                                   return "Execution";
    if (tn.rfind("ProfiledData", 0) == 0)                    return "Profiled Data";
    if (tn.empty())                                          return "";   // an UNTYPED port -> caught by the metadata gate
    return tn;   // fallback: the raw type name (still non-empty + derived)
}

} // namespace krs::ports

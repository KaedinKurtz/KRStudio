#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <Eigen/Dense>
#include <librealsense2/rs.hpp>
#include <vector>
#include <numeric>

/**
 * @brief A static library of functions for converting between common
 * robotics and graphics data types.
 */
namespace DataConversions {

    //==============================================================================
    // GLM <-> Components & STD
    //==============================================================================

    inline glm::vec2 components_to_vec(float x, float y) {
        return { x, y };
    }
    inline void vec_to_components(const glm::vec2& v, float& x, float& y) {
        x = v.x; y = v.y;
    }

    inline glm::vec3 components_to_vec(float x, float y, float z) {
        return { x, y, z };
    }
    inline void vec_to_components(const glm::vec3& v, float& x, float& y, float& z) {
        x = v.x; y = v.y; z = v.z;
    }

    inline glm::vec4 components_to_vec(float x, float y, float z, float w) {
        return { x, y, z, w };
    }
    inline void vec_to_components(const glm::vec4& v, float& x, float& y, float& z, float& w) {
        x = v.x; y = v.y; z = v.z; w = v.w;
    }

    inline glm::quat components_to_quat(float w, float x, float y, float z) {
        return { w, x, y, z };
    }
    inline void quat_to_components(const glm::quat& q, float& w, float& x, float& y, float& z) {
        w = q.w; x = q.x; y = q.y; z = q.z;
    }

    template<int N, typename T>
    inline std::vector<T> vec_to_std_vector(const glm::vec<N, T>& v) {
        return std::vector<T>(glm::value_ptr(v), glm::value_ptr(v) + N);
    }

    template<int N, typename T>
    inline glm::vec<N, T> std_vector_to_vec(const std::vector<T>& vec) {
        glm::vec<N, T> v;
        if (vec.size() >= N) {
            memcpy(glm::value_ptr(v), vec.data(), N * sizeof(T));
        }
        return v;
    }

    //==============================================================================
    // Eigen <-> Components & STD
    //==============================================================================

    inline Eigen::Vector3f components_to_eigen_vec(float x, float y, float z) {
        return { x, y, z };
    }
    inline void eigen_vec_to_components(const Eigen::Vector3f& v, float& x, float& y, float& z) {
        x = v.x(); y = v.y(); z = v.z();
    }

    inline Eigen::Quaternionf components_to_eigen_quat(float w, float x, float y, float z) {
        return Eigen::Quaternionf(w, x, y, z);
    }
    inline void eigen_quat_to_components(const Eigen::Quaternionf& q, float& w, float& x, float& y, float& z) {
        w = q.w(); x = q.x(); y = q.y(); z = q.z();
    }

    inline std::vector<float> eigen_vec_to_std_vector(const Eigen::VectorXf& v) {
        return std::vector<float>(v.data(), v.data() + v.size());
    }
    inline Eigen::VectorXf std_vector_to_eigen_vec(const std::vector<float>& vec) {
        return Eigen::Map<const Eigen::VectorXf>(vec.data(), vec.size());
    }

    //==============================================================================
    // GLM <-> Eigen (High-Performance Conversions)
    //==============================================================================

    inline Eigen::Vector3f to_eigen(const glm::vec3& v) {
        return Eigen::Vector3f(v.x, v.y, v.z);
    }
    inline glm::vec3 to_glm(const Eigen::Vector3f& v) {
        return glm::vec3(v.x(), v.y(), v.z());
    }

    inline Eigen::Vector4f to_eigen(const glm::vec4& v) {
        return Eigen::Vector4f(v.x, v.y, v.z, v.w);
    }
    inline glm::vec4 to_glm(const Eigen::Vector4f& v) {
        return glm::vec4(v.x(), v.y(), v.z(), v.w());
    }

    inline Eigen::Quaternionf to_eigen(const glm::quat& q) {
        return Eigen::Quaternionf(q.w, q.x, q.y, q.z);
    }
    inline glm::quat to_glm(const Eigen::Quaternionf& q) {
        return glm::quat(q.w(), q.x(), q.y(), q.z());
    }

    inline Eigen::Matrix4f to_eigen(const glm::mat4& m) {
        return Eigen::Map<const Eigen::Matrix4f>(glm::value_ptr(m));
    }
    inline glm::mat4 to_glm(const Eigen::Matrix4f& m) {
        return glm::make_mat4(m.data());
    }

    //==============================================================================
    // Librealsense <-> GLM / Eigen
    //==============================================================================

    inline glm::vec3 to_glm(const rs2_vector& v) {
        return glm::vec3(v.x, v.y, v.z);
    }
    inline Eigen::Vector3f to_eigen(const rs2_vector& v) {
        return Eigen::Vector3f(v.x, v.y, v.z);
    }

    inline rs2_vector to_realsense(const glm::vec3& v) {
        return { v.x, v.y, v.z };
    }
    inline rs2_vector to_realsense(const Eigen::Vector3f& v) {
        return { v.x(), v.y(), v.z() };
    }

    inline glm::quat to_glm(const rs2_quaternion& q) {
        return glm::quat(q.w, q.x, q.y, q.z);
    }
    inline Eigen::Quaternionf to_eigen(const rs2_quaternion& q) {
        return Eigen::Quaternionf(q.w, q.x, q.y, q.z);
    }

    inline rs2_quaternion to_realsense(const glm::quat& q) {
        return { q.x, q.y, q.z, q.w };
    }
    inline rs2_quaternion to_realsense(const Eigen::Quaternionf& q) {
        return { q.x(), q.y(), q.z(), q.w() };
    }

    inline glm::mat4 pose_frame_to_glm_mat4(const rs2::pose_frame& pose) {
        glm::vec3 translation = to_glm(pose.get_pose_data().translation);
        glm::quat rotation = to_glm(pose.get_pose_data().rotation);
        glm::mat4 rot_mat = glm::mat4_cast(rotation);
        return glm::translate(glm::mat4(1.0f), translation) * rot_mat;
    }

    inline Eigen::Matrix4f pose_frame_to_eigen_mat4(const rs2::pose_frame& pose) {
        Eigen::Vector3f translation = to_eigen(pose.get_pose_data().translation);
        Eigen::Quaternionf rotation = to_eigen(pose.get_pose_data().rotation);
        Eigen::Matrix4f t = Eigen::Matrix4f::Identity();
        t.block<3, 3>(0, 0) = rotation.toRotationMatrix();
        t.block<3, 1>(0, 3) = translation;
        return t;
    }

    /**
     * @brief Extracts all vertex data from a points frame into a std::vector of glm::vec3.
     * @param points_frame The rs2::points frame object that contains the vertex data.
     */
    inline std::vector<glm::vec3> pointcloud_to_glm_vector(const rs2::points& points_frame) {
        std::vector<glm::vec3> points;
        const rs2::vertex* vertices = points_frame.get_vertices();

        if (!vertices) {
            return points;
        }

        // FIX: Calculate the number of points from the total data size and the size of a single vertex.
        // This is a more robust method if get_count() isn't available.
        const size_t point_count = points_frame.get_data_size() / sizeof(rs2::vertex);

        points.reserve(point_count);
        for (size_t i = 0; i < point_count; ++i) {
            // A non-zero depth value is a simple way to filter out invalid points
            if (vertices[i].z) {
                points.emplace_back(vertices[i].x, vertices[i].y, vertices[i].z);
            }
        }
        return points;
    }
    //==============================================================================
    // Utility & Flattening Conversions
    //==============================================================================

    template<int N, typename T>
    inline std::vector<T> flatten_glm_vectors(const std::vector<glm::vec<N, T>>& vectors) {
        std::vector<T> flat;
        flat.reserve(vectors.size() * N);
        for (const auto& v : vectors) {
            flat.insert(flat.end(), glm::value_ptr(v), glm::value_ptr(v) + N);
        }
        return flat;
    }

    template<int N, typename T>
    inline std::vector<glm::vec<N, T>> unflatten_to_glm_vectors(const std::vector<T>& flat) {
        std::vector<glm::vec<N, T>> vectors;
        if (flat.size() % N != 0) return vectors; // Invalid size
        vectors.reserve(flat.size() / N);
        for (size_t i = 0; i < flat.size(); i += N) {
            glm::vec<N, T> v;
            // FIX: glm::make_vec<N> is not a standard function. Use memcpy for a robust conversion.
            memcpy(glm::value_ptr(v), &flat[i], N * sizeof(T));
            vectors.push_back(v);
        }
        return vectors;
    }

} // namespace DataConversions
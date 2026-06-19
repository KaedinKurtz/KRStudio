#pragma once
// RigidTransform -- a first-class QUAT-NATIVE rigid pose for the node editor: a rotation (unit quaternion) +
// a translation. Stored as quat+vec3 (NOT a raw 4x4) so composing long kinematic chains does not accumulate
// the precision loss / shear / scale drift a matrix pipeline would; a 4x4 view (matrix()) is exposed when a
// consumer needs matrix math. Convention: a point in the LOCAL frame maps to the WORLD frame as
//   p_world = rotation * p_local + position    (apply()),
// and composition A * B means "A applied to B's output" == the matrix product M_A * M_B (B is the inner/child).
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace krs {

struct RigidTransform {
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };   // (w,x,y,z) identity
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };

    // apply this transform to a LOCAL-frame point -> WORLD-frame point.
    glm::vec3 apply(const glm::vec3& p) const { return rotation * p + position; }

    // 4x4 homogeneous view (for consumers that need matrix math). No scale -> a pure rigid transform.
    glm::mat4 matrix() const {
        glm::mat4 m = glm::mat4_cast(glm::normalize(rotation));
        m[3] = glm::vec4(position, 1.0f);
        return m;
    }

    // build from a 4x4 (drops any scale/shear -- keeps the rigid R,t part).
    static RigidTransform fromMatrix(const glm::mat4& m) {
        RigidTransform t;
        t.position = glm::vec3(m[3]);
        glm::mat3 r(m);
        // re-orthonormalize the rotation columns before quat extraction (guards against minor scale).
        r[0] = glm::normalize(r[0]); r[1] = glm::normalize(r[1]); r[2] = glm::normalize(r[2]);
        t.rotation = glm::normalize(glm::quat_cast(r));
        return t;
    }

    static RigidTransform identity() { return RigidTransform{}; }

    // composition: (A * B) applies B first, then A == M_A * M_B.
    RigidTransform operator*(const RigidTransform& b) const {
        RigidTransform out;
        out.rotation = glm::normalize(rotation * b.rotation);
        out.position = rotation * b.position + position;
        return out;
    }

    // T^-1 : T * T.inverse() == identity.  R^-1 = conj(R) (unit quat); t' = -R^-1 * t.
    RigidTransform inverse() const {
        RigidTransform out;
        const glm::quat ri = glm::conjugate(glm::normalize(rotation));
        out.rotation = ri;
        out.position = -(ri * position);
        return out;
    }
};

} // namespace krs

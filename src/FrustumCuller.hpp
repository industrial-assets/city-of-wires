#pragma once

#include <glm/glm.hpp>
#include <array>

namespace pcengine {

// Plane in 3D space (ax + by + cz + d = 0)
struct Plane {
    glm::vec3 normal;
    float distance;
    
    // Signed distance from point to plane (positive = in front)
    float distanceToPoint(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

// Axis-aligned bounding box
struct AABB {
    glm::vec3 min;
    glm::vec3 max;
    
    AABB() : min(0.0f), max(0.0f) {}
    AABB(const glm::vec3& center, const glm::vec3& halfExtent) 
        : min(center - halfExtent), max(center + halfExtent) {}
    
    glm::vec3 getCenter() const { return (min + max) * 0.5f; }
    glm::vec3 getExtent() const { return (max - min) * 0.5f; }
};

// View frustum with 6 planes (near, far, left, right, top, bottom)
class Frustum {
public:
    enum PlaneIndex {
        NEAR = 0,
        FAR = 1,
        LEFT = 2,
        RIGHT = 3,
        TOP = 4,
        BOTTOM = 5
    };
    
    std::array<Plane, 6> planes;
    
    // Extract frustum planes from view-projection matrix
    void extractFromMatrix(const glm::mat4& viewProj) {
        // Left plane
        planes[LEFT].normal = glm::vec3(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0]
        );
        planes[LEFT].distance = viewProj[3][3] + viewProj[3][0];
        
        // Right plane
        planes[RIGHT].normal = glm::vec3(
            viewProj[0][3] - viewProj[0][0],
            viewProj[1][3] - viewProj[1][0],
            viewProj[2][3] - viewProj[2][0]
        );
        planes[RIGHT].distance = viewProj[3][3] - viewProj[3][0];
        
        // Top plane
        planes[TOP].normal = glm::vec3(
            viewProj[0][3] - viewProj[0][1],
            viewProj[1][3] - viewProj[1][1],
            viewProj[2][3] - viewProj[2][1]
        );
        planes[TOP].distance = viewProj[3][3] - viewProj[3][1];
        
        // Bottom plane
        planes[BOTTOM].normal = glm::vec3(
            viewProj[0][3] + viewProj[0][1],
            viewProj[1][3] + viewProj[1][1],
            viewProj[2][3] + viewProj[2][1]
        );
        planes[BOTTOM].distance = viewProj[3][3] + viewProj[3][1];
        
        // Near plane
        planes[NEAR].normal = glm::vec3(
            viewProj[0][3] + viewProj[0][2],
            viewProj[1][3] + viewProj[1][2],
            viewProj[2][3] + viewProj[2][2]
        );
        planes[NEAR].distance = viewProj[3][3] + viewProj[3][2];
        
        // Far plane
        planes[FAR].normal = glm::vec3(
            viewProj[0][3] - viewProj[0][2],
            viewProj[1][3] - viewProj[1][2],
            viewProj[2][3] - viewProj[2][2]
        );
        planes[FAR].distance = viewProj[3][3] - viewProj[3][2];
        
        // Normalize all planes
        for (auto& plane : planes) {
            float length = glm::length(plane.normal);
            plane.normal /= length;
            plane.distance /= length;
        }
    }
    
    // Test if AABB is inside or intersecting the frustum
    bool intersectsAABB(const AABB& aabb) const {
        for (const auto& plane : planes) {
            // Get the positive vertex (furthest in direction of plane normal)
            glm::vec3 positiveVertex = aabb.min;
            if (plane.normal.x >= 0) positiveVertex.x = aabb.max.x;
            if (plane.normal.y >= 0) positiveVertex.y = aabb.max.y;
            if (plane.normal.z >= 0) positiveVertex.z = aabb.max.z;
            
            // If positive vertex is behind plane, AABB is completely outside
            if (plane.distanceToPoint(positiveVertex) < 0) {
                return false;
            }
        }
        return true;
    }
    
    // Test if sphere is inside or intersecting the frustum
    bool intersectsSphere(const glm::vec3& center, float radius) const {
        for (const auto& plane : planes) {
            if (plane.distanceToPoint(center) < -radius) {
                return false;
            }
        }
        return true;
    }
    
    // Test if point is inside the frustum
    bool containsPoint(const glm::vec3& point) const {
        for (const auto& plane : planes) {
            if (plane.distanceToPoint(point) < 0) {
                return false;
            }
        }
        return true;
    }
};

} // namespace pcengine


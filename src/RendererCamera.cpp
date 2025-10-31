#include "Renderer.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pcengine {

void Renderer::processKeyboard(int key, int action) {
    // § (grave accent / backtick) toggles debug overlay (metrics only)
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
        debugOverlayVisible_ = !debugOverlayVisible_;
        return;
    }
    
    // V toggles shadow volumes
    if (key == GLFW_KEY_V && action == GLFW_PRESS) {
        shadowVolumesEnabled_ = !shadowVolumesEnabled_;
        printf("Shadow volumes: %s\n", shadowVolumesEnabled_ ? "enabled" : "disabled");
        return;
    }
    
    // P toggles debug visualization mode (wireframe, chunk boxes, bypass effects)
    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        debugVisualizationMode_ = !debugVisualizationMode_;
        return;
    }
    
    // L toggles light volume debug markers
    if (key == GLFW_KEY_L && action == GLFW_PRESS) {
        debugShowLightMarkers_ = !debugShowLightMarkers_;
        printf("Light markers: %s\n", debugShowLightMarkers_ ? "ON" : "OFF");
        if (debugShowLightMarkers_) {
            updateDebugLightMarkers(); // Update once when enabled
        }
        return;
    }
    
    // Volumetric lighting controls
    // - and = for volumetric brightness
    if (key == GLFW_KEY_MINUS && action == GLFW_PRESS) {
        volumetricScatteringMultiplier_ = std::max(0.5f, volumetricScatteringMultiplier_ * 0.8f);
        printf("Volumetric brightness: %.2f\n", volumetricScatteringMultiplier_);
        return;
    }
    if (key == GLFW_KEY_EQUAL && action == GLFW_PRESS) {
        volumetricScatteringMultiplier_ = std::min(50.0f, volumetricScatteringMultiplier_ * 1.25f);
        printf("Volumetric brightness: %.2f\n", volumetricScatteringMultiplier_);
        return;
    }
    
    // [ and ] for light intensity
    if (key == GLFW_KEY_LEFT_BRACKET && action == GLFW_PRESS) {
        volumetricLightIntensityScale_ = std::max(0.1f, volumetricLightIntensityScale_ * 0.8f);
        printf("Light intensity scale: %.2f\n", volumetricLightIntensityScale_);
        return;
    }
    if (key == GLFW_KEY_RIGHT_BRACKET && action == GLFW_PRESS) {
        volumetricLightIntensityScale_ = std::min(10.0f, volumetricLightIntensityScale_ * 1.25f);
        printf("Light intensity scale: %.2f\n", volumetricLightIntensityScale_);
        return;
    }
    
    // 9 and 0 for fog density
    if (key == GLFW_KEY_9 && action == GLFW_PRESS) {
        volumetricFogDensityScale_ = std::max(0.1f, volumetricFogDensityScale_ * 0.8f);
        printf("Fog density scale: %.2f\n", volumetricFogDensityScale_);
        return;
    }
    if (key == GLFW_KEY_0 && action == GLFW_PRESS) {
        volumetricFogDensityScale_ = std::min(10.0f, volumetricFogDensityScale_ * 1.25f);
        printf("Fog density scale: %.2f\n", volumetricFogDensityScale_);
        return;
    }
    
    // 7 and 8 for light radius
    if (key == GLFW_KEY_7 && action == GLFW_PRESS) {
        volumetricLightRadiusScale_ = std::max(0.1f, volumetricLightRadiusScale_ * 0.8f);
        printf("Light radius scale: %.2f\n", volumetricLightRadiusScale_);
        return;
    }
    if (key == GLFW_KEY_8 && action == GLFW_PRESS) {
        volumetricLightRadiusScale_ = std::min(5.0f, volumetricLightRadiusScale_ * 1.25f);
        printf("Light radius scale: %.2f\n", volumetricLightRadiusScale_);
        return;
    }

    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS) {
            keys_[key] = true;
        } else if (action == GLFW_RELEASE) {
            keys_[key] = false;
        }
    }
    
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        toggleShaderReload();
    }
}

void Renderer::processMouseMovement(float xpos, float ypos) {
    if (!mouseCaptured_) return;
    
    if (firstMouse_) {
        lastMouseX_ = xpos;
        lastMouseY_ = ypos;
        firstMouse_ = false;
    }
    
    float xoffset = xpos - lastMouseX_;
    float yoffset = lastMouseY_ - ypos; // Reversed since y-coordinates go from bottom to top
    
    lastMouseX_ = xpos;
    lastMouseY_ = ypos;
    
    xoffset *= mouseSensitivity_;
    yoffset *= mouseSensitivity_;
    
    yaw_ += xoffset;
    pitch_ += yoffset;
    
    // Constrain pitch to prevent camera flipping
    if (pitch_ > 89.0f) pitch_ = 89.0f;
    if (pitch_ < -89.0f) pitch_ = -89.0f;
    
    updateCameraVectors();
}

void Renderer::processMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            mouseCaptured_ = true;
            firstMouse_ = true;
        } else if (action == GLFW_RELEASE) {
            mouseCaptured_ = false;
        }
    }
}

void Renderer::updateCameraVectors() {
    // Calculate the new front vector
    glm::vec3 front;
    front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front.y = sin(glm::radians(pitch_));
    front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    cameraFront_ = glm::normalize(front);
    
    // Re-calculate the right and up vector
    glm::vec3 right = glm::normalize(glm::cross(cameraFront_, glm::vec3(0.0f, 1.0f, 0.0f)));
    cameraUp_ = glm::normalize(glm::cross(right, cameraFront_));
}

void Renderer::processMovement(float deltaSeconds) {
    float velocity = moveSpeed_ * deltaSeconds;
    
    // Forward/Backward movement
    if (keys_[GLFW_KEY_W]) {
        cameraPos_ += cameraFront_ * velocity;
    }
    if (keys_[GLFW_KEY_S]) {
        cameraPos_ -= cameraFront_ * velocity;
    }
    
    // Left/Right movement (strafe)
    glm::vec3 right = glm::normalize(glm::cross(cameraFront_, cameraUp_));
    if (keys_[GLFW_KEY_A]) {
        cameraPos_ -= right * velocity;
    }
    if (keys_[GLFW_KEY_D]) {
        cameraPos_ += right * velocity;
    }
    
    // Up movement
    if (keys_[GLFW_KEY_SPACE]) {
        cameraPos_ += cameraUp_ * velocity;
    }
}

}


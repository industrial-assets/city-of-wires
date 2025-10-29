#include "Renderer.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pcengine {

void Renderer::processKeyboard(int key, int action) {
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
    
    // Up/Down movement
    if (keys_[GLFW_KEY_SPACE]) {
        cameraPos_ += cameraUp_ * velocity;
    }
    if (keys_[GLFW_KEY_LEFT_SHIFT]) {
        cameraPos_ -= cameraUp_ * velocity;
    }
}

}


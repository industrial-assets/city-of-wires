#include "Engine.hpp"
#include "Renderer.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace pcengine {

static void glfwErrorCallback(int code, const char* description) {
    (void)code;
    (void)description;
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;
    auto* engine = static_cast<pcengine::Engine*>(glfwGetWindowUserPointer(window));
    
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_R) {
            // Toggle hot reloading
            engine->toggleShaderReload();
        }
    }
    
    // Forward all key events to renderer for flight controls
    engine->processKeyboard(key, action);
}

static void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* engine = static_cast<pcengine::Engine*>(glfwGetWindowUserPointer(window));
    engine->processMouseMovement(static_cast<float>(xpos), static_cast<float>(ypos));
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;
    auto* engine = static_cast<pcengine::Engine*>(glfwGetWindowUserPointer(window));
    engine->processMouseButton(button, action);
}

Engine::Engine() = default;
Engine::~Engine() { shutdown(); }

bool Engine::initialize(int width, int height, const std::string &title) {
    if (!glfwInit()) {
        return false;
    }

    glfwSetErrorCallback(glfwErrorCallback);

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }
    
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetCursorPosCallback(window_, mouseCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);

    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->initialize(window_)) {
        return false;
    }

    running_ = true;
    return true;
}

void Engine::poll() {
    glfwPollEvents();
    if (glfwWindowShouldClose(window_)) {
        running_ = false;
    }
}

void Engine::run() {
    double lastTime = glfwGetTime();
    while (running_) {
        double time = glfwGetTime();
        float deltaSeconds = static_cast<float>(time - lastTime);
        lastTime = time;

        poll();
        renderer_->update(deltaSeconds);
        renderer_->drawFrame();
    }
    renderer_->waitIdle();
}

void Engine::shutdown() {
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void Engine::toggleShaderReload() {
    if (renderer_) {
        renderer_->toggleShaderReload();
    }
}

void Engine::processKeyboard(int key, int action) {
    if (renderer_) {
        renderer_->processKeyboard(key, action);
    }
}

void Engine::processMouseMovement(float xpos, float ypos) {
    if (renderer_) {
        renderer_->processMouseMovement(xpos, ypos);
    }
}

void Engine::processMouseButton(int button, int action) {
    if (renderer_) {
        renderer_->processMouseButton(button, action);
    }
}

}



#pragma once

#include <memory>
#include <string>

struct GLFWwindow;

namespace pcengine {

class Renderer;

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool initialize(int width, int height, const std::string &title);
    void run();
    void shutdown();
    void toggleShaderReload();
    
    // Input handling (forwarded to renderer)
    void processKeyboard(int key, int action);
    void processMouseMovement(float xpos, float ypos);
    void processMouseButton(int button, int action);

private:
    void poll();

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    bool running_ = false;
};

}



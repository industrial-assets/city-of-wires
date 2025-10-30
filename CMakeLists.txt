cmake_minimum_required(VERSION 3.20)

project(ProceduralCityEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PC_ENGINE_BUILD_EXAMPLES "Build example apps" ON)
option(PC_ENGINE_USE_VOLK "Use volk for Vulkan loading" OFF)

include(FetchContent)

# Dependencies: GLFW, glm
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)

FetchContent_MakeAvailable(glfw glm)

# Vulkan
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
  if(DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK $ENV{VULKAN_SDK})
    message(STATUS "Using VULKAN_SDK at ${VULKAN_SDK}")
    set(Vulkan_INCLUDE_DIR ${VULKAN_SDK}/Include)
    if(APPLE)
      find_library(Vulkan_LIBRARY NAMES vulkan Vulkan PATHS ${VULKAN_SDK}/lib ${VULKAN_SDK}/macOS/lib ${VULKAN_SDK}/lib/macOS)
    else()
      find_library(Vulkan_LIBRARY NAMES vulkan Vulkan PATHS ${VULKAN_SDK}/Lib ${VULKAN_SDK}/lib)
    endif()
    if(Vulkan_LIBRARY)
      add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
      set_target_properties(Vulkan::Vulkan PROPERTIES
        IMPORTED_LOCATION ${Vulkan_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${Vulkan_INCLUDE_DIR}
      )
      set(Vulkan_FOUND TRUE)
    endif()
  endif()
endif()

if(NOT Vulkan_FOUND)
  message(FATAL_ERROR "Vulkan SDK not found. Install Vulkan SDK (MoltenVK on macOS) and set VULKAN_SDK.")
endif()

if(PC_ENGINE_USE_VOLK)
  FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 1.3.280
  )
  FetchContent_MakeAvailable(volk)
endif()

set(ENGINE_SOURCES
  src/main.cpp
  src/Engine.cpp
  src/Renderer.cpp
  src/RendererVulkanCore.cpp
  src/RendererSwapchain.cpp
  src/RendererPipelines.cpp
  src/RendererCamera.cpp
  src/RendererResources.cpp
  src/RendererGeometry.cpp
  src/RendererShadow.cpp
  src/RendererShadowVolume.cpp
  src/RendererPostProcess.cpp
  src/RendererDebugOverlay.cpp
  src/VulkanUtils.cpp
  src/CityGenerator.cpp
)

set(ENGINE_HEADERS
  src/Engine.hpp
  src/Renderer.hpp
  src/VulkanUtils.hpp
  src/CityGenerator.hpp
)

add_executable(procedural_city ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(procedural_city PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(procedural_city PRIVATE glfw glm::glm Vulkan::Vulkan)

if(PC_ENGINE_USE_VOLK)
  target_link_libraries(procedural_city PRIVATE volk)
  target_compile_definitions(procedural_city PRIVATE PC_ENGINE_USE_VOLK)
  target_include_directories(procedural_city PRIVATE ${volk_SOURCE_DIR})
endif()

if(APPLE)
  # GLFW on macOS needs Cocoa, IOKit, CoreVideo
  find_library(COCOA_LIBRARY Cocoa)
  find_library(IOKIT_LIBRARY IOKit)
  find_library(COREVIDEO_LIBRARY CoreVideo)
  # For PNG loading via CoreGraphics/ImageIO
  find_library(COREGRAPHICS_LIBRARY CoreGraphics)
  find_library(IMAGEIO_LIBRARY ImageIO)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
  target_link_libraries(procedural_city PRIVATE ${COCOA_LIBRARY} ${IOKIT_LIBRARY} ${COREVIDEO_LIBRARY} ${COREGRAPHICS_LIBRARY} ${IMAGEIO_LIBRARY} ${COREFOUNDATION_LIBRARY})
endif()

# Shaders
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
file(GLOB SHADER_SOURCES ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag)
set(SPIRV_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/macOS/bin $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
  message(FATAL_ERROR "glslc not found. Ensure Vulkan SDK is installed and glslc is in PATH.")
endif()

set(COMPILED_SHADERS)
foreach(SHADER ${SHADER_SOURCES})
  get_filename_component(FILE_NAME ${SHADER} NAME)
  set(SPIRV ${SPIRV_OUTPUT_DIR}/${FILE_NAME}.spv)
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSLC} -O ${SHADER} -o ${SPIRV}
    DEPENDS ${SHADER}
    COMMENT "Compiling shader ${FILE_NAME}"
    VERBATIM
  )
  list(APPEND COMPILED_SHADERS ${SPIRV})
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADERS})
add_dependencies(procedural_city compile_shaders)

target_compile_definitions(procedural_city PRIVATE PC_ENGINE_SHADER_DIR="${SPIRV_OUTPUT_DIR}")

# Enable warnings
if(MSVC)
  target_compile_options(procedural_city PRIVATE /W4 /permissive-)
else()
  target_compile_options(procedural_city PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()



# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-src")
  file(MAKE_DIRECTORY "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-src")
endif()
file(MAKE_DIRECTORY
  "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-build"
  "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-subbuild/glm-populate-prefix"
  "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-subbuild/glm-populate-prefix/tmp"
  "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-subbuild/glm-populate-prefix/src/glm-populate-stamp"
  "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-subbuild/glm-populate-prefix/src"
  "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-subbuild/glm-populate-prefix/src/glm-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-subbuild/glm-populate-prefix/src/glm-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/johnbower/Documents/_work/Personal/procedural_city/build/_deps/glm-subbuild/glm-populate-prefix/src/glm-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()

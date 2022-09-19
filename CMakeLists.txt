cmake_minimum_required(VERSION 3.19)
cmake_policy(SET CMP0042 NEW)
set (CMAKE_CXX_STANDARD 14)

project (node-dawn)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_JS_INC})

find_package(Git)

set(depdir ${CMAKE_BINARY_DIR})
function (fetchdep name repo commit)
  if (NOT EXISTS ${depdir}/${name})
    execute_process(COMMAND ${GIT_EXECUTABLE} clone -n ${repo} ${name}
      WORKING_DIRECTORY ${depdir})
  endif()
  execute_process(
    COMMAND ${GIT_EXECUTABLE} checkout ${commit}
    WORKING_DIRECTORY ${depdir}/${name})   
endfunction()

fetchdep(dawn https://dawn.googlesource.com/dawn.git HEAD)
file(READ ${depdir}/dawn/src/dawn/node/binding/GPUDevice.h GPU_DEVICE_HEADER)
string(REPLACE "private" "public" GPU_DEVICE_HEADER "${GPU_DEVICE_HEADER}")
file(WRITE ${depdir}/dawn/src/dawn/node/binding/GPUDevice.h "${GPU_DEVICE_HEADER}")

set(extdir "dawn/third_party")
fetchdep(${extdir}/glfw https://github.com/glfw/glfw.git HEAD)
fetchdep(${extdir}/abseil-cpp https://github.com/abseil/abseil-cpp.git HEAD)
fetchdep(${extdir}/vulkan-deps https://chromium.googlesource.com/vulkan-deps HEAD)
fetchdep(${extdir}/vulkan-deps/vulkan-headers/src https://github.com/KhronosGroup/Vulkan-Headers.git HEAD)
fetchdep(${extdir}/vulkan-deps/spirv-headers/src https://github.com/KhronosGroup/SPIRV-Headers.git HEAD)
fetchdep(${extdir}/vulkan-deps/spirv-tools/src https://github.com/KhronosGroup/SPIRV-Tools.git HEAD)
fetchdep(${extdir}/vulkan-deps/vulkan-tools/src https://github.com/KhronosGroup/Vulkan-Tools.git HEAD)
fetchdep(${extdir}/vulkan-deps/vulkan-validation-layers/src https://github.com/KhronosGroup/Vulkan-ValidationLayers.git HEAD)
fetchdep(${extdir}/node-addon-api https://github.com/nodejs/node-addon-api.git HEAD)
fetchdep(${extdir}/node-api-headers https://github.com/nodejs/node-api-headers.git HEAD)
fetchdep(${extdir}/gpuweb https://github.com/gpuweb/gpuweb.git 3c4734b0)

set(DAWN_BUILD_NODE_BINDINGS ON)
set(DAWN_BUILD_SAMPLES OFF)
set(BUILD_GMOCK OFF)
set(TINT_BUILD_SAMPLES OFF)
set(TINT_BUILD_TESTS OFF)
set(CMAKE_CONFIGURATION_TYPES Release)
set(CMAKE_BUILD_TYPE Release)
add_definitions(-DDAWN_ENABLE_VULKAN_VALIDATION_LAYERS -DDAWN_VK_DATA_DIR="${depdir}/${extdir}/vulkan-deps/vulkan-validation-layers/src")
include_directories(${depdir}/dawn)
add_subdirectory(${depdir}/dawn EXCLUDE_FROM_ALL)
target_link_libraries(dawn_node glfw dawn_glfw)

add_custom_target(inject COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Module.cpp ${depdir}/dawn/src/dawn/node/Module.cpp)
add_custom_target(node-dawn ALL
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/build/dawn/Release/dawn.node ${CMAKE_SOURCE_DIR}/node-dawn.node
  DEPENDS inject dawn_node)

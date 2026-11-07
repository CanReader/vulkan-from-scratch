cmake_minimum_required(VERSION 3.25)
project(VulkanFromScratch CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED CONFIG)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE Vulkan::Vulkan SDL3::SDL3)

# C++26 reflection is still behind a flag in GCC 16.
target_compile_options(app PRIVATE $<$<CXX_COMPILER_ID:GNU>:-freflection>)

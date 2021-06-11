cmake_minimum_required(VERSION 3.13.4)
project(kaleidoscope)

find_package(LLVM REQUIRED CONFIG)

set(CMAKE_CXX_STANDARD 14)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++11 support
# for your compiler.

include_directories(${LLVM_INCLUDE_DIRS} include)

add_definitions(${LLVM_DEFINITIONS})

# Now build our tools
add_executable(kaleidoscope main.cpp)

target_compile_options(kaleidoscope PUBLIC "SHELL:-Xclang -load" "SHELL:-Xclang /usr/lib/EasyJitPass.so" "SHELL:-Xclang /usr/lib/EasyJitPass.so" )

target_include_directories(kaleidoscope PUBLIC ../easy-just-in-time/include)

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs executionengine orcjit object)

# Link against LLVM libraries
target_link_libraries(kaleidoscope ${llvm_libs} EasyJitRuntime)

#target_include_directories(kaleidoscope PUBLIC "${PROJECT_SOURCE_DIR}/include")

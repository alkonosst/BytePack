cmake_minimum_required(VERSION 3.14)
project(BytePack VERSION 0.2.0 LANGUAGES CXX)

# Create an INTERFACE library.
add_library(BytePack INTERFACE)

# Alias with author prefix. Consumers link "alkonosst::BytePack".
add_library(alkonosst::BytePack ALIAS BytePack)

# Specify the include directories for the library.
target_include_directories(BytePack INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Set a C++ standard requirement for the library. This will also propagate to consumers of the library.
target_compile_features(BytePack INTERFACE cxx_std_17)
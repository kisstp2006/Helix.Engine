cmake_minimum_required(VERSION 3.28)
project(Helix.Engine LANGUAGES CXX)

if(NOT COMMAND helix_add_library)
    include("${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.CMake/Helix.cmake")
endif()

helix_require(Core)
helix_require(Platform)
helix_require(Profiler)
helix_require(Jobs)
helix_require(Math)
helix_require(Serialization)
helix_add_library(helix_engine ALIAS Engine PUBLIC_DEPS Helix::Core Helix::Math Helix::Serialization PRIVATE_DEPS Helix::Jobs)

helix_add_test_suite(helix_engine_tests DEPS Helix::Engine Helix::Jobs)
helix_add_benchmark_suite(helix_engine_bench DEPS Helix::Engine Helix::Jobs)

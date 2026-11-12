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
helix_require(Image)
helix_require(Model)
helix_require(ShaderCompiler)
helix_require(RHI)
helix_require(Scripting)
helix_add_library(helix_engine ALIAS Engine PUBLIC_DEPS Helix::Core Helix::Math Helix::Serialization Helix::Image Helix::ShaderCompiler Helix::RHI Helix::Scripting
                  PRIVATE_DEPS Helix::Jobs Helix::Platform Helix::Model)

# The engine's classes for C# and the scripting core: Helix.Bindgen over the tagged headers. The C++ goes into the
# engine; HELIX_ENGINE_BINDINGS_CS is the C# for the C# side of the engine (Helix.ScriptCore).
helix_dependency_dir(bindgen_dir Bindgen)
include("${bindgen_dir}/CMake/HelixBindgen.cmake")
set(engine_source "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# The value types of Helix.Math are the script API's too (their fields bound, Random over the engine's generator).
helix_dependency_dir(engine_math_dir Math)
set(math_source "${engine_math_dir}/Source")
set(HELIX_ENGINE_MATH_HEADERS "${math_source}/Helix/Math/Vector.h" "${math_source}/Helix/Math/Quaternion.h" "${math_source}/Helix/Math/Matrix4x4.h"
    "${math_source}/Helix/Math/Color.h" "${math_source}/Helix/Math/Geometry.h" "${math_source}/Helix/Math/Random.h" CACHE INTERNAL "")
helix_bindgen(helix_engine MODULE HelixEngine NAMESPACE HelixEngine KIND Engine API INCLUDE_ROOTS "${engine_source}" "${math_source}"
              HEADERS ${HELIX_ENGINE_MATH_HEADERS} "${engine_source}/Helix/Engine/Math/MathNative.h" "${engine_source}/Helix/Engine/Object/EntityId.h" "${engine_source}/Helix/Engine/Object/Object.h"
                      "${engine_source}/Helix/Engine/GameObject/GameObject.h" "${engine_source}/Helix/Engine/Transform/Transform.h"
                      "${engine_source}/Helix/Engine/Script/GameBehaviour.h" "${engine_source}/Helix/Engine/Debug/Debug.h"
                      "${engine_source}/Helix/Engine/Time/Time.h" "${engine_source}/Helix/Engine/Application/Application.h"
                      "${engine_source}/Helix/Engine/Screen/Screen.h" "${engine_source}/Helix/Engine/Scene/Scene.h"
                      "${engine_source}/Helix/Engine/Scene/SceneManager.h" "${engine_source}/Helix/Engine/Asset/Resources.h"
                      "${engine_source}/Helix/Engine/Scene/SceneNative.h"
              CS_OUTPUT engine_cs)
set(HELIX_ENGINE_BINDINGS_CS "${engine_cs}" CACHE INTERNAL "")

# HelixEngine: the engine and the libraries it links as one shared library (Windows, Linux, Android), which the player
# and the C++ game modules it loads link, so a module uses the engine of its process.
if(NOT HELIX_PLATFORM STREQUAL "web")
    helix_add_shared_runtime(helix_engine_shared ROOT helix_engine ALIAS EngineShared OUTPUT_NAME HelixEngine)
endif()

# The built in shaders are C#: built with the .NET SDK and embedded with the shader library they call into. The C#
# shader library and the embedding script are found where their repos are (this repo may be a dependency itself).
find_program(HELIX_DOTNET NAMES dotnet HINTS "$ENV{HELIX_DOTNET_ROOT}" "$ENV{DOTNET_ROOT}" REQUIRED)
helix_dependency_dir(shaders_dir Shaders)
helix_dependency_dir(shader_compiler_dir ShaderCompiler)
set(HELIX_ENGINE_SHADERS_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/HelixEngine.BuiltinShaders.csproj" CACHE INTERNAL "")
set(HELIX_SHADERS_DIR "${shaders_dir}" CACHE INTERNAL "")
set(engine_artifacts "${CMAKE_CURRENT_BINARY_DIR}/Dotnet")
set(engine_generated "${CMAKE_CURRENT_BINARY_DIR}/Generated")
set(builtin_dir "${engine_artifacts}/bin/HelixEngine.BuiltinShaders/release")
set(builtin_dll "${builtin_dir}/HelixEngine.BuiltinShaders.dll")
set(builtin_pdb "${builtin_dir}/HelixEngine.BuiltinShaders.pdb")
set(library_dll "${builtin_dir}/HelixEngine.Shaders.dll")
set(embed_file "${shader_compiler_dir}/Tools/EmbedFile.cmake")
set(HELIX_EMBED_FILE "${embed_file}" CACHE INTERNAL "")
file(GLOB_RECURSE builtin_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.csproj"
     "${shaders_dir}/Source/*.cs")
list(FILTER builtin_sources EXCLUDE REGEX "/(bin|obj)/")
add_custom_command(
    OUTPUT "${builtin_dll}" "${builtin_pdb}" "${library_dll}"
    COMMAND "${HELIX_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/HelixEngine.BuiltinShaders.csproj" -c Release
            --artifacts-path "${engine_artifacts}" --nologo -v quiet "-p:HelixShadersDir=${shaders_dir}"
    DEPENDS ${builtin_sources}
    COMMENT "Building the built in C# shaders")
set(builtin_embedded "${engine_generated}/BuiltinShadersAssembly.h" "${engine_generated}/BuiltinShadersSymbols.h"
                     "${engine_generated}/ShaderLibraryAssembly.h")
add_custom_command(
    OUTPUT ${builtin_embedded}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${engine_generated}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${builtin_dll}" "-DOUTPUT=${engine_generated}/BuiltinShadersAssembly.h" -DNAME=kBuiltinShadersAssembly
            -P "${embed_file}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${builtin_pdb}" "-DOUTPUT=${engine_generated}/BuiltinShadersSymbols.h" -DNAME=kBuiltinShadersSymbols
            -P "${embed_file}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${library_dll}" "-DOUTPUT=${engine_generated}/ShaderLibraryAssembly.h" -DNAME=kShaderLibraryAssembly
            -P "${embed_file}"
    DEPENDS "${builtin_dll}" "${builtin_pdb}" "${library_dll}" "${embed_file}"
    COMMENT "Embedding the built in shaders")
target_sources(helix_engine PRIVATE ${builtin_embedded})
target_include_directories(helix_engine PRIVATE "${engine_generated}")

set(engine_binding_headers ${HELIX_ENGINE_MATH_HEADERS} "${engine_source}/Helix/Engine/Object/EntityId.h" "${engine_source}/Helix/Engine/Object/Object.h"
    "${engine_source}/Helix/Engine/GameObject/GameObject.h" "${engine_source}/Helix/Engine/Transform/Transform.h"
    "${engine_source}/Helix/Engine/Script/GameBehaviour.h")
helix_add_test_suite(helix_engine_tests DEPS Helix::Engine Helix::Jobs)
# The tests' C++ scripts: a game module of their own (EngineTests), linked into the test program.
if(TARGET helix_engine_tests)
    helix_bindgen(helix_engine_tests MODULE EngineTests NAMESPACE EngineTests KIND NativeGame
                  HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Scripts/TestScripts.h" REFERENCES ${engine_binding_headers} REFERENCE_NAMESPACE HelixEngine
                  INCLUDE_ROOTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests" "${engine_source}")
endif()
helix_add_benchmark_suite(helix_engine_bench DEPS Helix::Engine Helix::Jobs)
if(TARGET helix_engine_bench)
    target_include_directories(helix_engine_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks")
    helix_bindgen(helix_engine_bench MODULE EngineBench NAMESPACE EngineBench KIND NativeGame
                  HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/Scripts/BenchScripts.h" REFERENCES ${engine_binding_headers} REFERENCE_NAMESPACE HelixEngine
                  INCLUDE_ROOTS "${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks" "${engine_source}")
endif()

# The test shaders: a C# assembly the tests load as an asset, with its references and debug information next to it.
if(TARGET helix_engine_tests OR TARGET helix_engine_bench)
    set(test_shaders_dir "${engine_artifacts}/bin/HelixEngine.TestShaders/release")
    file(GLOB_RECURSE test_shader_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/*.csproj")
    list(FILTER test_shader_sources EXCLUDE REGEX "/(bin|obj)/")
    add_custom_command(
        OUTPUT "${test_shaders_dir}/HelixEngine.TestShaders.dll" "${test_shaders_dir}/HelixEngine.TestShaders.pdb"
        COMMAND "${HELIX_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/HelixEngine.TestShaders.csproj" -c Release
                --artifacts-path "${engine_artifacts}" --nologo -v quiet "-p:HelixShadersDir=${shaders_dir}"
        DEPENDS ${test_shader_sources} ${builtin_sources} "${builtin_dll}"
        COMMENT "Building the C# test shaders")
    # Embedded: the tests write them into a temporary folder, which every platform (the Web's memory file system too) has.
    set(test_embedded "${engine_generated}/TestShadersAssembly.h" "${engine_generated}/TestShadersSymbols.h")
    add_custom_command(
        OUTPUT ${test_embedded}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${engine_generated}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${test_shaders_dir}/HelixEngine.TestShaders.dll" "-DOUTPUT=${engine_generated}/TestShadersAssembly.h"
                -DNAME=kTestShadersAssembly -P "${embed_file}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${test_shaders_dir}/HelixEngine.TestShaders.pdb" "-DOUTPUT=${engine_generated}/TestShadersSymbols.h"
                -DNAME=kTestShadersSymbols -P "${embed_file}"
        DEPENDS "${test_shaders_dir}/HelixEngine.TestShaders.dll" "${embed_file}"
        COMMENT "Embedding the C# test shaders")
    foreach(suite helix_engine_tests helix_engine_bench)
        if(TARGET ${suite})
            target_sources(${suite} PRIVATE ${test_embedded})
            target_compile_definitions(${suite} PRIVATE "HELIX_ENGINE_GOLDEN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Tests/Golden\"")
            target_include_directories(${suite} PRIVATE "${engine_generated}")
        endif()
    endforeach()
endif()

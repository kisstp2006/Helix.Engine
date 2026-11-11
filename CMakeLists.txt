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
helix_add_library(helix_engine ALIAS Engine PUBLIC_DEPS Helix::Core Helix::Math Helix::Serialization Helix::Image Helix::ShaderCompiler
                  PRIVATE_DEPS Helix::Jobs Helix::Platform Helix::Model)

# The built in shaders are C#: built with the .NET SDK and embedded with the shader library they call into.
find_program(HELIX_DOTNET NAMES dotnet HINTS "$ENV{HELIX_DOTNET_ROOT}" "$ENV{DOTNET_ROOT}" REQUIRED)
set(engine_artifacts "${CMAKE_CURRENT_BINARY_DIR}/Dotnet")
set(engine_generated "${CMAKE_CURRENT_BINARY_DIR}/Generated")
set(builtin_dir "${engine_artifacts}/bin/HelixEngine.BuiltinShaders/release")
set(builtin_dll "${builtin_dir}/HelixEngine.BuiltinShaders.dll")
set(builtin_pdb "${builtin_dir}/HelixEngine.BuiltinShaders.pdb")
set(library_dll "${builtin_dir}/HelixEngine.Shaders.dll")
set(embed_file "${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.ShaderCompiler/Tools/EmbedFile.cmake")
file(GLOB_RECURSE builtin_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.csproj"
     "${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.Shaders/Source/*.cs")
list(FILTER builtin_sources EXCLUDE REGEX "/(bin|obj)/")
add_custom_command(
    OUTPUT "${builtin_dll}" "${builtin_pdb}" "${library_dll}"
    COMMAND "${HELIX_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/HelixEngine.BuiltinShaders.csproj" -c Release
            --artifacts-path "${engine_artifacts}" --nologo -v quiet
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

helix_add_test_suite(helix_engine_tests DEPS Helix::Engine Helix::Jobs)
helix_add_benchmark_suite(helix_engine_bench DEPS Helix::Engine Helix::Jobs)

# The test shaders: a C# assembly the tests load as an asset, with its references and debug information next to it.
if(TARGET helix_engine_tests OR TARGET helix_engine_bench)
    set(test_shaders_dir "${engine_artifacts}/bin/HelixEngine.TestShaders/release")
    file(GLOB_RECURSE test_shader_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/*.csproj")
    list(FILTER test_shader_sources EXCLUDE REGEX "/(bin|obj)/")
    add_custom_command(
        OUTPUT "${test_shaders_dir}/HelixEngine.TestShaders.dll" "${test_shaders_dir}/HelixEngine.TestShaders.pdb"
        COMMAND "${HELIX_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/HelixEngine.TestShaders.csproj" -c Release
                --artifacts-path "${engine_artifacts}" --nologo -v quiet
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
            target_include_directories(${suite} PRIVATE "${engine_generated}")
        endif()
    endforeach()
endif()

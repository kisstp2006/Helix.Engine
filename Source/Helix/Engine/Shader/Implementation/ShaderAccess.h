#pragma once

#include <Helix/Engine/Shader/Shader.h>
#include <Helix/ShaderCompiler/Assembly/Assembly.h>
#include <Helix/ShaderCompiler/Assembly/DebugInfo.h>

namespace helix::engine
{
    // An assembly image kept alive with its debug information.
    struct LoadedAssembly
    {
        String name;
        Array<u8> image = Array<u8>();
        Array<u8> pdb = Array<u8>();
        shadercompiler::Assembly assembly;
        shadercompiler::DebugInfo symbols;
        bool hasSymbols = false;
    };

    struct ShaderProgram
    {
        std::unique_ptr<LoadedAssembly> main;
        // The main assembly first, then everything it references (shared libraries live for the whole run).
        Array<const shadercompiler::Assembly*> references = Array<const shadercompiler::Assembly*>();
        Array<const shadercompiler::DebugInfo*> symbols = Array<const shadercompiler::DebugInfo*>();
        Array<shadercompiler::ShaderDescription> shaders = Array<shadercompiler::ShaderDescription>();
        // Compiled variants by key (main thread).
        HashMap<u64, ShaderVariant*> variants;

        ShaderProgram() = default;
        ShaderProgram(const ShaderProgram&) = delete;
        ShaderProgram& operator=(const ShaderProgram&) = delete;
        ~ShaderProgram();
    };

    // Reads a referenced assembly (and its PDB, when there is one) by name; false when there is none.
    using AssemblyReader = bool (*)(void* context, StringView name, Array<u8>& dll, Array<u8>& pdb);

    // Any thread: loads an assembly, finds what it references (the engine's embedded libraries first, then the
    // reader) and reads its shader definitions.
    bool LoadShaderProgram(Array<u8>&& image, Array<u8>&& pdb, AssemblyReader reader, void* context, std::shared_ptr<ShaderProgram>& out,
                           String& error);
}

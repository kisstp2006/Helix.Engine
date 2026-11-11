#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Testing/Bench.h>

#include <TestShadersAssembly.h>

#include <chrono>
#include <cstdio>

using namespace helix;
using namespace helix::engine;
using shadercompiler::KeywordValue;
using shadercompiler::ShaderTarget;

namespace
{
    // The C# test shaders written into a temporary folder and loaded once.
    Shader* Surface()
    {
        static Shader* surface = [] {
            String root = platform::JoinPath(platform::TempDirectory(), "helix-material-bench");
            platform::CreateDirectories(root);
            platform::WriteFile(platform::JoinPath(root, "HelixEngine.TestShaders.dll"), Span<const u8>(kTestShadersAssembly, sizeof(kTestShadersAssembly)));
            static FolderAssetSource source(root);
            AddAssetSource(&source);
            LoadAsset(FindAssetAtPath("HelixEngine.TestShaders.dll")->guid);
            return Shader::Find("Tests/Surface");
        }();
        return surface;
    }
}

// What a renderer asks per draw when nothing changed: the material's data of a pass (keyword values, the variant, the
// written blocks).
HX_BENCH(MaterialPassDataUnchanged)
{
    Shader* s = Surface();
    Material* m = Material::Create(s);
    m->EnableKeyword("Glow");
    u32 pass = u32(s->PassIndex(0));
    m->GetPassData(pass, ShaderTarget::Dxbc);
    state.SetItemsPerIteration(1000);
    while (state.KeepRunning())
        for (u32 i = 0; i < 1000; i++)
            m->GetPassData(pass, ShaderTarget::Dxbc);
}

// A value set every draw: the blocks are written again.
HX_BENCH(MaterialSetFloatAndRewrite)
{
    Shader* s = Surface();
    Material* m = Material::Create(s);
    i32 id = PropertyToID("smoothness");
    u32 pass = u32(s->PassIndex(0));
    m->GetPassData(pass, ShaderTarget::Dxbc);
    state.SetItemsPerIteration(1000);
    f32 v = 0;
    while (state.KeepRunning())
        for (u32 i = 0; i < 1000; i++)
        {
            m->SetFloat(id, v += 0.001f);
            m->GetPassData(pass, ShaderTarget::Dxbc);
        }
}

// A variant compiled for the first time (translation, reflection, both backends; the cache holds nothing yet).
HX_BENCH(ShaderVariantFirstCompile)
{
    Shader* s = Surface();
    u32 pass = u32(s->PassIndex(0));
    state.SetItemsPerIteration(1);
    double total = 0;
    u32 rounds = 0;
    i64 detail = 0;
    while (state.KeepRunning())
    {
        // A fresh cache and fresh keyword values each round, so nothing comes from memory.
        Shader::SetShaderCacheDirectory({});
        s->ClearVariants();
        const KeywordValue keywords[] = {{"Glow", 1}, {"Detail", detail++ % 3}, {"Fog", rounds % 2}};
        auto a = std::chrono::steady_clock::now();
        s->GetVariant(pass, Span<const KeywordValue>(keywords, 3), rounds % 2 ? ShaderTarget::Dxbc : ShaderTarget::Glsl450);
        total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        rounds++;
    }
    std::printf("a variant of Tests/Surface compiles in %.2f ms\n", total / rounds);
}

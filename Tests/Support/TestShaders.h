#pragma once

#include <Support/TestClasses.h>

#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/Platform/IO/FileSystem.h>

#include <TestShadersAssembly.h>
#include <TestShadersSymbols.h>

namespace helix::engine::testing
{
    // The C# test shaders (Tests/Shaders) as an asset: the assembly and its PDB written into a temporary folder, the
    // libraries it references come from the engine.
    struct TestShaderFolder
    {
        String root;
        FolderAssetSource* source = nullptr;
        Guid guid;
        Shader* surface = nullptr;
        Shader* plain = nullptr;

        TestShaderFolder()
        {
            root = platform::JoinPath(platform::TempDirectory(), Format("helix-shaders-{}", Guid::Generate().ToString()));
            platform::CreateDirectories(root);
            platform::WriteFile(platform::JoinPath(root, "HelixEngine.TestShaders.dll"), Span<const u8>(kTestShadersAssembly, sizeof(kTestShadersAssembly)));
            platform::WriteFile(platform::JoinPath(root, "HelixEngine.TestShaders.pdb"), Span<const u8>(kTestShadersSymbols, sizeof(kTestShadersSymbols)));
            source = new FolderAssetSource(root);
            AddAssetSource(source);
            if (const AssetEntry* e = FindAssetAtPath("HelixEngine.TestShaders.dll"))
            {
                guid = e->guid;
                LoadAsset(guid);
            }
            surface = Shader::Find("Tests/Surface");
            plain = Shader::Find("Tests/Plain");
        }

        ~TestShaderFolder()
        {
            ResetAssets();
            Cleanup();
            Shader::Globals().Clear();
            delete source;
            platform::RemoveFile(platform::JoinPath(root, "HelixEngine.TestShaders.dll"));
            platform::RemoveFile(platform::JoinPath(root, "HelixEngine.TestShaders.pdb"));
            platform::RemoveEmptyDirectory(root);
        }
    };
}

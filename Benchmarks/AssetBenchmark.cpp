#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/Threading/Thread.h>
#include <Helix/Testing/Bench.h>

#include <chrono>
#include <cstdio>

using namespace helix;
using namespace helix::engine;

namespace
{
    constexpr u32 kTextures = 64;
    constexpr u32 kTextureSize = 256;
    constexpr u32 kGrid = 128;

    // A folder of source files made once: noisy PNGs (the decoder has work to do) and an OBJ grid.
    struct Files
    {
        String root;
        Array<Guid> textures;
        Guid grid;

        Files()
        {
            root = platform::JoinPath(platform::TempDirectory(), "helix-asset-bench");
            platform::CreateDirectories(root);
            u32 seed = 1;
            for (u32 i = 0; i < kTextures; i++)
            {
                Texture2D* t = Texture2D::Create(i32(kTextureSize), i32(kTextureSize), TextureFormat::RGBA32, false);
                Array<Color32> px;
                px.Resize(kTextureSize * kTextureSize);
                for (Color32& c : px)
                {
                    seed = seed * 1664525u + 1013904223u;
                    c = Color32(u8(seed >> 24), u8(seed >> 16), u8(i * 4), 255);
                }
                t->SetPixels32(Span<const Color32>(px.Data(), px.Size()));
                Array<u8> png;
                t->EncodeToPNG(png);
                DestroyImmediate(t, true);
                platform::WriteFile(platform::JoinPath(root, Format("t{}.png", i)), Span<const u8>(png.Data(), png.Size()));
            }
            String obj;
            for (u32 y = 0; y <= kGrid; y++)
                for (u32 x = 0; x <= kGrid; x++)
                    obj.Append(Format("v {} 0 {}\nvt {} {}\n", x, y, f32(x) / kGrid, f32(y) / kGrid));
            for (u32 y = 0; y < kGrid; y++)
                for (u32 x = 0; x < kGrid; x++)
                {
                    u32 a = y * (kGrid + 1) + x + 1, b = a + 1, c = a + kGrid + 2, d = a + kGrid + 1;
                    obj.Append(Format("f {}/{} {}/{} {}/{} {}/{}\n", a, a, b, b, c, c, d, d));
                }
            platform::WriteFile(platform::JoinPath(root, "grid.obj"), Span<const u8>(reinterpret_cast<const u8*>(obj.Data()), obj.Size()));
            FolderAssetSource source(root);
            AddAssetSource(&source);
            for (u32 i = 0; i < kTextures; i++)
                textures.Push(FindAssetAtPath(Format("t{}.png", i))->guid);
            grid = FindAssetAtPath("grid.obj")->guid;
            ResetAssets();
        }
    };

    Files& Data()
    {
        static Files f;
        return f;
    }

    // Every loaded object goes, so the next round loads from the files again.
    void Forget()
    {
        Array<Object*> all;
        FindObjectsOfTypeAll(ClassOf<Object>(), all);
        Array<EntityId> ids;
        for (Object* o : all)
            if (o->IsPersistent() && !o->Is<GameObject>() && !(o->Class().flags & ClassComponent) && !Has(o->hideFlags, HideFlags::DontUnloadUnusedAsset))
                ids.Push(o->GetEntityId());
        for (Object* o : all)
            if (GameObject* g = o->Is<GameObject>() ? static_cast<GameObject*>(o) : nullptr; g && g->IsPersistent() && !g->transform()->parent())
                ids.Push(g->GetEntityId());
        for (EntityId id : ids)
            if (Object* o = EntityIdToObject(id))
                DestroyImmediate(o, true);
        ResetAssets();
    }
}

HX_BENCH(AssetTextureLoadSync)
{
    Files& f = Data();
    FolderAssetSource source(f.root);
    state.SetItemsPerIteration(kTextures);
    while (state.KeepRunning())
    {
        AddAssetSource(&source);
        for (const Guid& g : f.textures)
            LoadAsset(g);
        Forget();
    }
}

HX_BENCH(AssetTextureLoadAsync)
{
    Files& f = Data();
    FolderAssetSource source(f.root);
    jobs::Initialize();
    state.SetItemsPerIteration(kTextures);
    double longest = 0;
    u32 frames = 0, rounds = 0;
    while (state.KeepRunning())
    {
        AddAssetSource(&source);
        Array<ResourceRequest> requests;
        for (const Guid& g : f.textures)
            requests.Push(LoadAssetAsync(g));
        while (PendingAssetLoads())
        {
            auto a = std::chrono::steady_clock::now();
            ProcessAssetLoads();
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            longest = ms > longest ? ms : longest;
            frames++;
            platform::YieldThread();
        }
        rounds++;
        Forget();
    }
    std::printf("main thread: longest frame %.2f ms, %u frames a round\n", longest, rounds ? frames / rounds : 0);
    jobs::Shutdown();
}

HX_BENCH(AssetModelLoadObj)
{
    Files& f = Data();
    FolderAssetSource source(f.root);
    state.SetItemsPerIteration(u64(kGrid) * kGrid * 2);
    while (state.KeepRunning())
    {
        AddAssetSource(&source);
        LoadAsset(f.grid);
        Forget();
    }
}

HX_BENCH(MeshSetVerticesTrianglesNormals)
{
    constexpr u32 side = 255;
    Array<Vector3> v;
    Array<i32> t;
    for (u32 y = 0; y <= side; y++)
        for (u32 x = 0; x <= side; x++)
            v.Push(Vector3(f32(x), 0, f32(y)));
    for (u32 y = 0; y < side; y++)
        for (u32 x = 0; x < side; x++)
        {
            i32 a = i32(y * (side + 1) + x);
            for (i32 i : {a, a + i32(side) + 1, a + 1, a + 1, a + i32(side) + 1, a + i32(side) + 2})
                t.Push(i);
        }
    state.SetItemsPerIteration(v.Size());
    double times[4] = {};
    u64 rounds = 0;
    while (state.KeepRunning())
    {
        auto a = std::chrono::steady_clock::now();
        Mesh* m = NewObject<Mesh>();
        m->SetVertices(Span<const Vector3>(v.Data(), v.Size()));
        auto b = std::chrono::steady_clock::now();
        m->SetTriangles(Span<const i32>(t.Data(), t.Size()));
        auto c = std::chrono::steady_clock::now();
        m->RecalculateNormals();
        auto d = std::chrono::steady_clock::now();
        DestroyImmediate(m, true);
        auto e = std::chrono::steady_clock::now();
        times[0] += std::chrono::duration<double, std::nano>(b - a).count();
        times[1] += std::chrono::duration<double, std::nano>(c - b).count();
        times[2] += std::chrono::duration<double, std::nano>(d - c).count();
        times[3] += std::chrono::duration<double, std::nano>(e - d).count();
        rounds++;
    }
    double per = double(rounds) * double(v.Size());
    std::printf("per vertex: vertices %.1f ns, triangles %.1f ns, normals %.1f ns, destroy %.1f ns\n", times[0] / per, times[1] / per, times[2] / per, times[3] / per);
}

#include <Helix/Image/Codecs/ImageDecoder.h>
#include <Helix/Image/Texture/Mipmaps.h>

// Where a texture load spends its time: decode, the Texture2D layout, the mips.
HX_BENCH(AssetTextureStagesSplit)
{
    Files& f = Data();
    Array<u8> png;
    platform::ReadFile(platform::JoinPath(f.root, "t0.png"), png);
    state.SetItemsPerIteration(1);
    double decode = 0, prepare = 0, mips = 0;
    u64 rounds = 0;
    while (state.KeepRunning())
    {
        image::TextureData data;
        String error;
        auto a = std::chrono::steady_clock::now();
        image::DecodeImage(Span<const u8>(png.Data(), png.Size()), data, error);
        auto b = std::chrono::steady_clock::now();
        PrepareTextureData(data, error);
        auto c = std::chrono::steady_clock::now();
        image::MipmapSettings s;
        s.srgb = true;
        image::TextureData chain;
        image::GenerateMipmaps(data, s, chain, error);
        auto d = std::chrono::steady_clock::now();
        decode += std::chrono::duration<double, std::milli>(b - a).count();
        prepare += std::chrono::duration<double, std::milli>(c - b).count();
        mips += std::chrono::duration<double, std::milli>(d - c).count();
        rounds++;
    }
    std::printf("256 x 256 texture: decode %.3f ms, layout %.3f ms, mips %.3f ms\n", decode / double(rounds), prepare / double(rounds), mips / double(rounds));
}

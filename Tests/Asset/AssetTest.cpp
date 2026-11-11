#include <Support/TestClasses.h>

#include <Helix/Engine/Asset/AssetLoader.h>
#include <Helix/Engine/Asset/Resources.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Prefab/Prefab.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Testing/Test.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    void RemoveTree(StringView path)
    {
        struct Names
        {
            Array<String> files, dirs;
        } n{Array<String>(), Array<String>()};
        platform::ListDirectory(
            path,
            [](const platform::DirectoryEntry& e, void* user) {
                Names& n = *static_cast<Names*>(user);
                (e.isDirectory ? n.dirs : n.files).Push(String(e.name));
            },
            &n);
        for (const String& f : n.files)
            platform::RemoveFile(platform::JoinPath(path, f));
        for (const String& d : n.dirs)
            RemoveTree(platform::JoinPath(path, d));
        platform::RemoveEmptyDirectory(path);
    }

    // A folder of source files for one test, removed with everything the runtime knows.
    struct Folder
    {
        String root;
        FolderAssetSource* source = nullptr;

        Folder()
        {
            root = platform::JoinPath(platform::TempDirectory(), Format("helix-assets-{}", Guid::Generate().ToString()));
            platform::CreateDirectories(root);
        }
        ~Folder()
        {
            ResetAssets();
            Cleanup();
            delete source;
            RemoveTree(root);
        }
        void Write(StringView path, Span<const u8> bytes)
        {
            String full = platform::JoinPath(root, path);
            platform::CreateDirectories(String(platform::GetDirectoryName(full)));
            platform::WriteFile(full, bytes);
        }
        void Write(StringView path, StringView text) { Write(path, Span<const u8>(reinterpret_cast<const u8*>(text.Data()), text.Size())); }
        void Open()
        {
            source = new FolderAssetSource(root);
            AddAssetSource(source);
        }
    };

    Array<u8> Png(i32 w, i32 h, Color32 c)
    {
        Texture2D* t = Texture2D::Create(w, h, TextureFormat::RGBA32, false);
        Array<Color32> px;
        for (i32 i = 0; i < w * h; i++)
            px.Push(c);
        t->SetPixels32(Span<const Color32>(px.Data(), px.Size()));
        // One pixel apart: the top row is blue, to see the orientation.
        t->SetPixel(0, h - 1, Color(0, 0, 1, 1));
        Array<u8> png;
        t->EncodeToPNG(png);
        DestroyImmediate(t, true);
        return png;
    }

    constexpr StringView kMetaGuid = "0123456789abcdef0123456789abcdef";

    constexpr StringView kObj = "o First\n"
                                "v 0 0 0\nv 0 1 0\nv 1 1 0\nv 1 0 0\n"
                                "vt 0 0\nvt 0 1\nvt 1 1\nvt 1 0\n"
                                "f 1/1 2/2 3/3 4/4\n"
                                "o Second\n"
                                "v 2 0 0\nv 2 1 0\nv 3 0 0\n"
                                "f 5 6 7\n";

    constexpr StringView kGltf = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"name":"Tri","mesh":0,"translation":[0,2,0]}],
"meshes":[{"name":"TriMesh","primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],
"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"normalTexture":{"index":1}}],
"textures":[{"source":0},{"source":1}],"images":[{"uri":"albedo.png"},{"uri":"bump.png"}],
"buffers":[{"uri":"tri.bin","byteLength":68}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";

    Array<u8> TriangleBuffer()
    {
        const f32 data[] = {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0};
        const u16 indices[] = {0, 1, 2, 0};
        Array<u8> b;
        b.Resize(68);
        std::memcpy(b.Data(), data, 60);
        std::memcpy(b.Data() + 60, indices, 8);
        return b;
    }

    // Frames of the loads until the operation is done (up to 10 s on a loaded machine).
    bool RunUntil(AsyncOperation op)
    {
        for (int i = 0; i < 10000 && !op.isDone(); i++)
        {
            ProcessAssetLoads();
            SceneManager::ProcessSceneLoads();
            if (!op.isDone())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return op.isDone();
    }
}

HX_TEST(Asset, CatalogListsSourcesWithGuidsAndResourcePaths)
{
    Folder f;
    Array<u8> png = Png(2, 2, Color32(255, 0, 0, 255));
    f.Write("Art/Resources/Tex/Red.png", Span<const u8>(png.Data(), png.Size()));
    f.Write("Art/Resources/Tex/Red.png.meta", Format("fileFormatVersion: 2\nguid: {}\nTextureImporter:\n  isReadable: 1\n", kMetaGuid));
    f.Write("Models/Quad.obj", kObj);
    f.Write("Notes/readme.txt", StringView("no loader"));
    f.Open();
    Guid metaGuid;
    HX_REQUIRE(Guid::TryParse(kMetaGuid, metaGuid));
    const AssetEntry* red = FindAsset(metaGuid);
    HX_REQUIRE(red != nullptr);
    HX_CHECK(red->path == StringView("Art/Resources/Tex/Red.png") && red->resourcePath == StringView("tex/red"));
    HX_CHECK(FindResource("TEX/Red") == red && FindAssetAtPath("art/resources/tex/red.PNG") == red);
    // Without a .meta the guid comes from the path.
    const AssetEntry* quad = FindAssetAtPath("Models/Quad.obj");
    HX_CHECK(quad && quad->guid == PathGuid("models/quad.OBJ") && quad->resourcePath.Empty());
    HX_CHECK(!FindAssetAtPath("Notes/readme.txt"));
    Array<const AssetEntry*> folder;
    FindResources("tex", folder);
    HX_CHECK(folder.Size() == 1 && folder[0] == red);
    AssetLoader loader;
    HX_CHECK(FindAssetLoader("png", loader) && loader.name == StringView("Texture") && FindAssetLoader("glb", loader) && loader.name == StringView("Model"));
}

HX_TEST(Asset, TexturesLoadWithTheirImportSettings)
{
    Folder f;
    Array<u8> png = Png(8, 4, Color32(255, 0, 0, 255));
    f.Write("Resources/red.png", Span<const u8>(png.Data(), png.Size()));
    f.Write("Resources/red.png.meta",
            Format("guid: {}\nTextureImporter:\n  mipmaps:\n    enableMipMap: 0\n  isReadable: 1\n  textureSettings:\n    filterMode: 0\n    wrapU: 1\n",
                   kMetaGuid));
    f.Write("Resources/plain.png", Span<const u8>(png.Data(), png.Size()));
    f.Open();
    Texture2D* t = Resources::Load<Texture2D>("red");
    HX_REQUIRE(t != nullptr);
    HX_CHECK(t->width() == 8 && t->height() == 4 && t->mipmapCount() == 1 && t->isReadable() && t->IsPersistent() && t->Name() == StringView("red"));
    HX_CHECK(t->filterMode() == FilterMode::Point && t->wrapModeU() == TextureWrapMode::Clamp && t->wrapModeV() == TextureWrapMode::Repeat);
    // Rows bottom first: the file's top row is the texture's last.
    HX_CHECK(t->GetPixel(0, 3) == Color(0, 0, 1, 1) && t->GetPixel(0, 0) == Color(1, 0, 0, 1));
    Guid g;
    i64 fileID = 0;
    i32 type = 0;
    HX_CHECK(GetAssetReference(t, g, fileID, type) && fileID == 2800000 && type == kAssetTypeImported);
    HX_CHECK(Resources::Load("red") == t && LoadAsset(g, 2800000) == t);
    // Defaults without a .meta: a full mip chain, not readable.
    Texture2D* plain = Resources::Load<Texture2D>("plain");
    HX_CHECK(plain && plain->mipmapCount() == 4 && !plain->isReadable() && plain->isDataSRGB());
    HX_CHECK(Resources::Load<Mesh>("red") == nullptr && Resources::Load("missing") == nullptr);
}

HX_TEST(Asset, ModelsBecomeMeshesAndAPrefab)
{
    Folder f;
    f.Write("Quad.obj", kObj);
    f.Write("Quad.obj.meta", StringView("ModelImporter:\n  isReadable: 1\n"));
    f.Open();
    Guid guid = FindAssetAtPath("Quad.obj")->guid;
    Object* main = LoadAsset(guid);
    HX_REQUIRE(main && main->Is<GameObject>());
    GameObject* root = static_cast<GameObject*>(main);
    HX_CHECK(root->Name() == StringView("Quad") && root->IsPersistent() && !root->scene().IsValid() && !root->activeInHierarchy());
    HX_CHECK(root->transform()->childCount() == 2 && root->transform()->GetChild(0)->Name() == StringView("First"));
    Array<Object*> all;
    LoadAllAssets(guid, all);
    Array<Mesh*> meshes;
    for (Object* o : all)
        if (o->Is<Mesh>())
            meshes.Push(static_cast<Mesh*>(o));
    HX_REQUIRE(meshes.Size() == 2);
    Mesh* first = meshes[0]->Name() == StringView("First") ? meshes[0] : meshes[1];
    HX_CHECK(first->subMeshCount() == 1 && first->GetIndexCount(0) == 6 && first->HasVertexAttribute(VertexAttribute::Normal));
    HX_CHECK(first->HasVertexAttribute(VertexAttribute::Tangent) && first->HasVertexAttribute(VertexAttribute::TexCoord0) && first->isReadable());
    // The engine's frame mirrors x of the right handed file.
    HX_CHECK(std::fabs(first->bounds().extents.x - 0.5f) < 1e-4f && std::fabs(first->bounds().center.x + 0.5f) < 1e-4f);
    Array<Vector3> n = first->normals();
    HX_CHECK(!n.Empty() && std::fabs(std::fabs(n[0].z) - 1) < 1e-4f);
    // The model is the prefab of its guid.
    HX_CHECK(FindPrefab(guid) == root);
    GameObject* copy = static_cast<GameObject*>(Instantiate(root));
    HX_CHECK(copy && copy->activeInHierarchy() && !copy->IsPersistent() && copy->transform()->childCount() == 2);
    // The objects' references: stable fileIDs from type and name.
    Guid g;
    i64 fileID = 0;
    i32 type = 0;
    HX_CHECK(GetAssetReference(first, g, fileID, type) && g == guid && fileID == AssetObjectFileId(ClassId::Mesh, "First"));
    HX_CHECK(LoadAsset(guid, fileID) == first);
}

HX_TEST(Asset, GltfWithTexturesNextToIt)
{
    Folder f;
    f.Write("Tri/tri.gltf", kGltf);
    Array<u8> bin = TriangleBuffer();
    f.Write("Tri/tri.bin", Span<const u8>(bin.Data(), bin.Size()));
    Array<u8> albedo = Png(4, 4, Color32(200, 100, 50, 255)), bump = Png(4, 4, Color32(128, 128, 255, 255));
    f.Write("Tri/albedo.png", Span<const u8>(albedo.Data(), albedo.Size()));
    f.Write("Tri/bump.png", Span<const u8>(bump.Data(), bump.Size()));
    f.Open();
    Guid guid = FindAssetAtPath("Tri/tri.gltf")->guid;
    Array<Object*> all;
    LoadAllAssets(guid, all);
    Texture2D* a = nullptr;
    Texture2D* b = nullptr;
    Mesh* m = nullptr;
    for (Object* o : all)
    {
        if (o->Is<Texture2D>())
            (o->Name() == StringView("albedo") ? a : b) = static_cast<Texture2D*>(o);
        if (o->Is<Mesh>())
            m = static_cast<Mesh*>(o);
    }
    HX_REQUIRE(a && b && m);
    // Color maps are sRGB, data maps linear.
    HX_CHECK(a->isDataSRGB() && !b->isDataSRGB() && a->mipmapCount() == 3);
    HX_CHECK(m->Name() == StringView("TriMesh") && m->IndexCount() == 3 && m->HasVertexAttribute(VertexAttribute::TexCoord0));
    GameObject* root = static_cast<GameObject*>(LoadAsset(guid));
    HX_CHECK(root && root->transform()->GetChild(0)->localPosition() == Vector3(0, 2, 0));
    // The paths next to the model are also assets of their own.
    HX_CHECK(FindAssetAtPath("Tri/albedo.png") != nullptr);
}

HX_TEST(Asset, AsyncLoadsShareOneRequest)
{
    Folder f;
    for (int i = 0; i < 6; i++)
    {
        Array<u8> png = Png(16, 16, Color32(u8(i * 40), 0, 0, 255));
        f.Write(Format("Resources/t{}.png", i), Span<const u8>(png.Data(), png.Size()));
    }
    f.Open();
    jobs::Initialize();
    ResourceRequest a = Resources::LoadAsync<Texture2D>("t0");
    ResourceRequest b = Resources::LoadAsync("t0");
    ResourceRequest late = Resources::LoadAsync("t1");
    HX_CHECK(!a.isDone() && PendingAssetLoads() == 2 && a.asset() == nullptr);
    late.SetPriority(5);
    HX_CHECK(RunUntil(a) && RunUntil(b) && RunUntil(late));
    HX_CHECK(a.asset() && a.asset() == b.asset() && a.asset() == Resources::Load("t0") && late.asset() && late.asset() != a.asset());
    // A synchronous load finishes the one in flight.
    ResourceRequest c = Resources::LoadAsync("t2");
    Object* now = Resources::Load("t2");
    HX_CHECK(now && c.isDone() && c.asset() == now && PendingAssetLoads() == 0);
    // Already loaded: done at once; nothing at the path: done with no asset.
    HX_CHECK(Resources::LoadAsync("t0").isDone() && Resources::LoadAsync("nothing").isDone() && Resources::LoadAsync("nothing").asset() == nullptr);
    // Through the player loop's UpdatePreloading.
    Array<ResourceRequest> many;
    for (int i = 3; i < 6; i++)
        many.Push(Resources::LoadAsync(Format("t{}", i)));
    for (const ResourceRequest& r : many)
        HX_CHECK(RunUntil(r) && r.asset() && r.asset()->Is<Texture2D>());
    jobs::Shutdown();
}

HX_TEST(Asset, SceneFilesReferToAssets)
{
    Folder f;
    f.Write("Quad.obj", kObj);
    f.Open();
    Guid guid = FindAssetAtPath("Quad.obj")->guid;
    Mesh* mesh = static_cast<Mesh*>(LoadAssetOfClass(guid, ClassOf<Mesh>()));
    HX_REQUIRE(mesh != nullptr);
    Scene scene = SceneManager::GetActiveScene();
    GameObject* g = GameObject::Create("User");
    g->AddComponent<Linker>()->list.Push(PPtr<Object>(mesh));
    String yaml, error;
    HX_REQUIRE(WriteSceneYaml(scene, yaml, error));
    char text[33];
    guid.ToChars(text);
    String expected = String("fileID: ").Append(Format("{}", AssetObjectFileId(ClassId::Mesh, "First"))).Append(", guid: ").Append(StringView(text, 32)).Append(", type: 3");
    HX_CHECK(StringView(yaml).Contains(expected));
    Array<u8> binary;
    HX_REQUIRE(WriteSceneBinary(scene, binary, error));
    // Everything goes; reading the scene loads the asset again.
    DestroyImmediate(g, true);
    ResetAssets();
    Cleanup();
    AddAssetSource(f.source);
    Array<String> errors;
    HX_REQUIRE(ReadSceneFile(StringView(yaml), scene, errors));
    HX_CHECK(errors.Empty());
    Linker* l = FindAnyObjectByType<Linker>();
    HX_REQUIRE(l && l->list.Size() == 1);
    Object* back = l->list[0].Get();
    HX_CHECK(back && back->Is<Mesh>() && back->Name() == StringView("First") && IsAssetLoaded(guid));
    // An async load of the binary scene loads its assets before it builds.
    DestroyImmediate(l->gameObject(), true);
    ResetAssets();
    Cleanup();
    AddAssetSource(f.source);
    MemorySceneSource memory;
    memory.Set("Level.helix", Span<const u8>(binary.Data(), binary.Size()));
    SceneManager::SetSceneSource(&memory);
    jobs::Initialize();
    AsyncOperation op = SceneManager::LoadSceneAsync("Level.helix", LoadSceneMode::Additive);
    HX_CHECK(RunUntil(op));
    Linker* again = FindAnyObjectByType<Linker>();
    HX_CHECK(again && again->list.Size() == 1 && again->list[0].Get() && again->list[0].Get()->Is<Mesh>());
    jobs::Shutdown();
    SceneManager::SetSceneSource(nullptr);
    Scene level = SceneManager::GetSceneByName("Level");
    if (level.isLoaded())
        SceneManager::UnloadSceneAsync(level);
    SceneManager::ProcessSceneLoads();
}

HX_TEST(Asset, PlayerDataFolderOfSerializedFiles)
{
    Folder f;
    // Assets written by code: a mesh in binary and a texture in YAML, with their index.
    Mesh* m = NewObject<Mesh>();
    m->SetName("Quad");
    const Vector3 v[] = {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
    const i32 t[] = {0, 1, 2, 0, 2, 3};
    m->SetVertices(Span<const Vector3>(v, 4));
    m->SetTriangles(Span<const i32>(t, 6));
    m->RecalculateNormals();
    Texture2D* tex = Texture2D::Create(4, 4, TextureFormat::RGBA32, true);
    tex->SetName("Checker");
    tex->SetPixel(1, 2, Color(0, 1, 0, 1));
    tex->Apply();
    Array<u8> meshFile, texFile;
    String error;
    Object* const mo[] = {m};
    Object* const to[] = {tex};
    HX_REQUIRE(WriteAssetFile(Span<Object* const>(mo, 1), meshFile, error, true));
    HX_REQUIRE(WriteAssetFile(Span<Object* const>(to, 1), texFile, error, false));
    HX_CHECK(StringView(reinterpret_cast<const char*>(texFile.Data()), texFile.Size()).Contains("--- !h!28 &2800000"));
    f.Write("data/quad.asset", Span<const u8>(meshFile.Data(), meshFile.Size()));
    f.Write("data/checker.asset", Span<const u8>(texFile.Data(), texFile.Size()));
    AssetEntry entries[2];
    entries[0].guid = Guid::Generate();
    entries[0].path = "data/quad.asset";
    entries[1].guid = Guid::Generate();
    entries[1].path = "data/checker.asset";
    HX_REQUIRE(WriteAssetIndex(f.root, Span<const AssetEntry>(entries, 2), error));
    DestroyImmediate(m, true);
    DestroyImmediate(tex, true);
    PlayerDataSource data(f.root);
    AddAssetSource(&data);
    Mesh* back = static_cast<Mesh*>(LoadAsset(entries[0].guid));
    HX_REQUIRE(back && back->Is<Mesh>());
    HX_CHECK(back->Name() == StringView("Quad") && back->vertexCount() == 4 && back->triangles().Size() == 6 && back->normals().Size() == 4);
    HX_CHECK(back->bounds().extents == Vector3(0.5f, 0.5f, 0) && back->IsPersistent());
    Guid g;
    i64 fileID = 0;
    i32 type = 0;
    HX_CHECK(GetAssetReference(back, g, fileID, type) && fileID == 4300000 && type == kAssetTypeSerialized);
    Texture2D* t2 = static_cast<Texture2D*>(LoadAsset(entries[1].guid));
    HX_REQUIRE(t2 && t2->Is<Texture2D>());
    HX_CHECK(t2->width() == 4 && t2->mipmapCount() == 3 && t2->GetPixel(1, 2) == Color(0, 1, 0, 1) && t2->Name() == StringView("Checker"));
    RemoveAssetSource(&data);
}

HX_TEST(Asset, UnloadingAssets)
{
    Folder f;
    Array<u8> png = Png(4, 4, Color32(255, 0, 0, 255));
    f.Write("Resources/a.png", Span<const u8>(png.Data(), png.Size()));
    f.Write("Resources/a.png.meta", StringView("TextureImporter:\n  isReadable: 1\n"));
    f.Write("Resources/b.png", Span<const u8>(png.Data(), png.Size()));
    f.Open();
    Texture2D* a = Resources::Load<Texture2D>("a");
    Texture2D* b = Resources::Load<Texture2D>("b");
    HX_REQUIRE(a && b);
    EntityId aId = a->GetEntityId(), bId = b->GetEntityId();
    // UnloadAsset: the data goes, the object stays and takes its data again on the next load.
    Resources::UnloadAsset(a);
    HX_CHECK(EntityIdToObject(aId) == a && a->width() == 0 && !IsAssetLoaded(FindResource("a")->guid));
    HX_CHECK(Resources::Load<Texture2D>("a") == a && a->width() == 4 && a->GetPixel(0, 0) == Color(1, 0, 0, 1));
    // GameObjects cannot be unloaded that way.
    GameObject* holder = GameObject::Create("Holder");
    Resources::UnloadAsset(holder);
    HX_CHECK(EntityIdToObject(holder->GetEntityId()) == holder);
    // UnloadUnusedAssets: what a scene object reaches stays, the rest goes (code made objects too).
    holder->AddComponent<Linker>()->list.Push(PPtr<Object>(a));
    Mesh* loose = NewObject<Mesh>();
    Mesh* kept = NewObject<Mesh>();
    kept->hideFlags = HideFlags::DontUnloadUnusedAsset;
    EntityId looseId = loose->GetEntityId(), keptId = kept->GetEntityId();
    Texture2D* white = Texture2D::whiteTexture();
    AsyncOperation op = Resources::UnloadUnusedAssets();
    HX_CHECK(!op.isDone());
    ProcessAssetLoads();
    HX_CHECK(op.isDone() && EntityIdToObject(aId) == a && EntityIdToObject(bId) == nullptr && EntityIdToObject(looseId) == nullptr);
    HX_CHECK(EntityIdToObject(keptId) == kept && Texture2D::whiteTexture() == white && !IsAssetLoaded(FindResource("b")->guid));
    // Loading it again makes a new object.
    Texture2D* b2 = Resources::Load<Texture2D>("b");
    HX_CHECK(b2 && b2->GetEntityId() != bId);
}

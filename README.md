# Helix.Engine

The runtime of the Helix engine: the object model, GameObjects and components, tags and layers, the lifecycle of
objects (instantiate, InstantiateAsync, destroy), the transform hierarchy, scenes and scene files, prefabs and pools,
time, the player loop, the application and settings assets, the asset runtime with meshes and textures, shaders and
materials, the rendering components with a placeholder renderer on Helix.RHI, and the screen as a player host
reports it.

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Engine/Object/EntityId.h` | `EntityId`: a 64-bit object id (slot and generation), no order or sign meaning |
| `Source/Helix/Engine/Object/Class.h` | `ClassInfo`, the class registry, `ClassOf<T>()`, `IsDerivedFrom`; every class is also a type of the scripting core (Helix.Scripting, module HelixEngine, with its ClassId as an attribute and its serialized fields by property path through `Object.ReadSerializedProperty` and `ApplySerializedProperty`) |
| `Source/Helix/Engine/Object/Object.h` | `Object`, `HideFlags`, `HX_ENGINE_CLASS`, `EntityIdToObject`, `FindObjectsByType`, `FindAnyObjectByType`, `PPtr<T>`, `ReferenceRemap` |
| `Source/Helix/Engine/Object/SmallArray.h` | `SmallArray<T, N>`: inline storage for small lists |
| `Source/Helix/Engine/GameObject/GameObject.h` | `GameObject`, `Component`, `Behaviour` |
| `Source/Helix/Engine/GameObject/Tags.h` | `TagHandle`, tags and the 32 layers |
| `Source/Helix/Engine/GameObject/Lifecycle.h` | `Instantiate`, `InstantiateParameters`, `InstantiateGameObjects`, `Destroy`, `DestroyImmediate`, `DontDestroyOnLoad` |
| `Source/Helix/Engine/Transform/Transform.h` | `Transform`: local and world values, hierarchy, sibling order, movement, single and span conversions, `Space` |
| `Source/Helix/Engine/Transform/Hierarchy.h` | `TransformHierarchy` blocks, `UpdateWorldMatrices`, `RegisterTransformSystem`, `SetTransformInterest`, `DispatchTransformChanges` |
| `Source/Helix/Engine/Transform/TransformHandle.h` | `TransformHandle` and its child enumerator |
| `Source/Helix/Engine/Scene/Scene.h` | `Scene`, `SceneHandle`, `LoadSceneMode`, `LoadSceneParameters`, `UnloadSceneOptions` |
| `Source/Helix/Engine/Scene/LoadableSceneId.h` | `LoadableSceneId`: a scene asset by its guid |
| `Source/Helix/Engine/Scene/SceneManager.h` | `SceneManager`, `SceneEvent`, `SceneSource` (`FileSceneSource`, `MemorySceneSource`, `AssetSceneSource`) |
| `Source/Helix/Engine/Async/AsyncOperation.h` | `AsyncOperation`, `ResourceRequest` |
| `Source/Helix/Engine/Scene/SceneFile.h` | `WriteSceneYaml`, `WriteSceneBinary`, `ReadSceneFile` |
| `Source/Helix/Engine/Scene/SceneFormat.h` | `SceneFormat`, `SceneBuilder`, `RegisterSceneFormat`: scene file formats as plug ins |
| `Source/Helix/Engine/Prefab/Prefab.h` | `LoadPrefab`, `UnloadPrefab`, `FindPrefab`, `RegisterPrefab`, `PrefabSource` |
| `Source/Helix/Engine/Pool/ObjectPool.h` | `ObjectPool<T>`, `LinkedPool<T>`, `PoolCallbacks<T>` |
| `Source/Helix/Engine/Time/Time.h` | `Time`, `TimeClock` |
| `Source/Helix/Engine/PlayerLoop/PlayerLoop.h` | `PlayerLoop`, `PlayerLoopSystem` |
| `Source/Helix/Engine/Application/Application.h` | `Application`, `RuntimePlatform`, `NetworkReachability`, `LogType` |
| `Source/Helix/Engine/Settings/Settings.h` | `TimeManager`, `TagManager`, `PlayerSettings`, `BuildSettings`, `RegisterSettings`, `LoadSettingsFile` |
| `Source/Helix/Engine/Screen/Screen.h` | `Screen`, `FullScreenMode`, `ScreenOrientation`, `Resolution`, `RefreshRate`, `DisplayInfo`, the host's `ScreenReport` and `ScreenRequests` |
| `Source/Helix/Engine/Event/Event.h` | `Event<Args...>`, `Query<Args...>` |
| `Source/Helix/Engine/Asset/Asset.h` | The asset runtime: `AssetSource` (`FolderAssetSource`, `PlayerDataSource` with a `DataFileReader`), the catalog, `LoadAsset`, `LoadAssetAsync`, `UnloadUnusedAssets`, `WriteAssetFile` |
| `Source/Helix/Engine/Asset/AssetLoader.h` | `AssetLoader`, `RegisterAssetLoader`: loaders by file extension, decode and integrate halves |
| `Source/Helix/Engine/Asset/Resources.h` | `Resources`: `Load`, `LoadAll`, `LoadAsync`, `UnloadAsset`, `UnloadUnusedAssets` |
| `Source/Helix/Engine/Mesh/Mesh.h` | `Mesh`, `VertexAttributeDescriptor`, `SubMeshDescriptor`, `VertexAttribute`, `VertexAttributeFormat`, `IndexFormat`, `MeshTopology` |
| `Source/Helix/Engine/Texture/Texture.h` | `Texture`, `Texture2D`, `TextureFormat`, `FilterMode`, `TextureWrapMode` |
| `Source/Helix/Engine/Shader/Shader.h` | `Shader`, `ShaderVariant`, `ShaderPropertyType`, `ShaderPropertyFlags`: shaders of C# assemblies, variants on demand, global keywords and properties |
| `Source/Helix/Engine/Material/Material.h` | `Material`, `MaterialPropertyBlock`, `MaterialPassData` (block bytes and textures per pass and variant) |
| `Source/Helix/Engine/Material/PropertySheet.h` | `PropertyToID`, `PropertySheet`: property values by id |
| `Source/Helix/Engine/Rendering/Renderer.h` | `MeshFilter`, `Renderer`, `MeshRenderer`, `ShadowCastingMode` |
| `Source/Helix/Engine/Rendering/Camera.h` | `Camera`, `CameraClearFlags`, `CameraType`, `GeometryUtility` |
| `Source/Helix/Engine/Rendering/Light.h` | `Light`, `LightType`, `LightShadows`, `LightRenderMode` |
| `Source/Helix/Engine/Rendering/Graphics.h` | `Graphics` (device, target, `RenderFrame`), `RenderTarget`, `RenderStats`: the placeholder renderer |
| `Source/Helix/Engine/Rendering/ScreenCapture.h` | `ScreenCapture::CaptureScreenshot`: the frame drawn again into a texture and written as PNG |
| `Source/Helix/Engine/Script/GameBehaviour.h` | `GameBehaviour` (class 114): the base of scripts in C++ and C# |
| `Source/Helix/Engine/Script/MonoScript.h` | `MonoScript` (class 115): the script asset (assembly or game module, namespace, class, execution order) |
| `Source/Helix/Engine/Script/ScriptClass.h` | `ScriptClass`, `ScriptMessage`, the registry of script classes of both languages |
| `Source/Helix/Engine/Script/ScriptRuntime.h` | The scheduler of scripts (`RunScriptPhase`, `CallScriptMessage`, `BroadcastScriptMessage`), `AddScript`, `SendScriptMessage` (`SendMessage`, `BroadcastMessage`, `SendMessageUpwards`), C++ game modules (`LoadGameModule`, `RegisterLinkedGameModule`, `UnloadGameModule`) |
| `Shaders/` | The built in shaders in C# (`Hidden/InternalErrorShader`, `Hidden/InternalClear`, the engine's shader variables and vertex input locations), built with the .NET SDK and embedded |
| `Tests/Shaders/` | C# test shaders, embedded into the tests |
| `Tests/`, `Benchmarks/` | Ids, the class tree, components, hierarchies, enable and destroy order, clones, world math, change dispatch, scenes and scene files, assets, meshes and textures; throughput |

## Design

- Every object has an `EntityId`: a slot in one table and the slot's generation. Resolving an id is two loads and a
  compare; an id of a destroyed object resolves to null, and a reused slot carries a new generation.
- Classes are numbered depth first, so "is this class derived from that one" is one subtraction and one compare, and
  the objects of a class and all its subclasses are the classes in one contiguous range.
- Objects of each class live in a pool of their own (64 objects a block) and are listed densely per class:
  `FindObjectsByType` walks those lists, never the whole table.
- A GameObject keeps its components as `{id, class}` entries inline (4 without allocating); `GetComponent` checks the
  recorded class range and resolves the id, without calling into the component.
- References between objects are `PPtr<T>` (an id). Instantiate copies every object of a hierarchy, then remaps the
  references that point inside the copy through each class's `Transfer`.
- Every root and its descendants live in one block (`TransformHierarchy`) with a capacity: side by side arrays of
  local TRS, cached world matrix and rotation, parent index, subtree size, child count and flags, the nodes in depth
  first order. A subtree is one contiguous range, so moving it is one cut and one insert, `IsChildOf` is a range test
  and a world update is a forward loop. Hierarchies of one node come from pools.
- World values are cached. A change marks the subtree's range dirty; reading a world value recomputes the chain up to
  the first clean ancestor, and `UpdateWorldMatrices` brings every stale hierarchy up to date, hierarchies in parallel
  on Helix.Jobs.
- Systems register for an interest bit and set it on the transforms they care about. A change ORs each transform's
  interest into its changed bits; `DispatchTransformChanges` visits the changed transforms of one system with world
  matrices up to date, hierarchy by hierarchy in parallel. Only hierarchies with pending changes are visited.
- The hierarchy does not change while activation hooks run or while the objects involved are being destroyed; destroyed
  children's nodes stay (marked dead) until the topmost destroyed Transform removes the range at once.
- Every hierarchy belongs to a scene; the roots of a scene are its own list. New GameObjects and clones go into the
  active scene unless told otherwise; DontDestroyOnLoad moves a root into a scene of its own that Single loads keep.
- Scene files are one YAML document per object (`--- !h!<classID> &<fileID>`, the class's fields below its name) with
  references as `{fileID: N}` and the roots in order in a last SceneRoots document; the binary form is a serialized
  file of the same objects. The loader knows no class: it creates objects by class ID through the class registry and
  reads them through each class's Transfer, with references going through one table that maps fileIDs to objects.
  Then it checks GameObjects and components, builds each root's transform block in one go from the parents and children
  the Transforms read, and enables the objects parents first. Formats are plug ins (`RegisterSceneFormat`); YAML and
  binary come registered. Objects read from a file keep their fileIDs when the scene is written again.
- Prefab assets are GameObjects outside every scene (persistent, never enabled), loaded from prefab files through the
  scene formats and found by guid (`PrefabSource` is the hook for an asset system). PrefabInstance documents in scene
  and prefab files copy their source, give the copy's objects the fileIDs (instance XOR source object) & 0x7FFF...
  that stripped placeholders and added objects use, apply the property modifications (paths such as
  `m_LocalPosition.x`, `list.Array.data[2]`), drop removed components and GameObjects and attach added ones; nested
  prefabs and variants are instances inside prefab files. Players keep no prefab links.
- Copies of a prefab asset go through a template built once (its objects in order and an index for remapping), so a
  copy is copy construction into the class pools, one hierarchy block and remapping by index. `InstantiateAsync` makes
  copies on the main thread within `SetIntegrationTimeMS` per frame (`ProcessAsyncInstantiations`), each enabled when
  made; `allowSceneActivation` holds it, `WaitForCompletion` finishes it, `Cancel` undoes it. Destroying stays immediate: its
  hooks and the table removal are the cost, not releasing memory (measured), and pools reuse copies instead.
- `LoadScene` loads in the next `ProcessSceneLoads`; `LoadSceneAsync` parses on Helix.Jobs and waits at 0.9 while
  `allowSceneActivation` is off; a `LoadScene` makes the pending async loads finish first. Single unloads the other
  scenes first, then the new scene becomes active, its objects are enabled, then `sceneLoaded` is raised. Scene bytes
  come from a `SceneSource` (files by default); names resolve through the build scene list.
- A frame is the player loop: the phases TimeUpdate, Initialization, EarlyUpdate, FixedUpdate (run once per fixed step
  the frame's time allows), PreUpdate, Update, PreLateUpdate and PostLateUpdate, each a list of named systems. The
  engine's own work is registered there by name (Time, scene loads and async instantiations in EarlyUpdate, the
  destroy queue and the world matrices in PostLateUpdate before rendering); systems of others go in with
  `InsertSystem`. `SetPlayerLoop` compiles the tree into a flat list, so a frame does not walk a tree.
- Time takes real time from a replaceable clock, caps it by maximumDeltaTime, scales it by timeScale (0 stops game
  time, frames go on) and steps fixed time in fixedDeltaTime steps; inside a step deltaTime and time are the fixed
  ones. Settings files set it up (TimeManager), with tags and layers (TagManager) and the product (PlayerSettings);
  more kinds of settings register by class ID.
- `Application::Quit` asks `wantsToQuit` (any false keeps running), then raises `quitting`, unloads every scene with
  its hooks and raises `unloading`; `RunFrame` returns false from then on.
- Assets come from sources that register (`AddAssetSource`): a folder of source files for development (a file's guid
  from its .meta, or from its path when it has none) and a player data folder of the engine's serialized files with
  an index. The catalog maps guids, paths and Resources paths to entries; the loaded objects of each asset are kept by
  (guid, fileID): the main object of a single object asset at classID * 100000, objects an importer makes at an id from
  their type and name. Scene and prefab files write references to them as `{fileID, guid, type}` and load them when
  read; async scene loads request their assets while parsing (binary files list them as externals) and build once
  they are in. Prefab and model files are the prefabs of their guids.
- Loaders register by file extension, each a decode half (any thread: bytes to data) and an integrate half (main
  thread: objects in their class pools). Images load through every decoder Helix.Image has, with the TextureImporter
  settings of a .meta (mips, sRGB, readable, max size, filter, wrap); glTF, GLB, FBX and OBJ through Helix.Model with
  the importer's axis and unit conversion, triangle lists, normals and tangents where the file has none and the vertex
  cache and fetch order, into a Mesh per model mesh (a submesh per primitive), Texture2Ds for its images (sRGB for
  color maps) and a prefab of Transforms; the engine's .asset and .prefab files through the scene formats.
- A load reads on one IO thread (highest priority first), decodes on Helix.Jobs and integrates in UpdatePreloading
  within the main thread time of `Application::backgroundLoadingPriority` (2 to 50 ms); loads of one asset share one
  request, and a synchronous load runs the steps at once and finishes a request in flight. Without threads (the Web)
  the reads and decodes run in UpdatePreloading.
- `UnloadAsset` drops an asset's data and keeps its object, which takes the data again when the asset loads.
  `UnloadUnusedAssets` keeps whatever scene objects, DontDestroyOnLoad objects and DontUnloadUnusedAsset objects reach
  through the classes' references (an asset whole when any of its objects is reached) and destroys the other assets and
  the objects made by code outside the scenes.
- A Mesh is one block of vertex streams in the layout of its `VertexAttributeDescriptor`s (attributes in the order of
  `VertexAttribute` inside each of up to 4 streams), one index block of 16 or 32 bit indices, submeshes as ranges of
  it and bounds. The simple arrays (`vertices`, `normals`, `uv`, `colors32`, `triangles`) convert to and from the
  layout; `SetVertexBufferParams` / `SetVertexBufferData` and the index and submesh calls write the blocks as they are.
  A Texture2D is a TextureData of Helix.Image (every mip in one block, rows bottom first) with the sampling settings;
  the pixel calls convert through Helix.Image, block formats decode for reading. Both carry a version the renderer
  watches for uploads.
- Shaders are C#: a .dll asset gives one Shader per ShaderDefinition it declares, read by Helix.ShaderCompiler; the
  assemblies it references come from the engine's embedded HelixEngine.Shaders and HelixEngine.BuiltinShaders or
  from next to it, its PDB gives file:line in errors. A variant compiles on demand for a pass, the keyword values and a
  target (translation, linking, reflection, property binding, both backends through one ShaderCache). A variant that
  does not compile logs its errors once and draws with Hidden/InternalErrorShader (magenta); nothing asserts. The
  engine's blocks sit at uniform slots 9 (camera) and 10 (draw), the dynamic keywords at 11, materials at 0 to 8.
- A Material holds its values in one PropertySheet (an entry per property id, numbers in one pool, textures with their
  tiling), the shader's defaults filled in, values of properties the shader lacks kept. For the renderer it keeps per
  pass and variant the bytes of the uniform blocks its properties go to (laid out by std140, so float and vector arrays
  set by name land 16 bytes apart) and the texture of every slot (a property's default texture by name when it has
  none), written again only when a value or a global changed; the variant itself is looked up again only when the
  shader or a keyword changed. Members of material blocks that are no properties take the material's value by name,
  else the global one. .mat files are YAML documents with the reference's fields (saved properties as pairs).
- Renderers are thin components: a manager keeps every renderer that takes part (enabled component on an active
  GameObject) in dense arrays (world matrix, local and world bounds, mesh, layer mask, sorting order, flags), follows
  the Transform system through its interest bit and reads the component only when something of it changes. At the end
  of the frame it copies what changed into a render side in structure of arrays, which culling and drawing read.
  MeshFilter.mesh and Renderer.material(s) make copies owned by the component on first access.
- Cameras keep the reference's matrices (camera space looks along -z, clip depth -1 to 1); the renderer gives the GPU
  the projection with depth 0 to 1. Vertex inputs are [Location(n)] for VertexAttribute n; the engine's blocks take
  slots 9 (camera: view, projection, position, the main light, ambient) and 10 (draw: object to world and back).
- The placeholder renderer (until the pipelines): the host sets a device and a target; each frame every enabled
  camera by depth culls by frustum (on jobs) and culling mask, sorts opaque queues by variant and front to back and
  transparent ones back to front, and draws each submesh with the first pass of its material that is no shadow, depth
  or meta pass (the error shader for missing materials). A camera with a viewport smaller than the target clears it by
  drawing (Hidden/InternalClear). GPU meshes (formats the GPU lacks widened to float, missing attributes read zero),
  textures, samplers, shaders and pipelines are cached by the versions of their sources; meshes and textures that are
  not readable drop their CPU copy once uploaded.
- Screen keeps what the host reports (render size, dpi, mode, displays, window position) and queues what scripts ask
  (resolution and mode, window moves as AsyncOperations, orientation, sleep timeout, brightness); the host takes the
  queue at the end of the frame and applies it.
- Scenes can be assets: with `AssetSceneSource` scene files (`.helix`) are listed by the asset catalog and read
  through their source, `LoadSceneAsync(LoadableSceneId)` loads one by its guid, and `BuildSettings` in a settings
  file gives the build scene list.
- `Destroy` is queued with a delay in scaled time and runs at `ProcessDestroyQueue`, after the update loop and before
  rendering; `DestroyImmediate` destroys at once. Children go first, then components from the last to the first, then
  the GameObject; behaviours get their disable hook before their destroy hook.

## Usage

```cpp
using namespace helix::engine;

class Health : public Behaviour
{
    HX_ENGINE_CLASS(Health, Behaviour, 1000101, ClassBehaviour)
public:
    float hp = 100;
    template <class V>
    void Transfer(V& v) { Base::Transfer(v); v.Field("hp", hp); }
};

GameObject* enemy = GameObject::Create("Enemy");
enemy->AddComponent<Health>();
enemy->transform()->SetParent(level->transform());     // world placement kept
enemy->transform()->SetLocalPosition(Vector3(0, 1, 0));
Vector3 at = enemy->transform()->position();

GameObject* copy = Instantiate(enemy, Vector3(5, 0, 0), Quaternion::identity());
EntityId id = copy->GetEntityId();

Array<Health*> all;
FindObjectsByType(all);

Destroy(copy, 2.0f);
// Each frame, after the update loop:
SetLifecycleTime(scaledTime);
ProcessDestroyQueue();
GameObject* gone = EntityIdToObject<GameObject>(id); // null once destroyed
```

Scenes:

```cpp
StringView build[] = {"Assets/Scenes/Menu.helix", "Assets/Scenes/Level1.helix"};
SceneManager::SetBuildScenes(Span<const StringView>(build, 2));
SceneManager::sceneLoaded().Add([](void*, Scene s, LoadSceneMode) { Log(s.name()); });

AsyncOperation op = SceneManager::LoadSceneAsync("Level1", LoadSceneMode::Additive);
op.SetAllowSceneActivation(false);
// Each frame, after the update loop:
SceneManager::ProcessSceneLoads();
if (op.progress() >= 0.9f)
    op.SetAllowSceneActivation(true);

String text, error;
WriteSceneYaml(SceneManager::GetActiveScene(), text, error);
```

Prefabs and pools:

```cpp
GameObject* crate = LoadPrefab(text, errors, crateGuid);   // scene files with instances of crateGuid load after this
GameObject* one = Instantiate(crate, Vector3(0, 0, 5), Quaternion::identity());

AsyncInstantiateOperation::SetIntegrationTimeMS(2.0f);
AsyncInstantiateOperation op = InstantiateAsync(crate, 500, Span<const Vector3>(spawnPoints, n), {});
// Each frame:
ProcessAsyncInstantiations();

PoolCallbacks<GameObject*> cb;
cb.user = crate;
cb.create = [](void* p) { return Instantiate(static_cast<GameObject*>(p)); };
cb.onGet = [](void*, GameObject*& g) { g->SetActive(true); };
cb.onRelease = [](void*, GameObject*& g) { g->SetActive(false); };
cb.onDestroy = [](void*, GameObject*& g) { DestroyImmediate(g); };
ObjectPool<GameObject*> pool(cb);
GameObject* bullet = pool.Get();
pool.Release(bullet);
```

The frame:

```cpp
Array<String> errors;
LoadSettingsFile(timeAndTagsText, errors);
PlayerLoopSystem mine;
mine.type = "MyUpdate";
mine.updateFunction = [](void*) { /* every frame, before the scripts' Update */ };
PlayerLoop::InsertSystem("Update", mine, "ScriptRunBehaviourUpdate");
Application::SetTargetFrameRate(60);
while (Application::RunFrame())
    ;
return Application::ExitCode();
```

Systems that keep their own copy of transform data (renderers, physics) follow changes like this:

```cpp
u32 renderer = RegisterTransformSystem("Renderer");
SetTransformInterest(meshTransform, renderer, true);
// Each frame:
DispatchTransformChanges(renderer, [](TransformHierarchy& h, u32 i) {
    StoreMatrix(h.transforms[i], h.world[i]); // runs on worker threads: write only data of your own per node
});
```

Assets, from a folder of source files:

```cpp
FolderAssetSource folder("Assets");
AddAssetSource(&folder);
Texture2D* logo = Resources::Load<Texture2D>("ui/logo");      // Assets/.../Resources/ui/logo.png
ResourceRequest level = Resources::LoadAsync<Mesh>("terrain"); // done in a later frame
GameObject* ship = static_cast<GameObject*>(LoadAsset(FindAssetAtPath("Models/ship.glb")->guid));
Instantiate(ship);
```

## Performance (Ryzen 7 3750H, release)

| Benchmark | Result |
|---|---|
| Create 100000 GameObjects with a Transform and two components, then destroy them | 0.75 us per GameObject |
| `GetComponent` (a component, a base class, the Transform) on 1000 GameObjects | 7.4 ns per lookup |
| `EntityIdToObject` over 100000 ids | 3.3 ns per id |
| `FindObjectsByType<Behaviour>` over 10000 GameObjects | 29 ns per object returned |

Transform numbers, measured with seven other processes busy on the cores (the benchmark at high priority; the object
model rows above read the same under that load):

| Benchmark | Result |
|---|---|
| 100000 transforms in 2000 hierarchies of 50: every local position set, then `UpdateWorldMatrices` on 7 workers | 58 ns per transform |
| The same, each set followed by reading its world position | 91 ns per transform |
| `SetParent` of a 10 node subtree between two hierarchies of 1000, world placement kept | 1.0 us per move |
| Every root moved, then `DispatchTransformChanges` over the 100000 changed transforms | 22 ns per transform |
| `TransformPoints` over 100000 points | 3.6 ns per point |

Scene files of 10000 GameObjects (1000 roots with 9 children, each with a Transform and a behaviour with a reference),
under the same load; reading includes destroying the objects again, which alone (with making them) takes 2.4 us:

| Benchmark | Result |
|---|---|
| `WriteSceneYaml` (7.6 MB) | 12.8 us per GameObject |
| `ReadSceneFile` from YAML: parse, create, read, link, enable | 15 us per GameObject |
| `WriteSceneBinary` (4.0 MB, no type trees) | 4.1 us per GameObject |
| `ReadSceneFile` from the binary file | 8.0 us per GameObject |

Prefabs of 100 objects (a root and 32 children, each with a Transform and a behaviour with a reference), 1000 copies,
under the same load at normal priority:

| Benchmark | Result |
|---|---|
| `Instantiate` of the prefab asset (template), then `DestroyImmediate` | 300 ns and 150 ns per object |
| The same from a scene object (no template) | 8 % slower |
| `InstantiateAsync` of 1000 copies with a 2 ms budget | worst frame 2.1 to 3.7 ms (the machine's other load), 18 to 70 frames |
| `ObjectPool` Get and Release of a pooled copy (33 behaviours enabled and disabled) | 2.6 to 3.6 times cheaper than a new copy and its destroy |

Assets from a folder of source files (64 PNGs of 256 x 256 noise, an OBJ grid of 32768 triangles), measured on a warm
machine (numbers swing up to 2 times with its clock):

| Benchmark | Result |
|---|---|
| Texture load on the main thread (decode 3.0 ms, mips 2.6 ms in Helix.Image) | 6.0 ms per texture |
| `LoadAssetAsync` of the 64 on 7 workers, integrated in UpdatePreloading | 1.6 ms per texture, the longest main thread frame under 1 ms |
| OBJ load: read, conversion, normals, tangents, cache order, a Mesh and a prefab | 2.5 us per triangle |
| `SetVertices`, `SetTriangles` and `RecalculateNormals` of a 65536 vertex grid | 90 ns per vertex |

Materials of the C# test shader Tests/Surface (Direct3D target):

| Benchmark | Result |
|---|---|
| `GetPassData` of a material that did not change (what a draw asks) | 12 ns |
| `SetFloat`, then `GetPassData` writing the blocks again | 0.47 us |
| A variant compiled for the first time: translation, reflection, binding, backend | 0.25 ms |

10000 renderers (16 materials), D3D11 on the GTX 1660 Ti:

| Benchmark | Result |
|---|---|
| Every renderer moved: the manager's bounds and the copy for the render side (after 58 ns to set and 42 ns for world matrices) | 106 ns per renderer |
| Nothing moved: the manager's frame | 3.8 ns per renderer |
| A frame of 5100 visible quads: culling, sorting, recording, submitting, waiting for the GPU | 6.0 ms |

The player loop: an empty frame of the engine's loop (its own systems run with nothing to do) takes about 170 ns;
walking the same tree instead of the compiled list takes 2.8 times as long.

## Scripts

- Scripts of both languages derive from `GameBehaviour`. A C++ script is an `HX_CLASS` class of a game module whose
  generated registration (Helix.Bindgen) adds it as a script class; its messages (Awake, Start, Update, ...) are
  found by name in the header, without virtual. A C# script is a GameBehaviour whose C# object is its wrapper.
- One scheduler in the reference's order: Awake when the GameObject first becomes active, OnEnable, pending Starts
  before the first FixedUpdate or Update, the phases at their player loop systems, OnDisable, OnDestroy;
  OnApplicationFocus, Pause and Quit, the hierarchy and visibility messages. Within one execution order (Script
  Execution Order of the MonoScript, then DefaultExecutionOrder) the C++ scripts of a phase run first through
  function pointers of dense lists, then the C# ones in one call into C#.
- `HelixEngine` is also built as one shared library (`helix_engine_shared`, `Helix::EngineShared`:
  `HelixEngine.dll`, `libHelixEngine.so`) on Windows, Linux and Android: the player and the game modules it loads link
  it, so a module uses the engine of its process.

## Build

The build runs the .NET SDK (`dotnet` on the path, or `HELIX_DOTNET_ROOT`) for the C# shaders.

```
git clone https://github.com/kisstp2006/Helix.Engine
git -C Helix.Engine submodule update --init
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

## License

GPL-3.0, see `LICENSE`.

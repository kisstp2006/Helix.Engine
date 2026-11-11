# Helix.Engine

The runtime of the Helix engine: the object model, GameObjects and components, tags and layers, the lifecycle of
objects (instantiate and destroy), the transform hierarchy, scenes and scene files.

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Engine/Object/EntityId.h` | `EntityId`: a 64-bit object id (slot and generation), no order or sign meaning |
| `Source/Helix/Engine/Object/Class.h` | `ClassInfo`, the class registry, `ClassOf<T>()`, `IsDerivedFrom` |
| `Source/Helix/Engine/Object/Object.h` | `Object`, `HideFlags`, `HX_ENGINE_CLASS`, `EntityIdToObject`, `FindObjectsByType`, `FindAnyObjectByType`, `PPtr<T>`, `ReferenceRemap` |
| `Source/Helix/Engine/Object/SmallArray.h` | `SmallArray<T, N>`: inline storage for small lists |
| `Source/Helix/Engine/GameObject/GameObject.h` | `GameObject`, `Component`, `Behaviour` |
| `Source/Helix/Engine/GameObject/Tags.h` | `TagHandle`, tags and the 32 layers |
| `Source/Helix/Engine/GameObject/Lifecycle.h` | `Instantiate`, `InstantiateParameters`, `InstantiateGameObjects`, `Destroy`, `DestroyImmediate`, `DontDestroyOnLoad` |
| `Source/Helix/Engine/Transform/Transform.h` | `Transform`: local and world values, hierarchy, sibling order, movement, single and span conversions, `Space` |
| `Source/Helix/Engine/Transform/Hierarchy.h` | `TransformHierarchy` blocks, `UpdateWorldMatrices`, `RegisterTransformSystem`, `SetTransformInterest`, `DispatchTransformChanges` |
| `Source/Helix/Engine/Transform/TransformHandle.h` | `TransformHandle` and its child enumerator |
| `Source/Helix/Engine/Scene/Scene.h` | `Scene`, `SceneHandle`, `LoadSceneMode`, `LoadSceneParameters`, `UnloadSceneOptions` |
| `Source/Helix/Engine/Scene/SceneManager.h` | `SceneManager`, `AsyncOperation`, `SceneEvent`, `SceneSource` (`FileSceneSource`, `MemorySceneSource`) |
| `Source/Helix/Engine/Scene/SceneFile.h` | `WriteSceneYaml`, `WriteSceneBinary`, `ReadSceneFile` |
| `Source/Helix/Engine/Scene/SceneFormat.h` | `SceneFormat`, `SceneBuilder`, `RegisterSceneFormat`: scene file formats as plug ins |
| `Tests/`, `Benchmarks/` | Ids, the class tree, components, hierarchies, enable and destroy order, clones, world math, change dispatch, scenes and scene files; throughput |

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
- `LoadScene` loads in the next `ProcessSceneLoads`; `LoadSceneAsync` parses on Helix.Jobs and waits at 0.9 while
  `allowSceneActivation` is off; a `LoadScene` makes the pending async loads finish first. Single unloads the other
  scenes first, then the new scene becomes active, its objects are enabled, then `sceneLoaded` is raised. Scene bytes
  come from a `SceneSource` (files by default); names resolve through the build scene list.
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

Systems that keep their own copy of transform data (renderers, physics) follow changes like this:

```cpp
u32 renderer = RegisterTransformSystem("Renderer");
SetTransformInterest(meshTransform, renderer, true);
// Each frame:
DispatchTransformChanges(renderer, [](TransformHierarchy& h, u32 i) {
    StoreMatrix(h.transforms[i], h.world[i]); // runs on worker threads: write only data of your own per node
});
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

## Build

```
git clone https://github.com/kisstp2006/Helix.Engine
git -C Helix.Engine submodule update --init
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

## License

GPL-3.0, see `LICENSE`.

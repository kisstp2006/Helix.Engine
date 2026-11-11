# Helix.Engine

The runtime of the Helix engine: the object model, GameObjects and components, tags and layers, and the lifecycle of
objects (instantiate and destroy).

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Engine/Object/EntityId.h` | `EntityId`: a 64-bit object id (slot and generation), no order or sign meaning |
| `Source/Helix/Engine/Object/Class.h` | `ClassInfo`, the class registry, `ClassOf<T>()`, `IsDerivedFrom` |
| `Source/Helix/Engine/Object/Object.h` | `Object`, `HideFlags`, `HX_ENGINE_CLASS`, `EntityIdToObject`, `FindObjectsByType`, `FindAnyObjectByType`, `PPtr<T>`, `ReferenceRemap` |
| `Source/Helix/Engine/Object/SmallArray.h` | `SmallArray<T, N>`: inline storage for small lists |
| `Source/Helix/Engine/GameObject/GameObject.h` | `GameObject`, `Component`, `Behaviour`, `Transform` |
| `Source/Helix/Engine/GameObject/Tags.h` | `TagHandle`, tags and the 32 layers |
| `Source/Helix/Engine/GameObject/Lifecycle.h` | `Instantiate`, `InstantiateParameters`, `InstantiateGameObjects`, `Destroy`, `DestroyImmediate`, `DontDestroyOnLoad` |
| `Tests/`, `Benchmarks/` | Ids, the class tree, components, hierarchies, enable and destroy order, clones; object model throughput |

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
enemy->transform()->SetParent(level->transform());

GameObject* copy = Instantiate(enemy);
EntityId id = copy->GetEntityId();

Array<Health*> all;
FindObjectsByType(all);

Destroy(copy, 2.0f);
// Each frame, after the update loop:
SetLifecycleTime(scaledTime);
ProcessDestroyQueue();
GameObject* gone = EntityIdToObject<GameObject>(id); // null once destroyed
```

## Performance (Ryzen 7 3750H, release)

| Benchmark | Result |
|---|---|
| Create 100000 GameObjects with a Transform and two components, then destroy them | 0.71 us per GameObject |
| `GetComponent` (a component, a base class, the Transform) on 1000 GameObjects | 7.4 ns per lookup |
| `EntityIdToObject` over 100000 ids | 3.3 ns per id |
| `FindObjectsByType<Behaviour>` over 10000 GameObjects | 29 ns per object returned |

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

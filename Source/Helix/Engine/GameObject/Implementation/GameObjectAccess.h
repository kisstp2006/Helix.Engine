#pragma once

#include <Helix/Engine/GameObject/GameObject.h>

namespace helix::engine
{
    // What the object model's own code reaches inside GameObject, Component, Behaviour and Transform.
    struct GameObjectAccess
    {
        static SmallArray<GameObject::ComponentEntry, 4>& Components(GameObject& g) { return g.components_; }
        static void SetGameObject(Component& c, GameObject* g) { c.gameObject_ = PPtr<GameObject>(g); }
        static bool& ActiveInHierarchy(GameObject& g) { return g.activeInHierarchy_; }
        static bool& EnableCalled(Behaviour& b) { return b.enableCalled_; }

        // Brings activeInHierarchy of the GameObject and its subtree in line with its parents, running the components'
        // enable and disable hooks where it changed.
        static void UpdateActive(GameObject* g);
        // Runs a component's enable or disable hook if whether it takes part changed.
        static void Notify(Component* c);
    };
}

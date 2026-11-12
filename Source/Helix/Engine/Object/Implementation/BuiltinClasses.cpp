#include <Helix/Engine/Object/Implementation/BuiltinClasses.h>

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Engine/Settings/Settings.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Engine/Transform/Transform.h>

namespace helix::engine
{
    namespace
    {
        // Every class of the engine, in one table: a new class is one more row.
        using StaticClassFunction = u32 (*)();
        constexpr StaticClassFunction kBuiltinClasses[] = {
            &Object::StaticClass,       &GameObject::StaticClass,  &Component::StaticClass,      &Behaviour::StaticClass,
            &Transform::StaticClass,    &Camera::StaticClass,      &Light::StaticClass,          &Renderer::StaticClass,
            &MeshFilter::StaticClass,   &MeshRenderer::StaticClass, &Mesh::StaticClass,          &Material::StaticClass,
            &Shader::StaticClass,       &Texture::StaticClass,     &Texture2D::StaticClass,      &TimeManager::StaticClass,
            &TagManager::StaticClass,   &PlayerSettings::StaticClass, &BuildSettings::StaticClass,
        };
    }

    void RegisterBuiltinClasses()
    {
        static bool done = false;
        if (done)
            return;
        done = true;
        for (StaticClassFunction f : kBuiltinClasses)
            f();
    }
}

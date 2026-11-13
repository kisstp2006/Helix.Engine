#pragma once

#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Rendering/Pipeline/RenderStates.h>

// CommandBuffer: rendering commands recorded now and run where the buffer is executed (a context's
// ExecuteCommandBuffer, in the order of the context's own commands, or Graphics.ExecuteCommandBuffer at once). It
// draws into the target of the camera being rendered. C++ pipelines use it as it is; the C# class holds one by id.
namespace helix::engine
{
    class Mesh;
    class Renderer;
    class Texture;

    namespace render
    {
        // The kinds of commands, the context's own last; each kind runs through its entry in the executor's table.
        enum class Op : u8
        {
            ClearTarget,
            Viewport,
            Scissor,
            NoScissor,
            ViewProjection,
            GlobalFloat,
            GlobalInteger,
            GlobalVector,
            GlobalMatrix,
            GlobalTexture,
            Keyword,
            DrawMesh,
            DrawRenderer,
            BeginSample,
            EndSample,
            DrawRenderers,
            SetupCamera,
            RenderObjects,
            Count,
        };

        struct Command
        {
            Op op = Op::Count;
            bool flag = false, flag2 = false;
            i32 a = 0, b = 0;
            f32 v[4] = {};
            f32 f = 0;
            // The first of its matrices, its text or its draw request, and its property sheet (indices into the list's arrays).
            u32 index = 0;
            u32 sheet = ~0u;
            EntityId object, object2;
        };

        struct DrawRequest
        {
            CullingData* culling = nullptr;
            DrawingSettings drawing;
            EntityId overrideMaterial, overrideShader, fallbackMaterial;
            FilteringSettings filtering;
            RenderStateBlock state;
            // The fallback pipeline's draw: each material's first pass that is no shadow, depth, meta or motion pass.
            bool forward = false;
        };

        // Commands with what they refer to.
        struct CommandList
        {
            Array<Command> commands = Array<Command>();
            Array<Matrix4x4> matrices = Array<Matrix4x4>();
            Array<String> texts = Array<String>();
            Array<PropertySheet> sheets = Array<PropertySheet>();
            Array<DrawRequest> draws = Array<DrawRequest>();

            Command& Add(Op op);
            // The commands of another list after these (their indices moved).
            void Append(const CommandList& other);
            void Clear();
            usize Bytes() const;
        };
    }

    class CommandBuffer
    {
    public:
        CommandBuffer();
        ~CommandBuffer();
        CommandBuffer(const CommandBuffer&) = delete;
        CommandBuffer& operator=(const CommandBuffer&) = delete;

        const String& name() const { return name_; }
        void SetName(StringView name) { name_ = String(name); }
        // The memory its commands take.
        i32 sizeInBytes() const { return i32(list_.Bytes()); }
        void Clear() { list_.Clear(); }

        // The color (with that color) and depth (to that depth) of the current viewport; the whole target when the
        // viewport covers it, else by drawing over the viewport.
        void ClearRenderTarget(bool clearDepth, bool clearColor, Color backgroundColor, f32 depth = 1.0f);
        // A rect of the target in pixels, from its bottom left.
        void SetViewport(const Rect& pixelRect);
        void EnableScissorRect(const Rect& scissor);
        void DisableScissorRect();
        // The camera block's view and projection (the projection as the reference gives it; made the device's here).
        void SetViewProjectionMatrices(const Matrix4x4& view, const Matrix4x4& proj);

        void SetGlobalFloat(i32 nameID, f32 value);
        void SetGlobalFloat(StringView name, f32 value) { SetGlobalFloat(PropertyToID(name), value); }
        void SetGlobalInt(i32 nameID, i32 value) { SetGlobalFloat(nameID, f32(value)); }
        void SetGlobalInt(StringView name, i32 value) { SetGlobalFloat(name, f32(value)); }
        void SetGlobalInteger(i32 nameID, i32 value);
        void SetGlobalInteger(StringView name, i32 value) { SetGlobalInteger(PropertyToID(name), value); }
        void SetGlobalVector(i32 nameID, Vector4 value);
        void SetGlobalVector(StringView name, Vector4 value) { SetGlobalVector(PropertyToID(name), value); }
        void SetGlobalColor(i32 nameID, Color value) { SetGlobalVector(nameID, Vector4(value.r, value.g, value.b, value.a)); }
        void SetGlobalColor(StringView name, Color value) { SetGlobalColor(PropertyToID(name), value); }
        void SetGlobalMatrix(i32 nameID, const Matrix4x4& value);
        void SetGlobalMatrix(StringView name, const Matrix4x4& value) { SetGlobalMatrix(PropertyToID(name), value); }
        void SetGlobalTexture(i32 nameID, Texture* value);
        void SetGlobalTexture(StringView name, Texture* value) { SetGlobalTexture(PropertyToID(name), value); }
        void EnableShaderKeyword(StringView keyword);
        void DisableShaderKeyword(StringView keyword);
        void SetKeyword(const GlobalKeyword& keyword, bool value);

        // One submesh with one pass of the material (-1: every pass) at the matrix, with values of the block over it.
        void DrawMesh(Mesh* mesh, const Matrix4x4& matrix, Material* material, i32 submeshIndex = 0, i32 shaderPass = -1,
                      const MaterialPropertyBlock* properties = nullptr);
        // A renderer's mesh (its submesh) with a material's pass (-1: every pass), its matrix and its property block.
        void DrawRenderer(Renderer* renderer, Material* material, i32 submeshIndex = 0, i32 shaderPass = -1);
        // A profiler sample around the commands between them (while the profiler captures).
        void BeginSample(StringView name);
        void EndSample(StringView name);

        // ---------------------------------------------------------------------------------------------- native
        const render::CommandList& List() const { return list_; }
        render::CommandList& MutableList() { return list_; }
        // The id scripts hold (0 for none); a buffer of an id while it lives.
        u64 Id() const { return id_; }
        static CommandBuffer* Find(u64 id);

    private:
        String name_;
        render::CommandList list_;
        u64 id_ = 0;
    };
}

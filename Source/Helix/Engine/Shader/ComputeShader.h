#pragma once

#include <Helix/Engine/Material/PropertySheet.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Rendering/GraphicsBuffer.h>
#include <Helix/Engine/Shader/ShaderKeywords.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/ShaderCompiler/Cache/ShaderCache.h>
#include <Helix/ShaderCompiler/Definition/ShaderDefinition.h>
#include <Helix/ShaderCompiler/Reflection/Reflection.h>

#include <memory>

namespace helix::rhi
{
    class CommandList;
}

// ComputeShader (class 72): one compute definition of a C# shader assembly, its [ComputeShader] methods the kernels
// by index. Values (SetFloat, ...) are the shader's, textures and buffers each kernel's; what a kernel reads and
// nothing set takes the global values (Shader.SetGlobal*, CommandBuffer.SetGlobalBuffer). Variants compile on demand
// per kernel, keyword values and target through the shader cache; Dispatch runs at once, a command buffer's
// DispatchCompute in its order.
namespace helix::engine
{
    namespace ClassId
    {
        inline constexpr i32 ComputeShader = 72;
    }

    struct ShaderProgram;

    // One compiled variant of a kernel for one target.
    struct ComputeVariant
    {
        bool ok = false;
        String error;
        u32 kernel = 0;
        shadercompiler::ShaderTarget target = shadercompiler::ShaderTarget::Glsl430;
        shadercompiler::CompiledShader compute;
        shadercompiler::ShaderReflection reflection;
        Array<shadercompiler::KeywordValue> keywords = Array<shadercompiler::KeywordValue>();
        // The value each block member takes: its property id and element ("weights[3]": weights, 3).
        struct Member
        {
            u32 block;
            const shadercompiler::ReflectedMember* member;
            i32 id;
            u32 element;
        };
        Array<Member> members = Array<Member>();
        // The property id of every resource, in the reflection's order.
        Array<i32> resourceIds = Array<i32>();
        // Made on the device the variant was used on.
        void* device = nullptr;
        rhi::ShaderHandle shader;
        rhi::PipelineHandle pipeline;
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class ComputeShader : public Object
    {
        HX_ENGINE_CLASS(ComputeShader, Object, ClassId::ComputeShader, ClassNone)

    public:
        ComputeShader() = default;
        // A copy has the values and keywords; its variants compile again.
        ComputeShader(const ComputeShader& other);
        ~ComputeShader() override;

        // A loaded compute shader by name; null when none is.
        static ComputeShader* Find(StringView name);
        // The kernel of that name; -1 (and an error) when there is none (C# throws).
        i32 FindKernel(StringView name) const;
        HX_FUNCTION(Internal, Name = "KernelIndex") i32 KernelIndex(StringView name) const;
        HX_FUNCTION() bool HasKernel(StringView name) const;
        // Invocations per group of a kernel along x (0), y (1) or z (2); 0 for no kernel.
        HX_FUNCTION(Internal) u32 KernelGroupSize(i32 kernelIndex, i32 axis) const;
        void GetKernelThreadGroupSizes(i32 kernelIndex, u32& x, u32& y, u32& z) const;
        HX_FUNCTION(Internal) i32 KernelCount() const;
        // Whether the device runs compute shaders and the kernel compiles for it.
        HX_FUNCTION() bool IsSupported(i32 kernelIndex);

        HX_FUNCTION() void SetFloat(StringView name, f32 value) { SetFloat(engine::PropertyToID(name), value); }
        HX_FUNCTION() void SetFloat(i32 nameID, f32 value) { values_.SetFloat(nameID, value); }
        HX_FUNCTION() void SetInt(StringView name, i32 value) { SetInt(engine::PropertyToID(name), value); }
        HX_FUNCTION() void SetInt(i32 nameID, i32 value) { values_.SetInt(nameID, value); }
        HX_FUNCTION() void SetBool(StringView name, bool value) { SetInt(name, value ? 1 : 0); }
        HX_FUNCTION() void SetBool(i32 nameID, bool value) { SetInt(nameID, value ? 1 : 0); }
        HX_FUNCTION() void SetVector(StringView name, Vector4 value) { SetVector(engine::PropertyToID(name), value); }
        HX_FUNCTION() void SetVector(i32 nameID, Vector4 value) { values_.SetVector(nameID, value); }
        HX_FUNCTION() void SetMatrix(StringView name, Matrix4x4 value) { SetMatrix(engine::PropertyToID(name), value); }
        HX_FUNCTION() void SetMatrix(i32 nameID, Matrix4x4 value) { values_.SetMatrix(nameID, value); }
        HX_FUNCTION() void SetFloats(StringView name, Span<const f32> values) { SetFloats(engine::PropertyToID(name), values); }
        HX_FUNCTION() void SetFloats(i32 nameID, Span<const f32> values) { values_.SetFloatArray(nameID, values); }
        HX_FUNCTION() void SetInts(StringView name, Span<const i32> values) { SetInts(engine::PropertyToID(name), values); }
        HX_FUNCTION() void SetInts(i32 nameID, Span<const i32> values);
        HX_FUNCTION() void SetVectorArray(StringView name, Span<const Vector4> values) { SetVectorArray(engine::PropertyToID(name), values); }
        HX_FUNCTION() void SetVectorArray(i32 nameID, Span<const Vector4> values) { values_.SetVectorArray(nameID, values); }
        HX_FUNCTION() void SetMatrixArray(StringView name, Span<const Matrix4x4> values) { SetMatrixArray(engine::PropertyToID(name), values); }
        HX_FUNCTION() void SetMatrixArray(i32 nameID, Span<const Matrix4x4> values) { values_.SetMatrixArray(nameID, values); }

        HX_FUNCTION() void SetTexture(i32 kernelIndex, StringView name, Texture* texture) { SetTexture(kernelIndex, engine::PropertyToID(name), texture, 0); }
        HX_FUNCTION() void SetTexture(i32 kernelIndex, i32 nameID, Texture* texture) { SetTexture(kernelIndex, nameID, texture, 0); }
        HX_FUNCTION() void SetTexture(i32 kernelIndex, StringView name, Texture* texture, i32 mipLevel)
        {
            SetTexture(kernelIndex, engine::PropertyToID(name), texture, mipLevel);
        }
        HX_FUNCTION() void SetTexture(i32 kernelIndex, i32 nameID, Texture* texture, i32 mipLevel);
        HX_FUNCTION() void SetTextureFromGlobal(i32 kernelIndex, StringView name, StringView globalTextureName)
        {
            SetTextureFromGlobal(kernelIndex, engine::PropertyToID(name), engine::PropertyToID(globalTextureName));
        }
        HX_FUNCTION() void SetTextureFromGlobal(i32 kernelIndex, i32 nameID, i32 globalTextureNameID);
        // A buffer by its id (0 for none); C# takes GraphicsBuffer and ComputeBuffer.
        HX_FUNCTION(Internal) void SetBufferId(i32 kernelIndex, i32 nameID, u64 buffer);
        void SetBuffer(i32 kernelIndex, i32 nameID, GraphicsBuffer* buffer) { SetBufferId(kernelIndex, nameID, buffer ? buffer->Id() : 0); }
        void SetBuffer(i32 kernelIndex, StringView name, GraphicsBuffer* buffer) { SetBuffer(kernelIndex, engine::PropertyToID(name), buffer); }

        // Runs the kernel at once with the values set: groups of its thread group size, x * y * z of them.
        HX_FUNCTION() void Dispatch(i32 kernelIndex, i32 threadGroupsX, i32 threadGroupsY, i32 threadGroupsZ);
        // The group counts from three u32 at a byte offset of an IndirectArguments buffer.
        HX_FUNCTION(Internal) void DispatchIndirectId(i32 kernelIndex, u64 argsBuffer, u32 argsOffset);
        void DispatchIndirect(i32 kernelIndex, GraphicsBuffer* argsBuffer, u32 argsOffset = 0) { DispatchIndirectId(kernelIndex, argsBuffer ? argsBuffer->Id() : 0, argsOffset); }

        // ---------------------------------------------------------------------------------------------- keywords
        HX_FUNCTION() void EnableKeyword(StringView keyword);
        HX_FUNCTION() void DisableKeyword(StringView keyword);
        HX_FUNCTION() bool IsKeywordEnabled(StringView keyword) const;
        HX_FUNCTION() void EnableKeyword(const LocalKeyword& keyword) { EnableKeyword(keyword.name()); }
        HX_FUNCTION() void DisableKeyword(const LocalKeyword& keyword) { DisableKeyword(keyword.name()); }
        HX_FUNCTION() bool IsKeywordEnabled(const LocalKeyword& keyword) const { return IsKeywordEnabled(keyword.name()); }
        HX_FUNCTION() void SetKeyword(const LocalKeyword& keyword, bool value) { value ? EnableKeyword(keyword) : DisableKeyword(keyword); }
        HX_PROPERTY() Array<String> shaderKeywords() const { return keywords_; }
        void SetShaderKeywords(Span<const String> keywords);
        HX_PROPERTY(ReadOnly) Array<LocalKeyword> enabledKeywords() const;
        HX_PROPERTY(ReadOnly) LocalKeywordSpace keywordSpace() const { return LocalKeywordSpace{GetEntityId().value}; }
        // Every keyword of the definition: bool keywords by their name, enum keywords as name_member.
        const Array<ShaderKeywordInfo>& KeywordSpace() const;

        // ---------------------------------------------------------------------------------------------- native
        // The compute shader of definition `index` of a program (the asset loader).
        static ComputeShader* Create(std::shared_ptr<ShaderProgram> program, u32 index);
        const shadercompiler::ComputeDescription* Description() const;
        // The target compute shaders compile to on the device; false when it runs none.
        static bool ComputeTarget(shadercompiler::ShaderTarget& out);
        // The variant of a kernel for the enabled keywords; null when the kernel is not there or it does not compile
        // (its error is logged once).
        const ComputeVariant* GetVariant(i32 kernelIndex, shadercompiler::ShaderTarget target);
        // Records the kernel's dispatch with the values set into a command list (direct when `indirect` is null);
        // false, and nothing recorded, when it cannot run (an error says why).
        bool Record(rhi::CommandList& cl, i32 kernelIndex, u32 x, u32 y, u32 z, GraphicsBuffer* indirect = nullptr, u32 argsOffset = 0);
        // Forgets the compiled variants and their device objects.
        void ClearVariants();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Name", name_);
        }

    private:
        struct KernelResources
        {
            PropertySheet sheet;
            // Mip levels of storage textures by property id.
            HashMap<i32, i32> mips;
        };
        KernelResources* Kernel(i32 kernelIndex);

        std::shared_ptr<ShaderProgram> program_;
        u32 index_ = 0;
        PropertySheet values_;
        Array<KernelResources> kernels_ = Array<KernelResources>();
        Array<String> keywords_ = Array<String>();
        HashMap<u64, ComputeVariant*> variants_;
        mutable Array<ShaderKeywordInfo> keywordSpace_ = Array<ShaderKeywordInfo>();
        mutable bool keywordSpaceMade_ = false;
    };
}

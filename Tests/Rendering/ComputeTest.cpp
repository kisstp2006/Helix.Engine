#include <Support/Devices.h>
#include <Support/TestShaders.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Rendering/GraphicsBuffer.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>
#include <Helix/Engine/Shader/ComputeShader.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Texture/RenderTexture.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    struct ErrorCount
    {
        u32 errors = 0;
        ErrorCount() { AddLogSink(&Sink, this); }
        ~ErrorCount() { RemoveLogSink(&Sink, this); }
        static void Sink(const LogMessage& m, void* user)
        {
            if (m.level >= LogLevel::Error)
                static_cast<ErrorCount*>(user)->errors++;
        }
    };

    bool Near(f32 a, f32 b) { return std::fabs(a - b) < 1e-4f; }

    // A unit quad around the origin, as two triangles.
    Mesh* Quad()
    {
        Mesh* m = NewObject<Mesh>();
        const Vector3 v[] = {{-0.5f, -0.5f, 0}, {-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}};
        const i32 t[] = {0, 1, 2, 0, 2, 3};
        m->SetVertices(Span<const Vector3>(v, 4));
        m->SetTriangles(Span<const i32>(t, 6));
        return m;
    }

    // Texels of a render texture's color, rows as the device has them.
    Array<u32> Texels(rhi::Device& device, RenderTexture* rt)
    {
        Array<u32> texels;
        const render::GpuRenderTarget* g = render::RenderTargetOf(*rt);
        if (!g)
            return texels;
        texels.Resize(usize(g->width) * g->height);
        if (!device.ReadTexture(g->color, 0, texels.Data(), texels.Size() * 4))
            texels.Clear();
        return texels;
    }
}

HX_TEST(Compute, ShadersAndBuffersWithoutADevice)
{
    TestShaderFolder shaders;
    ComputeShader* cs = ComputeShader::Find("Tests/Fill");
    HX_REQUIRE(cs != nullptr);
    HX_CHECK(cs->KernelCount() == 2 && cs->KernelIndex("Scale") == 0 && cs->KernelIndex("Paint") == 1 && cs->HasKernel("Paint") && !cs->HasKernel("Nope"));
    u32 x, y, z;
    cs->GetKernelThreadGroupSizes(0, x, y, z);
    HX_CHECK(x == 64 && y == 1 && z == 1);
    cs->GetKernelThreadGroupSizes(1, x, y, z);
    HX_CHECK(x == 8 && y == 8 && z == 1);
    {
        ErrorCount e;
        HX_CHECK(cs->FindKernel("Nope") == -1 && e.errors == 1);
        // Without a device nothing runs (and nothing breaks).
        HX_CHECK(!cs->IsSupported(0));
        cs->Dispatch(0, 1, 1, 1);
        cs->SetTexture(5, "image", nullptr);
        HX_CHECK(e.errors == 2);
    }
    // Keywords: the space of the definition, enabled ones by name.
    HX_CHECK(cs->keywordSpace().keywordCount() == 1 && cs->keywordSpace().keywordNames()[0] == StringView("Doubled"));
    LocalKeyword doubled(cs, "Doubled");
    HX_CHECK(doubled.isValid() && !cs->IsKeywordEnabled(doubled));
    cs->EnableKeyword(doubled);
    HX_CHECK(cs->IsKeywordEnabled("Doubled") && cs->enabledKeywords().Size() == 1 && cs->shaderKeywords().Size() == 1);
    cs->SetKeyword(doubled, false);
    HX_CHECK(cs->enabledKeywords().Empty());

    // Buffers: the reference's rules, data kept without a device.
    {
        ErrorCount e;
        HX_CHECK(GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, 4, 3) == nullptr);
        HX_CHECK(GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, 0, 4) == nullptr);
        HX_CHECK(e.errors == 2);
    }
    GraphicsBuffer* b = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, 4, 4);
    HX_REQUIRE(b != nullptr);
    HX_CHECK(b->count() == 4 && b->stride() == 4 && b->sizeInBytes() == 16 && GraphicsBuffer::Find(b->Id()) == b);
    const f32 in[] = {1, 2, 3, 4};
    HX_CHECK(b->SetData(in, 16, 0));
    f32 out[2] = {};
    HX_CHECK(b->GetData(out, 8, 8) && out[0] == 3 && out[1] == 4);
    {
        ErrorCount e;
        HX_CHECK(!b->SetData(in, 16, 4) && e.errors == 1);
    }
    u64 id = b->Id();
    GraphicsBuffer::Release(b);
    HX_CHECK(GraphicsBuffer::Find(id) == nullptr);
}

HX_TEST(Compute, KernelsRunOnEveryComputeDevice)
{
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device) {
        if (!device.Caps().compute || !device.Caps().storageBuffers)
        {
            std::printf("      no compute shaders, skipped\n");
            return;
        }
        TestShaderFolder shaders;
        Graphics::SetDevice(&device);
        ComputeShader* cs = ComputeShader::Find("Tests/Fill");
        HX_REQUIRE(cs != nullptr);
        shadercompiler::ShaderTarget target;
        HX_REQUIRE(ComputeShader::ComputeTarget(target));
        HX_CHECK(cs->IsSupported(0) && cs->IsSupported(1));
        ran++;

        constexpr u32 n = 100;
        f32 values[n];
        for (u32 i = 0; i < n; i++)
            values[i] = f32(i);
        GraphicsBuffer* input = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, n, 4);
        GraphicsBuffer* output = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, n, 4);
        HX_REQUIRE(input && output && input->SetData(values, sizeof(values), 0));
        // Values by name into the block (an int, a uint from SetInt, a vector), buffers per kernel.
        cs->SetFloat("scale", 2);
        cs->SetInt("offset", 3);
        cs->SetInt("count", i32(n));
        cs->SetVector("bias", Vector4(0.5f, 1, 0, 1));
        cs->SetBuffer(0, "input", input);
        cs->SetBuffer(0, "output", output);
        auto check = [&](f32 scale, f32 add, f32 factor, const char* what) {
            f32 got[n] = {};
            HX_REQUIRE(output->GetData(got, sizeof(got), 0));
            bool ok = true;
            for (u32 i = 0; i < n; i++)
                ok = ok && Near(got[i], (f32(i) * scale + add) * factor);
            if (!ok)
                std::printf("      %s: %f %f %f\n", what, got[0], got[1], got[99]);
            HX_CHECK(ok);
        };
        cs->Dispatch(0, 2, 1, 1);
        check(2, 3.5f, 1, "dispatch");
        // A keyword picks the variant.
        cs->EnableKeyword("Doubled");
        cs->Dispatch(0, 2, 1, 1);
        check(2, 3.5f, 2, "keyword");
        cs->DisableKeyword("Doubled");
        // What a kernel has none of comes from the globals.
        GraphicsBuffer* ones = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, n, 4);
        f32 one[n];
        for (f32& v : one)
            v = 1;
        HX_REQUIRE(ones && ones->SetData(one, sizeof(one), 0));
        cs->SetBuffer(0, "input", nullptr);
        Shader::SetGlobalBufferId(Shader::PropertyToID("input"), ones->Id());
        Shader::SetGlobalFloat("scale", 9);
        cs->Dispatch(0, 2, 1, 1);
        {
            f32 got[n] = {};
            HX_REQUIRE(output->GetData(got, sizeof(got), 0));
            // The shader's own scale wins over the global one.
            HX_CHECK(Near(got[0], 5.5f) && Near(got[99], 5.5f));
        }
        Shader::SetGlobalBufferId(Shader::PropertyToID("input"), 0);
        {
            // Nothing set anywhere: an error and no dispatch.
            ErrorCount e;
            cs->Dispatch(0, 2, 1, 1);
            HX_CHECK(e.errors == 1);
        }
        cs->SetBuffer(0, "input", input);

        // A storage image: a render texture with random write.
        RenderTexture* rt = RenderTexture::New(16, 16, 0, RenderTextureFormat::ARGB32, RenderTextureReadWrite::Linear);
        rt->SetEnableRandomWrite(true);
        HX_REQUIRE(rt->Create());
        cs->SetVector("bias", Vector4(1, 0, 0, 1));
        cs->SetTexture(1, "image", rt);
        cs->Dispatch(1, 2, 2, 1);
        Array<u32> texels = Texels(device, rt);
        HX_REQUIRE(texels.Size() == 256);
        HX_CHECK(texels[0] == 0xFF0000FFu && texels[255] == 0xFF0000FFu);

        // A command buffer: data, values and dispatches in its order, and an indirect dispatch.
        CommandBuffer cb;
        f32 twos[n];
        for (f32& v : twos)
            v = 2;
        cb.SetBufferData(input->Id(), twos, sizeof(twos), 0);
        cb.SetComputeFloatParam(cs, Shader::PropertyToID("scale"), 10);
        cb.SetComputeVectorParam(cs, Shader::PropertyToID("bias"), Vector4(0, 0, 0, 0));
        cb.SetComputeBufferParam(cs, 0, Shader::PropertyToID("output"), output);
        cb.DispatchCompute(cs, 0, 2, 1, 1);
        Graphics::ExecuteCommandBuffer(cb);
        check(0, 23, 1, "command buffer");
        GraphicsBuffer* args = GraphicsBuffer::Create(GraphicsBufferTarget::IndirectArguments, 0, 3, 4);
        const u32 groups[] = {2, 1, 1};
        HX_REQUIRE(args && args->SetData(groups, sizeof(groups), 0));
        CommandBuffer indirect;
        indirect.SetComputeFloatParam(cs, Shader::PropertyToID("scale"), 1);
        indirect.DispatchCompute(cs, 0, args->Id(), 0);
        Graphics::ExecuteCommandBuffer(indirect);
        check(0, 5, 1, "indirect");

        // Draws read buffers by name where the device's draw shaders take storage buffers.
        if (Graphics::ShaderTarget() == shadercompiler::ShaderTarget::Dxbc || Graphics::ShaderTarget() == shadercompiler::ShaderTarget::Glsl450)
        {
            const Vector4 green[] = {{1, 0, 0, 1}, {0, 1, 0, 1}};
            const Vector4 blue[] = {{1, 0, 0, 1}, {0, 0, 1, 1}};
            GraphicsBuffer* g = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, 2, 16);
            GraphicsBuffer* bl = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, 2, 16);
            HX_REQUIRE(g && bl && g->SetData(green, sizeof(green), 0) && bl->SetData(blue, sizeof(blue), 0));
            RenderTexture* into = RenderTexture::New(8, 8, 0, RenderTextureFormat::ARGB32, RenderTextureReadWrite::Linear);
            HX_REQUIRE(into->Create());
            Material* m = Material::Create(Shader::Find("Tests/BufferColor"));
            Mesh* quad = Quad();
            MaterialPropertyBlock block;
            block.SetBuffer("colors", bl->Id());
            // The global, the property block over the material, the material over the global.
            for (i32 round = 0; round < 3; round++)
            {
                if (round == 2)
                    m->SetBufferId(Shader::PropertyToID("colors"), bl->Id());
                CommandBuffer draw;
                draw.SetRenderTarget(RenderTargetIdentifier(into));
                draw.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
                draw.SetViewProjectionMatrices(Matrix4x4::identity, Matrix4x4::identity);
                draw.SetGlobalBuffer(Shader::PropertyToID("colors"), g);
                draw.DrawMesh(quad, Matrix4x4::Scale(Vector3(2, 2, 1)), m, 0, 0, round == 1 ? &block : nullptr);
                Graphics::ExecuteCommandBuffer(draw);
                Array<u32> pixels = Texels(device, into);
                HX_REQUIRE(pixels.Size() == 64);
                u32 want = round == 0 ? 0xFF00FF00u : 0xFFFF0000u;
                HX_CHECK(round != 2 || m->GetBufferId(Shader::PropertyToID("colors")) == bl->Id());
                if (pixels[0] != want || pixels[63] != want)
                    std::printf("      draw %d: %08x %08x\n", round, pixels[0], pixels[63]);
                HX_CHECK(pixels[0] == want && pixels[63] == want);
            }
            Shader::Globals().SetBuffer(Shader::PropertyToID("colors"), 0);
            GraphicsBuffer::Release(g);
            GraphicsBuffer::Release(bl);
        }
        else
            std::printf("      draws take no storage buffers here, the draw part skipped\n");

        GraphicsBuffer::Release(args);
        GraphicsBuffer::Release(ones);
        GraphicsBuffer::Release(input);
        GraphicsBuffer::Release(output);
        cs->ClearVariants();
        Graphics::ReleaseResources();
    });
    std::printf("    %u compute devices\n", ran);
}

HX_TEST(Rendering, BindlessMembersHoldTheTexturesIndices)
{
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device) {
        if (!device.Caps().bindlessTextures || device.Caps().backend != rhi::Backend::OpenGL)
        {
            std::printf("      no bindless textures in draws, skipped\n");
            return;
        }
        TestShaderFolder shaders;
        Graphics::SetDevice(&device);
        ran++;
        auto solid = [](Color32 c) {
            Texture2D* t = Texture2D::Create(2, 2, TextureFormat::RGBA32, false, true);
            const Color32 texels[] = {c, c, c, c};
            t->SetPixels32(Span<const Color32>(texels, 4));
            t->SetFilterMode(FilterMode::Point);
            t->Apply(false);
            return t;
        };
        Texture2D* red = solid(Color32(255, 0, 0, 255));
        Texture2D* green = solid(Color32(0, 255, 0, 255));
        RenderTexture* into = RenderTexture::New(8, 8, 0, RenderTextureFormat::ARGB32, RenderTextureReadWrite::Linear);
        HX_REQUIRE(into->Create());
        Material* m = Material::Create(Shader::Find("Tests/BindlessColor"));
        Material* plain = Material::Create(Shader::Find("Tests/BindlessColor"));
        m->SetTexture("baseMap", red);
        Mesh* quad = Quad();
        MaterialPropertyBlock block;
        block.SetTexture("baseMap", green);
        // The material's texture, the property block's over it, the default (white) of a material without one, and the
        // material's again after its texture changed (a new index).
        const u32 want[] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFFFFFFu, 0xFF00FF00u};
        for (i32 round = 0; round < 4; round++)
        {
            if (round == 3)
                m->SetTexture("baseMap", green);
            CommandBuffer draw;
            draw.SetRenderTarget(RenderTargetIdentifier(into));
            draw.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
            draw.SetViewProjectionMatrices(Matrix4x4::identity, Matrix4x4::identity);
            draw.DrawMesh(quad, Matrix4x4::Scale(Vector3(2, 2, 1)), round == 2 ? plain : m, 0, 0, round == 1 ? &block : nullptr);
            Graphics::ExecuteCommandBuffer(draw);
            Array<u32> pixels = Texels(device, into);
            HX_REQUIRE(pixels.Size() == 64);
            if (pixels[0] != want[round] || pixels[63] != want[round])
                std::printf("      round %d: %08x %08x\n", round, pixels[0], pixels[63]);
            HX_CHECK(pixels[0] == want[round] && pixels[63] == want[round]);
        }
        HX_CHECK(render::BindlessIndexOf(*red) != ~0u && render::BindlessIndexOf(*red) != render::BindlessIndexOf(*green));
        Graphics::ReleaseResources();
    });
    std::printf("    %u bindless devices\n", ran);
}

HX_TEST(Camera, PhysicalPropertiesDriveTheProjection)
{
    GameObject* g = GameObject::Create("Lens");
    Camera* c = g->AddComponent<Camera>();
    Camera::SetDisplaySize(300, 200);
    // The defaults of the reference: a 36 x 24 sensor, 50 mm, f/16, 1/200 s, ISO 200.
    HX_CHECK(!c->usePhysicalProperties() && c->sensorSize() == Vector2(36, 24) && c->focalLength() == 50 && c->aperture() == 16 && c->iso() == 200);
    c->SetUsePhysicalProperties(true);
    // 50 mm over a 24 mm high sensor: 2 atan(12 / 50).
    HX_CHECK(std::fabs(c->fieldOfView() - 2.0f * std::atan(12.0f / 50.0f) * mathf::Rad2Deg) < 1e-3f);
    c->SetFieldOfView(90);
    HX_CHECK(std::fabs(c->focalLength() - 12.0f) < 1e-3f);
    c->SetFocalLength(24);
    HX_CHECK(std::fabs(c->fieldOfView() - 2.0f * std::atan(0.5f) * mathf::Rad2Deg) < 1e-3f);
    // The lens shift moves the projection's centre by twice the shift in clip space.
    Matrix4x4 centred = c->projectionMatrix();
    c->SetLensShift(Vector2(0.25f, -0.1f));
    Matrix4x4 shifted = c->projectionMatrix();
    HX_CHECK(centred.m02 == 0 && std::fabs(shifted.m02 - 0.5f) < 1e-6f && std::fabs(shifted.m12 + 0.2f) < 1e-6f);
    // Clamped like the reference's.
    c->SetAperture(100);
    c->SetBladeCount(1);
    HX_CHECK(c->aperture() == 32 && c->bladeCount() == 3);

    // The file form keeps them.
    c->SetIso(800);
    c->SetShutterSpeed(0.01f);
    c->SetAperture(2.8f);
    c->SetGateFit(CameraGateFitMode::Fill);
    Scene start = SceneManager::GetActiveScene();
    Scene written = SceneManager::CreateScene("LensWritten");
    SceneManager::SetActiveScene(written);
    GameObject* saved = static_cast<GameObject*>(Object::Instantiate(g));
    SceneManager::MoveGameObjectToScene(saved, written);
    String yaml, error;
    HX_REQUIRE(WriteSceneYaml(written, yaml, error));
    Scene read = SceneManager::CreateScene("LensRead");
    Array<String> errors;
    HX_REQUIRE(ReadSceneFile(StringView(yaml), read, errors));
    Array<GameObject*> roots = read.GetRootGameObjects();
    Camera* back = roots.Size() == 1 ? roots[0]->GetComponent<Camera>() : nullptr;
    HX_CHECK(StringView(yaml).Contains("m_projectionMatrixMode: 2") && StringView(yaml).Contains("m_Aperture"));
    for (const String& e : errors)
        std::printf("      %s\n", e.CStr());
    HX_CHECK(back && back->usePhysicalProperties() && back->iso() == 800 && std::fabs(back->shutterSpeed() - 0.01f) < 1e-6f &&
             std::fabs(back->aperture() - 2.8f) < 1e-6f && back->focalLength() == 24 && back->lensShift() == Vector2(0.25f, -0.1f) &&
             back->gateFit() == CameraGateFitMode::Fill);
    SceneManager::SetActiveScene(start);
    SceneManager::UnloadSceneAsync(written);
    SceneManager::UnloadSceneAsync(read);
    SceneManager::ProcessSceneLoads();
    DestroyImmediate(g);
}

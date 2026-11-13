#include <Support/TestShaders.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/Testing/Test.h>


using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;
using shadercompiler::KeywordValue;
using shadercompiler::ShaderTarget;

namespace
{
    using ShaderFolder = TestShaderFolder;

    struct LogCapture
    {
        Array<String> lines = Array<String>();
        LogCapture() { AddLogSink(&Sink, this); }
        ~LogCapture() { RemoveLogSink(&Sink, this); }
        static void Sink(const LogMessage& m, void* user) { static_cast<LogCapture*>(user)->lines.Push(String(m.text)); }
        bool Has(StringView part) const
        {
            for (const String& l : lines)
                if (StringView(l).Contains(part))
                    return true;
            return false;
        }
    };
}

HX_TEST(Shader, AssembliesLoadAsShaders)
{
    ShaderFolder f;
    HX_REQUIRE(f.guid.IsValid());
    Array<Object*> all;
    LoadAllAssets(f.guid, all);
    HX_CHECK(all.Size() == 7);
    Shader* s = Shader::Find("Tests/Surface");
    HX_REQUIRE(s != nullptr);
    HX_CHECK(s->isSupported() && s->IsPersistent() && s->renderQueue() == 3005 && s->passCount() == 2 && s->subshaderCount() == 1);
    HX_CHECK(s->FindPassTagValue(1, "LightMode") == StringView("ShadowCaster") && s->FindSubshaderTagValue(0, "RenderType") == StringView("Transparent"));
    i32 tint = s->FindPropertyIndex("tint"), smooth = s->FindPropertyIndex("smoothness"), base = s->FindPropertyIndex("baseMap");
    HX_REQUIRE(tint >= 0 && smooth >= 0 && base >= 0);
    HX_CHECK(s->GetPropertyType(tint) == ShaderPropertyType::Color && s->GetPropertyType(smooth) == ShaderPropertyType::Range &&
             s->GetPropertyType(base) == ShaderPropertyType::Texture && s->GetPropertyType(s->FindPropertyIndex("layers")) == ShaderPropertyType::Int);
    HX_CHECK(s->GetPropertyFlags(tint) == ShaderPropertyFlags::MainColor && s->GetPropertyRangeLimits(smooth) == Vector2(0, 1));
    HX_CHECK(s->GetPropertyDefaultVectorValue(tint) == Vector4(1, 0.5f, 0.25f, 1) && s->GetPropertyTextureDefaultName(base) == StringView("white"));
    HX_CHECK(s->GetPropertyNameId(tint) == Shader::PropertyToID("tint") && s->GetPropertyDescription(smooth) == StringView("Smoothness"));
    Array<String> keywords;
    s->GetLocalKeywords(keywords);
    HX_CHECK(keywords.Size() == 4 && keywords[0] == StringView("Glow") && keywords[3] == StringView("Detail_High"));
    HX_CHECK(Shader::Find("Tests/Nothing") == nullptr && Shader::ErrorShader() && Shader::ErrorShader()->passCount() == 1);
}

HX_TEST(Shader, VariantsCompileOnDemand)
{
    ShaderFolder f;
    LoadAsset(f.guid);
    Shader* s = Shader::Find("Tests/Surface");
    HX_REQUIRE(s != nullptr);
    const String enabled[] = {String("Glow"), String("Detail_High")};
    Array<KeywordValue> values;
    s->KeywordValues(Span<const String>(enabled, 2), values);
    HX_REQUIRE(values.Size() == 3);
    HX_CHECK(values[0].value == 1 && values[1].value == 2 && values[2].value == 0);
    u32 forward = u32(s->PassIndex(0));
    for (ShaderTarget target : {ShaderTarget::Glsl330, ShaderTarget::Essl300, ShaderTarget::Dxbc})
    {
        const ShaderVariant* v = s->GetVariant(forward, Span<const KeywordValue>(values.Data(), values.Size()), target);
        HX_REQUIRE(v != nullptr);
        HX_CHECK(v->ok && !v->vertex.code.Empty() && !v->fragment.code.Empty() && v->target == target);
        // The material block and the two textures of the pass; the engine's blocks at 9 and 10.
        const shadercompiler::PropertyBinding* t = v->layout.Find(u32(s->FindPropertyIndex("tint")), shadercompiler::PropertyTarget::Member);
        HX_CHECK(t && t->offset == 0 && v->layout.blocks.Size() == 1 && !v->layout.blocks[0].propertiesOnly);
        bool camera = false, draw = false;
        for (const shadercompiler::ReflectedBlock& b : v->reflection.blocks)
            camera |= b.slot == 9, draw |= b.slot == 10;
        HX_CHECK(camera && draw);
        // The same request comes from the cache.
        HX_CHECK(s->GetVariant(forward, Span<const KeywordValue>(values.Data(), values.Size()), target) == v);
    }
    // Another keyword value is another variant.
    Array<KeywordValue> plain;
    s->KeywordValues({}, plain);
    HX_CHECK(s->GetVariant(forward, Span<const KeywordValue>(plain.Data(), plain.Size()), ShaderTarget::Glsl330) !=
             s->GetVariant(forward, Span<const KeywordValue>(values.Data(), values.Size()), ShaderTarget::Glsl330));
    // Global keywords count for keywords the shader declares global.
    Shader::EnableKeyword("Fog");
    s->KeywordValues({}, plain);
    HX_CHECK(plain[2].value == 1 && Shader::IsKeywordEnabled("Fog"));
    Shader::DisableKeyword("Fog");
}

HX_TEST(Shader, BrokenShadersDrawWithTheErrorShader)
{
    ShaderFolder f;
    LoadAsset(f.guid);
    Shader* broken = Shader::Find("Tests/Broken");
    HX_REQUIRE(broken != nullptr);
    LogCapture log;
    const ShaderVariant* v = broken->GetVariant(0, {}, ShaderTarget::Glsl450);
    HX_REQUIRE(v != nullptr);
    const ShaderVariant* error = Shader::ErrorShader()->GetVariant(0, {}, ShaderTarget::Glsl450);
    HX_CHECK(v == error && v->ok && StringView(v->fragment.Text()).Contains("vec4(1.0, 0.0, 1.0, 1.0)"));
    // Logged once, with the file and line of the C# source.
    HX_CHECK(log.Has("Tests/Broken") && log.Has("TestShaders.cs:"));
    usize lines = log.lines.Size();
    broken->GetVariant(0, {}, ShaderTarget::Glsl450);
    HX_CHECK(log.lines.Size() == lines);
}

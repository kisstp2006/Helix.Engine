#include <Support/TestShaders.h>

#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Asset/Resources.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Testing/Test.h>

#include <cstring>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;
using shadercompiler::ReflectedBlock;
using shadercompiler::ReflectedMember;
using shadercompiler::ShaderTarget;

namespace
{
    using Shaders = TestShaderFolder;

    f32 FloatAt(const MaterialPassData& d, const ShaderVariant& v, StringView member)
    {
        for (const ReflectedBlock& b : v.reflection.blocks)
            if (b.slot == 0)
                if (const ReflectedMember* m = b.Find(member))
                {
                    f32 f;
                    std::memcpy(&f, d.blocks[0].Data() + m->offset, 4);
                    return f;
                }
        return -1000;
    }

    i32 IntAt(const MaterialPassData& d, const ShaderVariant& v, StringView member)
    {
        for (const ReflectedBlock& b : v.reflection.blocks)
            if (b.slot == 0)
                if (const ReflectedMember* m = b.Find(member))
                {
                    i32 i;
                    std::memcpy(&i, d.blocks[0].Data() + m->offset, 4);
                    return i;
                }
        return -1000;
    }
}

HX_TEST(Material, DefaultsValuesAndSettings)
{
    Shaders s;
    HX_REQUIRE(s.surface && s.plain);
    Material* m = Material::Create(s.surface);
    HX_REQUIRE(m != nullptr);
    HX_CHECK(m->shader() == s.surface && m->GetColor("tint") == Color(1, 0.5f, 0.25f, 1) && m->GetFloat("smoothness") == 0.5f && m->GetInteger("layers") == 2);
    HX_CHECK(m->HasProperty("tint") && !m->HasProperty("weights") && m->HasFloat("smoothness") && m->HasInteger("layers") && m->HasTexture("baseMap"));
    HX_CHECK(m->color() == m->GetColor("tint") && m->mainTexture() == nullptr && m->mainTextureScale() == Vector2(1, 1));
    m->SetMainTextureOffset(Vector2(0.5f, 0.25f));
    HX_CHECK(m->GetTextureOffset("baseMap") == Vector2(0.5f, 0.25f));
    HX_CHECK(m->renderQueue() == 3005 && m->rawRenderQueue() == -1);
    m->SetRenderQueue(2450);
    HX_CHECK(m->renderQueue() == 2450);
    m->SetRenderQueue(-1);
    HX_CHECK(m->renderQueue() == 3005 && m->passCount() == 2 && m->GetPassName(1) == StringView("Shadow") && m->FindPass("shadow") == 1);
    m->SetShaderPassEnabled("ShadowCaster", false);
    HX_CHECK(!m->GetShaderPassEnabled("shadowcaster") && m->GetShaderPassEnabled("ForwardBase"));
    HX_CHECK(m->GetTag("RenderType", false) == StringView("Transparent") && m->GetTag("Nothing", false, "x") == StringView("x"));
    m->SetOverrideTag("RenderType", "Opaque");
    HX_CHECK(m->GetTag("RenderType", false) == StringView("Opaque"));
    // Keywords: one member of an enum keyword at a time; unknown names are kept as invalid ones.
    m->EnableKeyword("Detail_Low");
    m->EnableKeyword("Detail_High");
    m->EnableKeyword("Unknown");
    HX_CHECK(!m->IsKeywordEnabled("Detail_Low") && m->IsKeywordEnabled("Detail_High") && m->shaderKeywords().Size() == 2);
    Array<String> enabled;
    m->GetEnabledKeywords(enabled);
    HX_CHECK(enabled.Size() == 1 && enabled[0] == StringView("Detail_High"));
    // Another shader: values both have stay, the new ones get defaults.
    m->SetColor("tint", Color(0.25f, 0.25f, 0.25f, 1));
    i32 crc = m->ComputeCRC();
    m->SetShader(s.plain);
    HX_CHECK(m->GetColor("tint") == Color(0.25f, 0.25f, 0.25f, 1) && m->GetFloat("strength") == 3.0f && m->GetFloat("smoothness") == 0.5f);
    HX_CHECK(m->ComputeCRC() != crc && m->renderQueue() == 2000);
    // Copies.
    Material* copy = Material::Create(*m);
    HX_CHECK(copy && copy != m && copy->shader() == s.plain && copy->GetFloat("strength") == 3.0f);
    Material* other = Material::Create(s.surface);
    other->CopyMatchingPropertiesFromMaterial(*m);
    HX_CHECK(other->GetColor("tint") == Color(0.25f, 0.25f, 0.25f, 1) && !other->HasFloat("strength"));
    other->Lerp(*m, *Material::Create(s.surface), 0.5f);
    HX_CHECK(other->GetColor("tint").r > 0.25f && other->GetColor("tint").r < 1.0f);
    HX_CHECK(Material::Create(static_cast<Shader*>(nullptr)) == nullptr);
}

HX_TEST(Material, BlocksAndTexturesForTheRenderer)
{
    Shaders s;
    Material* m = Material::Create(s.surface);
    m->SetColor("tint", Color(0.5f, 0.25f, 0.125f, 1));
    m->SetFloat("smoothness", 0.75f);
    m->SetInteger("layers", 3);
    m->SetTextureScale("baseMap", Vector2(2, 3));
    const f32 weights[] = {1, 2, 3, 4};
    m->SetFloatArray("weights", Span<const f32>(weights, 4));
    u32 forward = u32(s.surface->PassIndex(0));
    for (ShaderTarget target : {ShaderTarget::Glsl330, ShaderTarget::Dxbc})
    {
        const MaterialPassData* d = m->GetPassData(forward, target);
        HX_REQUIRE(d && d->variant && d->variant->ok && d->blocks.Size() == 1);
        const ShaderVariant& v = *d->variant;
        HX_CHECK(FloatAt(*d, v, "tint") == 0.5f && FloatAt(*d, v, "tint.y") == -1000 && FloatAt(*d, v, "smoothness") == 0.75f && IntAt(*d, v, "layers") == 3);
        HX_CHECK(FloatAt(*d, v, "baseMap_ST") == 2.0f);
        // The array by name, every element on 16 bytes.
        HX_CHECK(FloatAt(*d, v, "weights[0]") == 1.0f && FloatAt(*d, v, "weights[3]") == 4.0f);
        // Textures: the default ones by their names (white for baseMap, gray without a name).
        HX_REQUIRE(d->textures.Size() == 2);
        for (const MaterialPassData::TextureSlot& t : d->textures)
            HX_CHECK(t.texture == (t.slot == 0 ? Texture2D::whiteTexture() : Texture2D::grayTexture())->GetEntityId());
    }
    // A change writes the blocks again; the data object stays.
    const MaterialPassData* before = m->GetPassData(forward, ShaderTarget::Glsl330);
    m->SetFloat("smoothness", 0.25f);
    const MaterialPassData* after = m->GetPassData(forward, ShaderTarget::Glsl330);
    HX_CHECK(after == before && FloatAt(*after, *after->variant, "smoothness") == 0.25f);
    // Keywords pick another variant.
    m->EnableKeyword("Glow");
    const MaterialPassData* glow = m->GetPassData(forward, ShaderTarget::Glsl330);
    HX_CHECK(glow != before && glow->variant != before->variant);
    // Members no material value sets come from the globals.
    Material* fresh = Material::Create(s.surface);
    const f32 global[] = {5, 6, 7, 8};
    Shader::Globals().SetFloatArray(PropertyToID("weights"), Span<const f32>(global, 4));
    const MaterialPassData* g = fresh->GetPassData(forward, ShaderTarget::Glsl330);
    HX_CHECK(FloatAt(*g, *g->variant, "weights[1]") == 6.0f);
    // A material of a broken shader draws with the error shader.
    Material* broken = Material::Create(Shader::Find("Tests/Broken"));
    const MaterialPassData* e = broken->GetPassData(0, ShaderTarget::Glsl330);
    HX_CHECK(e && e->variant == Shader::ErrorShader()->GetVariant(0, {}, ShaderTarget::Glsl330) && e->blocks.Empty());
}

HX_TEST(Material, MatFilesRoundTrip)
{
    Shaders s;
    String root = platform::JoinPath(platform::TempDirectory(), Format("helix-materials-{}", Guid::Generate().ToString()));
    platform::CreateDirectories(platform::JoinPath(root, "Resources"));
    // A texture asset for the mask (built in textures are no assets: references to them are not saved).
    Texture2D* t = Texture2D::Create(2, 2, TextureFormat::RGBA32, false);
    Array<u8> png;
    t->EncodeToPNG(png);
    DestroyImmediate(t, true);
    platform::WriteFile(platform::JoinPath(root, "Resources/mask.png"), Span<const u8>(png.Data(), png.Size()));
    FolderAssetSource folder(root);
    AddAssetSource(&folder);
    Texture2D* mask = Resources::Load<Texture2D>("mask");
    HX_REQUIRE(mask != nullptr);
    Material* m = Material::Create(s.surface);
    m->SetName("Shiny");
    m->SetColor("tint", Color(0.5f, 0.25f, 0.125f, 0.75f));
    m->SetFloat("smoothness", 0.8f);
    m->SetInteger("layers", 7);
    m->SetTexture("mask", mask);
    m->SetTextureOffset("baseMap", Vector2(0.5f, 0));
    m->EnableKeyword("Glow");
    m->EnableKeyword("Stale");
    m->SetRenderQueue(3100);
    m->SetOverrideTag("RenderType", "Cutout");
    m->SetShaderPassEnabled("ShadowCaster", false);
    m->SetEnableInstancing(true);
    Object* const objects[] = {m};
    Array<u8> file;
    String error;
    HX_REQUIRE(WriteAssetFile(Span<Object* const>(objects, 1), file, error));
    StringView text(reinterpret_cast<const char*>(file.Data()), file.Size());
    HX_CHECK(text.Contains("--- !h!21 &2100000") && text.Contains("m_Shader: {fileID: ") && text.Contains("type: 3}") && text.Contains("first: smoothness"));
    HX_CHECK(text.Contains("m_ValidKeywords:\n  - Glow") && text.Contains("m_InvalidKeywords:\n  - Stale"));
    platform::WriteFile(platform::JoinPath(root, "Resources/shiny.mat"), Span<const u8>(file.Data(), file.Size()));
    DestroyImmediate(m, true);
    RefreshAssets();
    Material* back = Resources::Load<Material>("shiny");
    HX_REQUIRE(back != nullptr);
    HX_CHECK(back->Name() == StringView("Shiny") && back->shader() == s.surface && back->GetColor("tint") == Color(0.5f, 0.25f, 0.125f, 0.75f));
    HX_CHECK(back->GetFloat("smoothness") == 0.8f && back->GetInteger("layers") == 7 && back->GetTexture("mask") == mask);
    HX_CHECK(back->GetTextureOffset("baseMap") == Vector2(0.5f, 0) && back->IsKeywordEnabled("Glow") && back->IsKeywordEnabled("Stale"));
    HX_CHECK(back->renderQueue() == 3100 && back->GetTag("RenderType", false) == StringView("Cutout") && !back->GetShaderPassEnabled("ShadowCaster"));
    HX_CHECK(back->enableInstancing() && back->GetPassData(u32(s.surface->PassIndex(0)), ShaderTarget::Glsl330) != nullptr);
    RemoveAssetSource(&folder);
    platform::RemoveFile(platform::JoinPath(root, "Resources/shiny.mat"));
    platform::RemoveFile(platform::JoinPath(root, "Resources/mask.png"));
    platform::RemoveEmptyDirectory(platform::JoinPath(root, "Resources"));
    platform::RemoveEmptyDirectory(root);
}

HX_TEST(Material, PropertyBlocks)
{
    MaterialPropertyBlock b;
    HX_CHECK(b.isEmpty());
    b.SetColor("tint", Color(1, 0, 0, 1));
    b.SetFloat(PropertyToID("strength"), 2);
    HX_CHECK(!b.isEmpty() && b.HasColor("tint") && b.GetFloat("strength") == 2 && !b.HasTexture("tint"));
    b.Clear();
    HX_CHECK(b.isEmpty() && b.GetFloat("strength") == 0);
}

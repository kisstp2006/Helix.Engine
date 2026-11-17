#include <Helix/Engine/Input/Cursor.h>
#include <Helix/Engine/Input/SystemClipboard.h>
#include <Helix/Engine/Math/Hash128.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;

HX_TEST(Cursor, ATextureIsTakenAsPixelsFromTheTopRowAndNullIsTheArrow)
{
    Cursor::Reset();
    Texture2D* t = Texture2D::Create(2, 2);
    HX_REQUIRE(t != nullptr);
    // The texture's row 0 is its bottom: the cursor's last row.
    t->SetPixel(0, 0, Color(1, 0, 0, 1));
    t->SetPixel(1, 1, Color(0, 0, 1, 0.5f));
    t->Apply();
    u64 before = Cursor::Current().version;
    Cursor::SetCursor(t, Vector2(1, 0), CursorMode::Auto);
    const Cursor::State& s = Cursor::Current();
    HX_CHECK(s.version == before + 1 && s.image && s.width == 2 && s.height == 2 && s.hotspotX == 1 && s.hotspotY == 0);
    HX_REQUIRE(s.pixels.Size() == 16);
    // Top right is (1, 1) of the texture, bottom left (0, 0).
    HX_CHECK(s.pixels[4 + 2] == 255 && s.pixels[4 + 3] == 128);
    HX_CHECK(s.pixels[8 + 0] == 255 && s.pixels[8 + 3] == 255);
    // The same again changes nothing; the texture may go.
    Cursor::SetCursor(t, Vector2(1, 0), CursorMode::ForceSoftware);
    HX_CHECK(Cursor::Current().version == before + 1);
    Object::DestroyImmediate(t);
    HX_CHECK(Cursor::Current().pixels.Size() == 16);
    Cursor::SetCursor(nullptr, Vector2(), CursorMode::Auto);
    HX_CHECK(!Cursor::Current().image && Cursor::Current().shape == platform::SystemCursor::Arrow && Cursor::Current().version == before + 2);
}

HX_TEST(Cursor, SystemShapesCountAsChangesOnlyWhenTheyChange)
{
    Cursor::Reset();
    u64 before = Cursor::Current().version;
    Cursor::SetSystemCursor(platform::SystemCursor::Arrow);
    HX_CHECK(Cursor::Current().version == before);
    Cursor::SetSystemCursor(platform::SystemCursor::Hand);
    Cursor::SetSystemCursor(platform::SystemCursor::Hand);
    HX_CHECK(Cursor::Current().version == before + 1 && Cursor::Current().shape == platform::SystemCursor::Hand);
    Cursor::Reset();
    HX_CHECK(Cursor::Current().shape == platform::SystemCursor::Arrow);
}

HX_TEST(SystemClipboard, TheTextIsTheHostsClipboardOrTheProcesss)
{
    SystemClipboard::SetHost({});
    SystemClipboard::SetText("own");
    HX_CHECK(SystemClipboard::Text() == StringView("own"));
    static String hosted;
    SystemClipboard::SetHost({[](void*) { return hosted; },
                                  [](void*, StringView t) {
                                      hosted = String(t);
                                      return true;
                                  },
                                  nullptr});
    SystemClipboard::SetText("to the host");
    HX_CHECK(hosted == StringView("to the host") && SystemClipboard::Text() == StringView("to the host"));
    hosted = String("changed elsewhere");
    HX_CHECK(SystemClipboard::Text() == StringView("changed elsewhere"));
    SystemClipboard::SetHost({});
}

HX_TEST(Hash128, ItsTextIsThirtyTwoHexDigitsFromTheLowestByte)
{
    Hash128 h(0x0123456789abcdefull, 0xfedcba9876543210ull);
    HX_CHECK(h.ToString() == StringView("efcdab89674523011032547698badcfe"));
    Hash128 back;
    HX_CHECK(Hash128::TryParse("EFCDAB89674523011032547698BADCFE", back) && back == h && back.isValid());
    HX_CHECK(!Hash128::TryParse("efcd", back) && !back.isValid());
    HX_CHECK(!Hash128::TryParse("zzcdab89674523011032547698badcfe", back));
}

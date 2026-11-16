#include <Helix/Engine/Input/InputFeed.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;

HX_TEST(InputFeed, EveryReaderSeesTheFrameAndTheNextFrameReplacesIt)
{
    InputFeed::Reset();
    InputEvent down;
    down.event.type = platform::EventType::MouseDown;
    down.event.mouse = {10, 20, platform::MouseButton::Left};
    down.time = 1.5;
    InputEvent text;
    text.event.type = platform::EventType::Text;
    text.event.text = {'a'};
    const InputEvent frame[] = {down, text};
    const u32 composing[] = {0x304B, 0x3093};
    u64 before = InputFeed::FrameNumber();
    InputFeed::SetFrame(Span<const InputEvent>(frame, 2), Span<const u32>(composing, 2));
    // Reading takes nothing away.
    for (int reader = 0; reader < 2; reader++)
    {
        Span<const InputEvent> events = InputFeed::Frame();
        HX_REQUIRE(events.Size() == 2);
        HX_CHECK(events[0].event.type == platform::EventType::MouseDown && events[0].event.mouse.y == 20 && events[0].time == 1.5);
        HX_CHECK(events[1].event.text.codepoint == 'a');
        HX_CHECK(InputFeed::Composition().Size() == 2 && InputFeed::Composition()[1] == 0x3093);
    }
    HX_CHECK(InputFeed::FrameNumber() == before + 1);
    InputFeed::SetFrame(Span<const InputEvent>(), Span<const u32>());
    HX_CHECK(InputFeed::Frame().Empty() && InputFeed::Composition().Empty() && InputFeed::FrameNumber() == before + 2);
}

HX_TEST(InputFeed, TextInputIsOnWhileAReaderAsksEachFrame)
{
    InputFeed::Reset();
    i32 x = 0, y = 0, h = 0;
    HX_CHECK(!InputFeed::TakeTextInputRequest(x, y, h));
    InputFeed::RequestTextInput(30, 40, 18);
    HX_CHECK(InputFeed::TakeTextInputRequest(x, y, h) && x == 30 && y == 40 && h == 18);
    // Asked for once: the next frame it is off again.
    HX_CHECK(!InputFeed::TakeTextInputRequest(x, y, h));
}

#include <Helix/Engine/Input/InputFeed.h>

#include <Helix/Core/Containers/Array.h>

namespace helix::engine
{
    namespace
    {
        struct Feed
        {
            Array<InputEvent> events;
            Array<u32> composition;
            bool asked = false;
            u64 frame = 0, resets = 0;
            i32 caretX = 0, caretY = 0, caretHeight = 0;
        };

        Feed& State()
        {
            static Feed* feed = new Feed();
            return *feed;
        }
    }

    void InputFeed::SetFrame(Span<const InputEvent> events, Span<const u32> composition)
    {
        Feed& f = State();
        f.frame++;
        f.events.Clear();
        for (const InputEvent& e : events)
            f.events.Push(e);
        f.composition.Clear();
        for (u32 c : composition)
            f.composition.Push(c);
    }

    Span<const InputEvent> InputFeed::Frame()
    {
        const Feed& f = State();
        return Span<const InputEvent>(f.events.Data(), f.events.Size());
    }

    u64 InputFeed::FrameNumber() { return State().frame; }

    Span<const u32> InputFeed::Composition()
    {
        const Feed& f = State();
        return Span<const u32>(f.composition.Data(), f.composition.Size());
    }

    void InputFeed::RequestTextInput(i32 caretX, i32 caretY, i32 caretHeight)
    {
        Feed& f = State();
        f.asked = true;
        f.caretX = caretX;
        f.caretY = caretY;
        f.caretHeight = caretHeight;
    }

    bool InputFeed::TakeTextInputRequest(i32& caretX, i32& caretY, i32& caretHeight)
    {
        Feed& f = State();
        bool asked = f.asked;
        caretX = f.caretX;
        caretY = f.caretY;
        caretHeight = f.caretHeight;
        f.asked = false;
        return asked;
    }

    void InputFeed::Reset()
    {
        Feed& f = State();
        f.events.Clear();
        f.composition.Clear();
        f.asked = false;
        f.resets++;
    }

    u64 InputFeed::ResetNumber() { return State().resets; }
}

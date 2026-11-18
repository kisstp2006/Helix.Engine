#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Platform/Window/Input.h>

// The input of a frame as the window gave it. The host (the player) hands the window's input events over before the
// frame's scripts run; readers (the UI, and the Input classes later) read them without taking them away, so every
// reader sees all of them and a game keeps the input its UI also handles. Positions are in the screen's pixels from
// the top left.
namespace helix::engine
{
    struct InputEvent
    {
        platform::Event event;
        // Real time since the start, in seconds, when the host took it.
        f64 time = 0;
    };

    class InputFeed
    {
    public:
        // The frame's events (in place of the last frame's) and the text an input method composes as of the last of
        // them (empty when nothing is composed).
        static void SetFrame(Span<const InputEvent> events, Span<const u32> composition);
        static Span<const InputEvent> Frame();
        static Span<const u32> Composition();
        // Counts the frames set (a reader that reads once a frame compares it with the last it read).
        static u64 FrameNumber();

        // A reader that takes text (a focused text field) asks for the input method each frame, with its caret (screen
        // pixels: the caret's top left and height); the host turns it on while one asks.
        static void RequestTextInput(i32 caretX, i32 caretY, i32 caretHeight);
        // Whether text input was asked for since the last call, and where (for the host, once a frame); clears it.
        static bool TakeTextInputRequest(i32& caretX, i32& caretY, i32& caretHeight);

        // No events, nothing composed or asked for (the host's shutdown, tests).
        static void Reset();
        // Counts the resets (a reader keeping keys or buttons held lets them go when it changes).
        static u64 ResetNumber();
    };
}

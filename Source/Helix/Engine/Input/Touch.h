#pragma once

#include <Helix/Math/Vector.h>

// A touch on the screen as the input reports it (the canvas UI's BaseInput reads touches from the frame's input; the
// Input class comes with the input step), and the input method's mode.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine")
    enum class TouchPhase : i32
    {
        Began = 0,
        Moved = 1,
        Stationary = 2,
        Ended = 3,
        Canceled = 4,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class TouchType : i32
    {
        Direct = 0,
        Indirect = 1,
        Stylus = 2,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class IMECompositionMode : i32
    {
        Auto = 0,
        On = 1,
        Off = 2,
    };

    HX_STRUCT(Namespace = "HelixEngine")
    struct Touch
    {
        HX_FIELD() i32 fingerId = 0;
        // In the screen's pixels from the bottom left.
        HX_FIELD() Vector2 position = Vector2(0, 0);
        HX_FIELD() Vector2 rawPosition = Vector2(0, 0);
        HX_FIELD() Vector2 deltaPosition = Vector2(0, 0);
        HX_FIELD() f32 deltaTime = 0;
        HX_FIELD() i32 tapCount = 0;
        HX_FIELD() TouchPhase phase = TouchPhase::Began;
        HX_FIELD() f32 pressure = 1;
        HX_FIELD() f32 maximumPossiblePressure = 1;
        HX_FIELD() TouchType type = TouchType::Direct;
        HX_FIELD() f32 altitudeAngle = 1.5707964f;
        HX_FIELD() f32 azimuthAngle = 0;
        HX_FIELD() f32 radius = 0;
        HX_FIELD() f32 radiusVariance = 0;
    };
}

#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Math/Color.h>

// Debug: the log of scripts (the channel "Script"; Application.logMessageReceived hears every line) and the debug
// drawing calls. The C# side writes the object and format overloads over the text ones (internal there); the C++
// script API calls these directly. DrawLine and DrawRay are accepted and drawn once the gizmo renderer exists.
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine")
    class Debug
    {
    public:
        HX_FUNCTION(Internal) static void Log(StringView message);
        HX_FUNCTION(Internal) static void Log(StringView message, Object* context);
        HX_FUNCTION(Internal) static void LogWarning(StringView message);
        HX_FUNCTION(Internal) static void LogWarning(StringView message, Object* context);
        HX_FUNCTION(Internal) static void LogError(StringView message);
        HX_FUNCTION(Internal) static void LogError(StringView message, Object* context);
        HX_FUNCTION(Internal) static void LogAssertion(StringView message);
        HX_FUNCTION(Internal) static void LogAssertion(StringView message, Object* context);
        // An exception's text (C#'s LogException formats it).
        HX_FUNCTION(Internal) static void LogExceptionText(StringView text, Object* context);

        HX_FUNCTION() static void Assert(bool condition);
        HX_FUNCTION() static void Assert(bool condition, StringView message);
        HX_FUNCTION() static void Assert(bool condition, StringView message, Object* context);
        HX_FUNCTION() static void Assert(bool condition, Object* context);

        HX_PROPERTY(ReadOnly) static bool isDebugBuild();
        HX_FUNCTION() static void Break();

        HX_FUNCTION() static void DrawLine(Vector3 start, Vector3 end);
        HX_FUNCTION() static void DrawLine(Vector3 start, Vector3 end, Color color);
        HX_FUNCTION() static void DrawLine(Vector3 start, Vector3 end, Color color, f32 duration);
        HX_FUNCTION() static void DrawLine(Vector3 start, Vector3 end, Color color, f32 duration, bool depthTest);
        HX_FUNCTION() static void DrawRay(Vector3 start, Vector3 dir);
        HX_FUNCTION() static void DrawRay(Vector3 start, Vector3 dir, Color color);
        HX_FUNCTION() static void DrawRay(Vector3 start, Vector3 dir, Color color, f32 duration);
        HX_FUNCTION() static void DrawRay(Vector3 start, Vector3 dir, Color color, f32 duration, bool depthTest);
    };
}

#include <Helix/Engine/Debug/Debug.h>

#include <Helix/Core/Logging/Log.h>

namespace helix::engine
{
    namespace
    {
        LogChannel ScriptLog{"Script"};

        void Write(LogLevel level, StringView message, Object* context)
        {
            if (context)
                WriteLog(ScriptLog, level, __FILE__, __LINE__, Format("{} ({})", message, context->Name()));
            else
                WriteLog(ScriptLog, level, __FILE__, __LINE__, message);
        }
    }

    void Debug::Log(StringView message) { Write(LogLevel::Info, message, nullptr); }
    void Debug::Log(StringView message, Object* context) { Write(LogLevel::Info, message, context); }
    void Debug::LogWarning(StringView message) { Write(LogLevel::Warning, message, nullptr); }
    void Debug::LogWarning(StringView message, Object* context) { Write(LogLevel::Warning, message, context); }
    void Debug::LogError(StringView message) { Write(LogLevel::Error, message, nullptr); }
    void Debug::LogError(StringView message, Object* context) { Write(LogLevel::Error, message, context); }
    void Debug::LogAssertion(StringView message) { Write(LogLevel::Error, message, nullptr); }
    void Debug::LogAssertion(StringView message, Object* context) { Write(LogLevel::Error, message, context); }
    void Debug::LogExceptionText(StringView text, Object* context) { Write(LogLevel::Error, text, context); }

    // A failed assertion is logged ("Assertion failed" and the message), the run goes on.
    void Debug::Assert(bool condition) { Assert(condition, StringView(), nullptr); }
    void Debug::Assert(bool condition, StringView message) { Assert(condition, message, nullptr); }
    void Debug::Assert(bool condition, Object* context) { Assert(condition, StringView(), context); }
    void Debug::Assert(bool condition, StringView message, Object* context)
    {
        if (condition)
            return;
        String text = message.Empty() ? String("Assertion failed") : Format("Assertion failed: {}", message);
        Write(LogLevel::Error, text, context);
    }

    bool Debug::isDebugBuild() { return HX_DEBUG != 0; }

    void Debug::Break()
    {
#if HX_DEBUG
        HX_LOG_WARNING(ScriptLog, "Debug.Break: the player has no editor to pause.");
#endif
    }

    void Debug::DrawLine(Vector3, Vector3) {}
    void Debug::DrawLine(Vector3, Vector3, Color) {}
    void Debug::DrawLine(Vector3, Vector3, Color, f32) {}
    void Debug::DrawLine(Vector3, Vector3, Color, f32, bool) {}
    void Debug::DrawRay(Vector3, Vector3) {}
    void Debug::DrawRay(Vector3, Vector3, Color) {}
    void Debug::DrawRay(Vector3, Vector3, Color, f32) {}
    void Debug::DrawRay(Vector3, Vector3, Color, f32, bool) {}
}

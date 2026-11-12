#pragma once

#include <Helix/Core/Basics/Types.h>
#include <Helix/Engine/Script/Coroutines.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Scripting/Bindings/Interop.h>

#include <bit>
#include <coroutine>
#include <type_traits>
#include <utility>

// Coroutines of C++ scripts: C++20 coroutines on the engine's scheduler, with the yield instructions of C# scripts.
//
//   Coroutine Blink()
//   {
//       for (int i = 0; i < 3; i++)
//       {
//           renderer->SetEnabled(!renderer->enabled());
//           co_await WaitForSeconds(0.5f);
//       }
//       co_await WaitUntil([this] { return ready; });
//       co_await Fade();                 // a nested coroutine of the same script, waited for
//   }
//   void Start() { StartCoroutine(Blink()); }
//
// co_await NextFrame() is C#'s yield return null. The code is inline and reaches the scheduler through its exports,
// so a game module in a binary of its own uses the scheduler of its process; each binary registers its own language.
namespace helix::engine
{
    class Coroutine;

    struct NextFrame
    {
    };
    struct WaitForSeconds
    {
        explicit WaitForSeconds(f32 s) : seconds(s) {}
        f32 seconds;
    };
    struct WaitForSecondsRealtime
    {
        explicit WaitForSecondsRealtime(f32 s) : waitTime(s) {}
        f32 waitTime;
    };
    struct WaitForFixedUpdate
    {
    };
    struct WaitForEndOfFrame
    {
    };

    enum class WaitTimeoutMode : i32
    {
        Realtime,
        InGameTime,
    };

    // The handle of a started coroutine (null when it ended at once): waited for with co_await, stopped by
    // StopCoroutine.
    struct CoroutineRef
    {
        u64 handle = 0;
        explicit operator bool() const { return handle != 0; }
    };

    namespace detail
    {
        struct CoroutineExports
        {
            u32 (*registerLanguage)(void (*)(u64, u64, CoroutineYield*), u8 (*)(u64, u64), void (*)(u64)) = nullptr;
            u64 (*start)(u64 owner, u32 language, u64 state) = nullptr;
            void (*stop)(u64 handle) = nullptr;
            void (*stopAll)(u64 owner) = nullptr;
            u8 (*isRunning)(u64 handle) = nullptr;
        };

        inline const CoroutineExports& Exports()
        {
            static const CoroutineExports e = [] {
                CoroutineExports x;
                x.registerLanguage = std::bit_cast<decltype(x.registerLanguage)>(scripting::FindNativeExport("HxCoroutines_RegisterLanguage"));
                x.start = std::bit_cast<decltype(x.start)>(scripting::FindNativeExport("HxCoroutines_Start"));
                x.stop = std::bit_cast<decltype(x.stop)>(scripting::FindNativeExport("HxCoroutines_Stop"));
                x.stopAll = std::bit_cast<decltype(x.stopAll)>(scripting::FindNativeExport("HxCoroutines_StopAll"));
                x.isRunning = std::bit_cast<decltype(x.isRunning)>(scripting::FindNativeExport("HxCoroutines_IsRunning"));
                return x;
            }();
            return e;
        }

        // A predicate's wait with an optional timeout (the reference's WaitUntil / WaitWhile).
        struct PredicateWait
        {
            bool (*keepWaiting)(void* self) = nullptr;
            void* self = nullptr;
        };

        u32 CppCoroutineLanguage();
    }

    // A C++ script's coroutine: started by StartCoroutine (or co_await from another coroutine), which owns it then.
    class Coroutine
    {
    public:
        struct promise_type
        {
            CoroutineYield* out = nullptr;
            detail::PredicateWait predicate;
            u64 owner = 0;

            Coroutine get_return_object() { return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this)); }
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept
            {
                if (out)
                    out->wait = u32(CoroutineWait::Ended);
                return {};
            }
            void return_void() {}
            void unhandled_exception() {}

            struct Yield
            {
                promise_type* p;
                CoroutineYield y;
                bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<>) noexcept
                {
                    if (p->out)
                        *p->out = y;
                }
                void await_resume() const noexcept {}
            };

            static CoroutineYield Make(CoroutineWait w, f64 seconds = 0, u64 inner = 0)
            {
                CoroutineYield y;
                y.wait = u32(w);
                y.seconds = seconds;
                y.inner = inner;
                return y;
            }

            Yield await_transform(NextFrame) { return {this, Make(CoroutineWait::NextFrame)}; }
            Yield await_transform(std::nullptr_t) { return {this, Make(CoroutineWait::NextFrame)}; }
            Yield await_transform(WaitForSeconds w) { return {this, Make(CoroutineWait::Seconds, w.seconds)}; }
            Yield await_transform(WaitForSecondsRealtime w) { return {this, Make(CoroutineWait::RealSeconds, w.waitTime)}; }
            Yield await_transform(WaitForFixedUpdate) { return {this, Make(CoroutineWait::FixedUpdate)}; }
            Yield await_transform(WaitForEndOfFrame) { return {this, Make(CoroutineWait::EndOfFrame)}; }
            Yield await_transform(CoroutineRef r) { return {this, Make(CoroutineWait::Coroutine, 0, r.handle)}; }
            // A nested coroutine: started for the same owner, waited for.
            Yield await_transform(Coroutine&& nested);

            // A wait the scheduler polls once per frame after Update: the object lives in the coroutine's frame.
            template <class W>
                requires requires(W& w) {
                    { w.keepWaiting() } -> std::convertible_to<bool>;
                }
            Yield await_transform(W&& wait)
            {
                using T = std::remove_reference_t<W>;
                // The awaited object outlives the suspension (it is a temporary of the co_await expression).
                predicate.self = const_cast<std::remove_const_t<T>*>(&wait);
                predicate.keepWaiting = [](void* self) { return static_cast<T*>(self)->keepWaiting(); };
                return {this, Make(CoroutineWait::Poll)};
            }
            // An async operation (anything with isDone()): until it is done.
            template <class A>
                requires requires(A* a) {
                    { a->isDone() } -> std::convertible_to<bool>;
                }
            Yield await_transform(A* operation)
            {
                predicate.self = operation;
                predicate.keepWaiting = [](void* self) { return !static_cast<A*>(self)->isDone(); };
                return {this, Make(CoroutineWait::Poll)};
            }
        };

        Coroutine(Coroutine&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
        Coroutine& operator=(Coroutine&& o) noexcept
        {
            if (this != &o)
            {
                if (handle_)
                    handle_.destroy();
                handle_ = std::exchange(o.handle_, {});
            }
            return *this;
        }
        Coroutine(const Coroutine&) = delete;
        Coroutine& operator=(const Coroutine&) = delete;
        ~Coroutine()
        {
            if (handle_)
                handle_.destroy();
        }

        // Hands the frame to the scheduler: started for `owner` (an object id), run to its first yield.
        CoroutineRef Start(u64 owner) &&
        {
            const detail::CoroutineExports& e = detail::Exports();
            if (!handle_ || !e.start)
                return {};
            handle_.promise().owner = owner;
            void* frame = std::exchange(handle_, {}).address();
            return CoroutineRef{e.start(owner, detail::CppCoroutineLanguage(), u64(std::bit_cast<uintptr_t>(frame)))};
        }

    private:
        explicit Coroutine(std::coroutine_handle<promise_type> h) : handle_(h) {}
        std::coroutine_handle<promise_type> handle_;
    };

    inline Coroutine::promise_type::Yield Coroutine::promise_type::await_transform(Coroutine&& nested)
    {
        CoroutineRef r = std::move(nested).Start(owner);
        // Ended at once: go on in the next frame, as a finished coroutine does.
        return {this, r ? Make(CoroutineWait::Coroutine, 0, r.handle) : Make(CoroutineWait::NextFrame)};
    }

    namespace detail
    {
        inline f64 CoroutineClock(WaitTimeoutMode mode) { return mode == WaitTimeoutMode::Realtime ? Time::realtimeSinceStartupAsDouble() : Time::timeAsDouble(); }

        // A predicate's wait, inverted for WaitWhile, with an optional timeout.
        template <class P, class T, bool Until>
        class PredicateInstruction
        {
        public:
            explicit PredicateInstruction(P predicate) : predicate_(std::move(predicate)) {}
            PredicateInstruction(P predicate, f64 timeoutSeconds, T onTimeout, WaitTimeoutMode mode)
                : predicate_(std::move(predicate)), onTimeout_(std::move(onTimeout)), timeout_(timeoutSeconds), mode_(mode), start_(CoroutineClock(mode))
            {
            }

            bool keepWaiting()
            {
                if (bool(predicate_()) == Until)
                    return false;
                if (timeout_ >= 0 && CoroutineClock(mode_) - start_ >= timeout_)
                {
                    if constexpr (std::is_pointer_v<T>)
                    {
                        if (onTimeout_)
                            onTimeout_();
                    }
                    else
                        onTimeout_();
                    return false;
                }
                return true;
            }

        private:
            P predicate_;
            T onTimeout_ = T();
            f64 timeout_ = -1;
            WaitTimeoutMode mode_ = WaitTimeoutMode::Realtime;
            f64 start_ = 0;
        };
    }

    // The reference's WaitUntil and WaitWhile, with the timeout overload (seconds, from the instruction's creation;
    // onTimeout runs when the timeout ends the wait first).
    template <class P, class T = void (*)()>
    class WaitUntil : public detail::PredicateInstruction<P, T, true>
    {
    public:
        explicit WaitUntil(P predicate) : detail::PredicateInstruction<P, T, true>(std::move(predicate)) {}
        WaitUntil(P predicate, f64 timeoutSeconds, T onTimeout, WaitTimeoutMode mode = WaitTimeoutMode::Realtime)
            : detail::PredicateInstruction<P, T, true>(std::move(predicate), timeoutSeconds, std::move(onTimeout), mode)
        {
        }
    };

    template <class P, class T = void (*)()>
    class WaitWhile : public detail::PredicateInstruction<P, T, false>
    {
    public:
        explicit WaitWhile(P predicate) : detail::PredicateInstruction<P, T, false>(std::move(predicate)) {}
        WaitWhile(P predicate, f64 timeoutSeconds, T onTimeout, WaitTimeoutMode mode = WaitTimeoutMode::Realtime)
            : detail::PredicateInstruction<P, T, false>(std::move(predicate), timeoutSeconds, std::move(onTimeout), mode)
        {
        }
    };

    namespace detail
    {
        inline u32 CppCoroutineLanguage()
        {
            static const u32 language = [] {
                const CoroutineExports& e = Exports();
                auto resume = [](u64 state, u64, CoroutineYield* out) {
                    auto h = std::coroutine_handle<Coroutine::promise_type>::from_address(std::bit_cast<void*>(uintptr_t(state)));
                    h.promise().out = out;
                    h.resume();
                    h.promise().out = nullptr;
                };
                auto poll = [](u64 state, u64) -> u8 {
                    auto h = std::coroutine_handle<Coroutine::promise_type>::from_address(std::bit_cast<void*>(uintptr_t(state)));
                    const PredicateWait& p = h.promise().predicate;
                    return p.keepWaiting && p.keepWaiting(p.self) ? 1 : 0;
                };
                auto release = [](u64 state) { std::coroutine_handle<>::from_address(std::bit_cast<void*>(uintptr_t(state))).destroy(); };
                return e.registerLanguage ? e.registerLanguage(resume, poll, release) : ~0u;
            }();
            return language;
        }
    }
}

// The same names in the C++ script API's namespace.
namespace HelixEngine
{
    using ::helix::engine::Coroutine;
    using ::helix::engine::CoroutineRef;
    using ::helix::engine::NextFrame;
    using ::helix::engine::WaitForEndOfFrame;
    using ::helix::engine::WaitForFixedUpdate;
    using ::helix::engine::WaitForSeconds;
    using ::helix::engine::WaitForSecondsRealtime;
    using ::helix::engine::WaitTimeoutMode;
    using ::helix::engine::WaitUntil;
    using ::helix::engine::WaitWhile;
}

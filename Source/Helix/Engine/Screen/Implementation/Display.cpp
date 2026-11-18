#include <Helix/Engine/Screen/Display.h>

namespace helix::engine
{
    namespace
    {
        struct State
        {
            Array<DisplayState> displays;
            Displays::ActivateFunction activate = nullptr;
            void* user = nullptr;
            Displays::UpdatedFunction updated = nullptr;
            i32 current = 0;
        };

        // Never destroyed (read during the process's end).
        State& S()
        {
            static State* s = [] {
                auto* made = new State();
                made->displays.Resize(1);
                made->displays[0].active = true;
                return made;
            }();
            return *s;
        }

        bool Valid(i32 index) { return index >= 0 && usize(index) < S().displays.Size(); }
    }

    void Displays::SetHost(ActivateFunction activate, void* user)
    {
        S().activate = activate;
        S().user = user;
    }

    i32 Displays::Count() { return i32(S().displays.Size()); }

    const DisplayState& Displays::Get(i32 index)
    {
        static const DisplayState none{};
        return Valid(index) ? S().displays[usize(index)] : none;
    }

    void Displays::SetCount(i32 count)
    {
        State& s = S();
        usize n = count < 1 ? 1 : usize(count);
        if (n == s.displays.Size())
            return;
        s.displays.Resize(n);
        s.displays[0].active = true;
        Changed();
    }

    void Displays::Set(i32 index, const DisplayState& info)
    {
        if (index < 0)
            return;
        State& s = S();
        if (usize(index) >= s.displays.Size())
            s.displays.Resize(usize(index) + 1);
        DisplayState& d = s.displays[usize(index)];
        bool resized = d.systemWidth != info.systemWidth || d.systemHeight != info.systemHeight || d.active != info.active;
        d = info;
        if (index == 0)
            d.active = true;
        if (resized)
            Changed();
    }

    i32 Displays::Add(const DisplayState& info)
    {
        State& s = S();
        // A free place first (a display of a closed tool window).
        for (usize i = 1; i < s.displays.Size(); i++)
            if (!s.displays[i].active && !s.displays[i].swapChain.IsValid() && s.displays[i].systemWidth == 0)
            {
                s.displays[i] = info;
                Changed();
                return i32(i);
            }
        s.displays.Push(info);
        Changed();
        return i32(s.displays.Size() - 1);
    }

    void Displays::Remove(i32 index)
    {
        if (!Valid(index) || index == 0)
            return;
        State& s = S();
        s.displays[usize(index)] = DisplayState();
        while (s.displays.Size() > 1 && !s.displays.Back().active && s.displays.Back().systemWidth == 0)
            s.displays.Pop();
        Changed();
    }

    bool Displays::Activate(i32 index, i32 width, i32 height, i32 x, i32 y)
    {
        if (!Valid(index))
            return false;
        if (S().displays[usize(index)].active)
            return true;
        return S().activate && S().activate(S().user, index, width, height, x, y);
    }

    bool Displays::Place(i32 index, i32 width, i32 height, i32 x, i32 y)
    {
        return Valid(index) && S().activate && S().activate(S().user, index, width, height, x, y);
    }

    i32 Displays::Current() { return S().current; }
    void Displays::SetCurrent(i32 index) { S().current = index; }

    i32 Displays::At(Vector2 desktop, Vector2& local)
    {
        State& s = S();
        // The tools' windows over the main one (the later added over the earlier).
        for (usize k = s.displays.Size(); k-- > 0;)
        {
            const DisplayState& d = s.displays[k];
            if (!d.active)
                continue;
            f32 lx = desktop.x - f32(d.x), ly = desktop.y - f32(d.y);
            if (lx >= 0 && ly >= 0 && lx < f32(d.systemWidth) && ly < f32(d.systemHeight))
            {
                local = Vector2(lx, ly);
                return i32(k);
            }
        }
        local = desktop;
        return -1;
    }

    void Displays::SetUpdated(UpdatedFunction function) { S().updated = function; }

    void Displays::Changed()
    {
        if (S().updated)
            S().updated();
        DisplayNative::displaysUpdated().Invoke();
    }

    // -------------------------------------------------------------------------------------------- the script API

    i32 DisplayNative::Count() { return Displays::Count(); }
    bool DisplayNative::Active(i32 index) { return Displays::Get(index).active; }
    i32 DisplayNative::SystemWidth(i32 index) { return Displays::Get(index).systemWidth; }
    i32 DisplayNative::SystemHeight(i32 index) { return Displays::Get(index).systemHeight; }
    i32 DisplayNative::RenderingWidth(i32 index) { return Displays::Get(index).renderingWidth; }
    i32 DisplayNative::RenderingHeight(i32 index) { return Displays::Get(index).renderingHeight; }
    bool DisplayNative::Activate(i32 index, i32 width, i32 height) { return Displays::Activate(index, width, height); }

    void DisplayNative::SetParams(i32 index, i32 width, i32 height, i32 x, i32 y)
    {
        if (Displays::Get(index).active)
            Displays::Place(index, width, height, x, y);
        else
            Displays::Activate(index, width, height, x, y);
    }

    void DisplayNative::SetRenderingResolution(i32 index, i32 width, i32 height)
    {
        DisplayState d = Displays::Get(index);
        d.renderingWidth = width;
        d.renderingHeight = height;
        Displays::Set(index, d);
    }

    Vector3 DisplayNative::RelativeMouseAt(Vector3 desktop)
    {
        Vector2 local;
        i32 index = Displays::At(Vector2(desktop.x, desktop.y), local);
        return Vector3(local.x, local.y, f32(index));
    }

    Event<>& DisplayNative::displaysUpdated()
    {
        static Event<>* e = new Event<>();
        return *e;
    }
}

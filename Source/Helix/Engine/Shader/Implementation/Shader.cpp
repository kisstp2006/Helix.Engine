#include <Helix/Engine/Shader/Implementation/ShaderAccess.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/ShaderCompiler/Frontend/Frontend.h>
#include <Helix/ShaderCompiler/Transforms/Link.h>

#include <BuiltinShadersAssembly.h>
#include <BuiltinShadersSymbols.h>
#include <ShaderLibraryAssembly.h>

#include <cstdlib>
#include <mutex>

namespace helix::engine
{
    using namespace shadercompiler;

    namespace
    {
        LogChannel ShaderLog{"Engine"};

        // Assemblies every shader may reference, by name: the embedded ones and the libraries loaded with shader assets.
        struct Libraries
        {
            std::mutex mutex;
            HashMap<String, LoadedAssembly*> byName;
            bool embedded = false;
        };

        Libraries& Libs()
        {
            static Libraries l;
            return l;
        }

        // An embedded image: the arrays live for the whole run, so the assembly reads them in place.
        LoadedAssembly* Embedded(Span<const u8> image, Span<const u8> pdb)
        {
            LoadedAssembly* a = new LoadedAssembly();
            if (!a->assembly.Load(image))
            {
                delete a;
                return nullptr;
            }
            a->name = String(a->assembly.Name());
            a->hasSymbols = !pdb.Empty() && a->symbols.Load(pdb) && a->symbols.Matches(a->assembly);
            return a;
        }

        // Called with the lock held.
        void EnsureEmbedded(Libraries& l)
        {
            if (l.embedded)
                return;
            l.embedded = true;
            for (LoadedAssembly* a : {Embedded(Span<const u8>(kShaderLibraryAssembly, sizeof(kShaderLibraryAssembly)), {}),
                                      Embedded(Span<const u8>(kBuiltinShadersAssembly, sizeof(kBuiltinShadersAssembly)),
                                               Span<const u8>(kBuiltinShadersSymbols, sizeof(kBuiltinShadersSymbols)))})
                if (a)
                    l.byName.Set(a->name, a);
        }

        // References of the runtime (System.*) are not shader code.
        bool IsRuntime(StringView name)
        {
            return name == "mscorlib" || name == "netstandard" || name == "System" || name.StartsWith("System.") || name.StartsWith("Microsoft.");
        }

        struct ShaderGlobals
        {
            Array<String> keywords = Array<String>();
            PropertySheet properties;
            i32 maximumLOD = i32(0x7FFFFFFF);
            u32 keywordsVersion = 0;
            std::unique_ptr<ShaderCache> cache;
            std::shared_ptr<ShaderProgram> builtin;
            EntityId errorShader;
        };

        ShaderGlobals& G()
        {
            static ShaderGlobals g;
            return g;
        }


        const ShaderTag* FindTag(const Array<ShaderTag>& tags, StringView name)
        {
            for (const ShaderTag& t : tags)
                if (t.name == name)
                    return &t;
            return nullptr;
        }

        // The render queue a Queue tag names: a queue name with an optional +n or -n, or a number.
        struct QueueName
        {
            StringView name;
            i32 value;
        };
        constexpr QueueName kQueues[] = {
            {"Background", 1000}, {"Geometry", 2000}, {"AlphaTest", 2450}, {"GeometryLast", 2500}, {"Transparent", 3000}, {"Overlay", 4000},
        };

        i32 ParseQueue(StringView tag)
        {
            tag = tag.Trimmed();
            for (const QueueName& q : kQueues)
                if (tag.StartsWith(q.name))
                {
                    StringView rest = tag.Substr(q.name.Size()).Trimmed();
                    if (rest.Empty())
                        return q.value;
                    String digits(rest.Substr(1));
                    i32 n = i32(std::atoi(digits.CStr()));
                    return rest[0] == '-' ? q.value - n : q.value + n;
                }
            String digits(tag);
            return digits.Empty() ? 2000 : i32(std::atoi(digits.CStr()));
        }

        u64 VariantKey(u32 shader, u32 pass, ShaderTarget target, Span<const KeywordValue> keywords)
        {
            u64 h = CombineHash(MixHash(shader), CombineHash(MixHash(pass), MixHash(u64(target))));
            for (const KeywordValue& k : keywords)
                h = CombineHash(h, CombineHash(Hash64(k.name.Data(), k.name.Size()), MixHash(u64(k.value))));
            return h;
        }

        bool Compile(const ShaderDescription& desc, const ShaderPass& pass, const ShaderProgram& program, ShaderTarget target, ShaderVariant& v)
        {
            TranslateOptions options;
            options.keywords = Span<const KeywordValue>(v.keywords.Data(), v.keywords.Size());
            options.references = Span<const Assembly* const>(program.references.Data(), program.references.Size());
            options.symbols = Span<const DebugInfo* const>(program.symbols.Data(), program.symbols.Size());
            const Assembly& assembly = pass.assembly ? *pass.assembly : program.main->assembly;
            Module vertex, fragment;
            ShaderReflection vr, fr;
            String& error = v.error;
            return TranslateEntryPoint(assembly, pass.vertex, Stage::Vertex, vertex, error, options) &&
                   TranslateEntryPoint(assembly, pass.fragment, Stage::Fragment, fragment, error, options) && LinkStages(vertex, fragment, error) &&
                   Reflect(vertex, vr, error) && Reflect(fragment, fr, error) && LinkReflection(vr, fr, v.reflection, error) &&
                   BindProperties(desc, v.reflection, v.layout, error) && CompileModule(vertex, target, v.vertex, error, &Shader::Cache()) &&
                   CompileModule(fragment, target, v.fragment, error, &Shader::Cache());
        }

        std::shared_ptr<ShaderProgram>& Builtin()
        {
            ShaderGlobals& g = G();
            if (!g.builtin)
            {
                Libraries& l = Libs();
                LoadedAssembly* builtin = nullptr;
                {
                    std::lock_guard<std::mutex> lock(l.mutex);
                    EnsureEmbedded(l);
                    if (LoadedAssembly** a = l.byName.Find(String("HelixEngine.BuiltinShaders")))
                        builtin = *a;
                }
                auto p = std::make_shared<ShaderProgram>();
                String error;
                if (builtin)
                {
                    for (auto e : l.byName)
                        p->references.Push(&e.value->assembly);
                    if (builtin->hasSymbols)
                        p->symbols.Push(&builtin->symbols);
                    if (!ReadShaderDefinitions(builtin->assembly, p->shaders, error, Span<const Assembly* const>(p->references.Data(), p->references.Size())))
                        HX_LOG_ERROR(ShaderLog, "The built in shaders could not be read: {}", error);
                }
                g.builtin = p;
            }
            return g.builtin;
        }

        // A built in shader by name, made on first use.
        Shader* FindBuiltin(StringView name)
        {
            std::shared_ptr<ShaderProgram>& p = Builtin();
            for (u32 i = 0; i < p->shaders.Size(); i++)
                if (p->shaders[i].name == name && p->shaders[i].assembly && p->shaders[i].assembly->Name() == StringView("HelixEngine.BuiltinShaders"))
                {
                    Shader* s = Shader::Create(p, i);
                    s->SetPersistent(true);
                    s->hideFlags = HideFlags::HideAndDontSave;
                    return s;
                }
            return nullptr;
        }
    }

    // The ids a material writes a variant's blocks with, found once.
    void PlanWrites(ShaderVariant& v)
    {
        const ShaderDescription& d = *v.description;
        for (const PropertyBinding& b : v.layout.bindings)
            v.bindingIds.Push(engine::PropertyToID(d.properties[b.property].name));
        for (const ReflectedBlock& rb : v.reflection.blocks)
        {
            u32 block = ~0u;
            for (u32 i = 0; i < v.layout.blocks.Size(); i++)
                if (v.layout.blocks[i].slot == rb.slot)
                    block = i;
            if (block == ~0u || v.layout.blocks[block].propertiesOnly)
                continue;
            for (const ReflectedMember& m : rb.members)
            {
                // "weights[3]": the value "weights", element 3.
                StringView name = m.name;
                u32 element = 0;
                if (!name.Empty() && name[name.Size() - 1] == ']')
                    if (usize open = name.FindLast('['); open != StringView::npos)
                    {
                        for (usize i = open + 1; i + 1 < name.Size(); i++)
                            element = element * 10 + u32(name[i] - '0');
                        name = name.Substr(0, open);
                    }
                if (d.FindProperty(name) || (name.EndsWith("_ST") && d.FindProperty(name.Substr(0, name.Size() - 3))))
                    continue;
                v.namedMembers.Push({block, &m, engine::PropertyToID(name), element});
            }
        }
    }

    ShaderProgram::~ShaderProgram()
    {
        for (auto e : variants)
            delete e.value;
    }

    bool LoadShaderProgram(Array<u8>&& image, Array<u8>&& pdb, AssemblyReader reader, void* context, std::shared_ptr<ShaderProgram>& out, String& error)
    {
        auto p = std::make_shared<ShaderProgram>();
        p->main = std::make_unique<LoadedAssembly>();
        LoadedAssembly& main = *p->main;
        main.image = Move(image);
        main.pdb = Move(pdb);
        if (!main.assembly.Load(Span<const u8>(main.image.Data(), main.image.Size())))
            return error = "the file is not a .NET assembly", false;
        main.name = String(main.assembly.Name());
        main.hasSymbols = !main.pdb.Empty() ? main.symbols.Load(Span<const u8>(main.pdb.Data(), main.pdb.Size())) && main.symbols.Matches(main.assembly)
                                            : main.symbols.LoadEmbedded(main.assembly);
        p->references.Push(&main.assembly);
        if (main.hasSymbols)
            p->symbols.Push(&main.symbols);
        // Every assembly it references, through the libraries first (one copy of each for the whole run).
        Libraries& l = Libs();
        Array<const LoadedAssembly*> work;
        work.Push(&main);
        while (!work.Empty())
        {
            const LoadedAssembly* a = work[work.Size() - 1];
            work.RemoveAt(work.Size() - 1);
            for (u32 row = 1; row <= a->assembly.RowCount(Table::AssemblyRef); row++)
            {
                // Column 6 of AssemblyRef is the name.
                StringView name = a->assembly.GetString(a->assembly.Cell(Table::AssemblyRef, row, 6));
                if (IsRuntime(name) || name == StringView(main.name))
                    continue;
                bool known = false;
                for (const Assembly* r : p->references)
                    known |= r->Name() == name;
                if (known)
                    continue;
                LoadedAssembly* found = nullptr;
                {
                    std::lock_guard<std::mutex> lock(l.mutex);
                    EnsureEmbedded(l);
                    if (LoadedAssembly** f = l.byName.Find(String(name)))
                        found = *f;
                }
                if (!found)
                {
                    LoadedAssembly* fresh = new LoadedAssembly();
                    if (!reader || !reader(context, name, fresh->image, fresh->pdb) ||
                        !fresh->assembly.Load(Span<const u8>(fresh->image.Data(), fresh->image.Size())) || fresh->assembly.Name() != name)
                    {
                        delete fresh;
                        return error = Format("{} references {}, which is not next to it", main.name, name), false;
                    }
                    fresh->name = String(name);
                    fresh->hasSymbols = !fresh->pdb.Empty() && fresh->symbols.Load(Span<const u8>(fresh->pdb.Data(), fresh->pdb.Size())) &&
                                        fresh->symbols.Matches(fresh->assembly);
                    std::lock_guard<std::mutex> lock(l.mutex);
                    // Another thread may have loaded the same library meanwhile.
                    if (LoadedAssembly** f = l.byName.Find(fresh->name))
                    {
                        delete fresh;
                        found = *f;
                    }
                    else
                    {
                        l.byName.Set(fresh->name, fresh);
                        found = fresh;
                    }
                }
                p->references.Push(&found->assembly);
                if (found->hasSymbols)
                    p->symbols.Push(&found->symbols);
                work.Push(found);
            }
        }
        if (!ReadShaderDefinitions(main.assembly, p->shaders, error, Span<const Assembly* const>(p->references.Data(), p->references.Size())))
            return false;
        out = Move(p);
        return true;
    }

    // ---------------------------------------------------------------------------------------------- objects

    Shader* Shader::Create(std::shared_ptr<ShaderProgram> program, u32 index)
    {
        Shader* s = NewObject<Shader>();
        if (program && index < program->shaders.Size())
            s->SetName(program->shaders[index].name);
        s->program_ = Move(program);
        s->index_ = index;
        return s;
    }

    Shader* Shader::Find(StringView name)
    {
        Shader* found = nullptr;
        ForEachObjectOfClass(ClassOf<Shader>(), [&](Object* o) {
            if (o->Name() == name)
            {
                found = static_cast<Shader*>(o);
                return false;
            }
            return true;
        });
        return found ? found : FindBuiltin(name);
    }

    Shader* Shader::ErrorShader()
    {
        ShaderGlobals& g = G();
        if (Shader* s = EntityIdToObject<Shader>(g.errorShader))
            return s;
        Shader* s = Find("Hidden/InternalErrorShader");
        g.errorShader = s ? s->GetEntityId() : EntityId();
        return s;
    }

    const ShaderDescription* Shader::Description() const { return program_ && index_ < program_->shaders.Size() ? &program_->shaders[index_] : nullptr; }

    bool Shader::isSupported() const { return Description() != nullptr; }

    i32 Shader::globalMaximumLOD() { return G().maximumLOD; }
    void Shader::SetGlobalMaximumLOD(i32 value) { G().maximumLOD = value; }

    i32 Shader::renderQueue() const
    {
        const ShaderDescription* d = Description();
        if (!d)
            return 2000;
        const ShaderTag* t = !d->subShaders.Empty() ? FindTag(d->subShaders[0].tags, "Queue") : nullptr;
        if (!t)
            t = FindTag(d->tags, "Queue");
        return t ? ParseQueue(t->value) : 2000;
    }

    i32 Shader::GetPassCountInSubshader(i32 subshader) const
    {
        const ShaderDescription* d = Description();
        i32 n = 0;
        if (d)
            for (const ShaderPass& p : d->passes)
                n += i32(p.subShader) == subshader;
        return n;
    }

    i32 Shader::passCount() const { return GetPassCountInSubshader(0); }

    i32 Shader::subshaderCount() const
    {
        const ShaderDescription* d = Description();
        return d ? i32(d->subShaders.Empty() ? 1 : d->subShaders.Size()) : 0;
    }

    i32 Shader::PassIndex(i32 pass) const
    {
        const ShaderDescription* d = Description();
        if (!d || pass < 0)
            return -1;
        i32 n = 0;
        for (u32 i = 0; i < d->passes.Size(); i++)
            if (d->passes[i].subShader == 0 && n++ == pass)
                return i32(i);
        return -1;
    }

    StringView Shader::FindPassTagValue(i32 pass, StringView tag) const
    {
        i32 i = PassIndex(pass);
        if (i < 0)
            return {};
        const ShaderPass& p = Description()->passes[u32(i)];
        if (tag == "LightMode")
            return p.lightMode;
        const ShaderTag* t = FindTag(p.tags, tag);
        return t ? StringView(t->value) : StringView();
    }

    StringView Shader::FindSubshaderTagValue(i32 subshader, StringView tag) const
    {
        const ShaderDescription* d = Description();
        if (!d || subshader < 0)
            return {};
        const ShaderTag* t = usize(subshader) < d->subShaders.Size() ? FindTag(d->subShaders[usize(subshader)].tags, tag) : nullptr;
        if (!t && (d->subShaders.Empty() || subshader == 0))
            t = FindTag(d->tags, tag);
        return t ? StringView(t->value) : StringView();
    }

    // ---------------------------------------------------------------------------------------------- properties

    namespace
    {
        const ShaderProperty* PropertyAt(const ShaderDescription* d, i32 index)
        {
            if (!d || index < 0 || usize(index) >= d->properties.Size())
            {
                HX_LOG_ERROR(ShaderLog, "Shader property index {} is out of bounds.", index);
                return nullptr;
            }
            return &d->properties[usize(index)];
        }
    }

    i32 Shader::GetPropertyCount() const
    {
        const ShaderDescription* d = Description();
        return d ? i32(d->properties.Size()) : 0;
    }

    i32 Shader::FindPropertyIndex(StringView name) const
    {
        const ShaderDescription* d = Description();
        if (d)
            for (u32 i = 0; i < d->properties.Size(); i++)
                if (d->properties[i].name == name)
                    return i32(i);
        return -1;
    }

    StringView Shader::GetPropertyName(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p ? StringView(p->name) : StringView();
    }

    i32 Shader::GetPropertyNameId(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p ? engine::PropertyToID(p->name) : 0;
    }

    StringView Shader::GetPropertyDescription(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p ? StringView(p->displayName) : StringView();
    }

    ShaderPropertyType Shader::GetPropertyType(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        if (!p)
            return ShaderPropertyType::Float;
        if (p->type.IsTexture())
            return ShaderPropertyType::Texture;
        if (p->type.IsScalar())
            return p->type.IsInteger() ? ShaderPropertyType::Int : p->hasRange ? ShaderPropertyType::Range : ShaderPropertyType::Float;
        // Vectors (and matrices, which the reference has no property type for); a color when it is marked as one.
        return p->Has(PropertyFlags::MainColor) || p->Has(PropertyFlags::Hdr) ? ShaderPropertyType::Color : ShaderPropertyType::Vector;
    }

    i32 Shader::GetPropertyFlags(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        if (!p)
            return 0;
        // The compiler's flags to the reference's numbers.
        struct FlagPair
        {
            PropertyFlags from;
            ShaderPropertyFlags to;
        };
        static constexpr FlagPair kFlags[] = {
            {PropertyFlags::HideInInspector, ShaderPropertyFlags::HideInInspector}, {PropertyFlags::PerRendererData, ShaderPropertyFlags::PerRendererData},
            {PropertyFlags::NoScaleOffset, ShaderPropertyFlags::NoScaleOffset},     {PropertyFlags::Hdr, ShaderPropertyFlags::HDR},
            {PropertyFlags::MainTexture, ShaderPropertyFlags::MainTexture},         {PropertyFlags::MainColor, ShaderPropertyFlags::MainColor},
        };
        i32 out = 0;
        for (const FlagPair& f : kFlags)
            if (p->Has(f.from))
                out |= i32(f.to);
        return out;
    }

    f32 Shader::GetPropertyDefaultFloatValue(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p ? p->defaultValue[0] : 0.0f;
    }

    i32 Shader::GetPropertyDefaultIntValue(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p ? i32(p->defaultValue[0]) : 0;
    }

    Vector4 Shader::GetPropertyDefaultVectorValue(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p ? Vector4(p->defaultValue[0], p->defaultValue[1], p->defaultValue[2], p->defaultValue[3]) : Vector4();
    }

    Vector2 Shader::GetPropertyRangeLimits(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p && p->hasRange ? Vector2(p->rangeMin, p->rangeMax) : Vector2();
    }

    TextureDimension Shader::GetPropertyTextureDimension(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        if (!p || !p->type.IsTexture())
            return TextureDimension::None;
        switch (p->type.base)
        {
        case BaseType::Texture2DArray:
            return TextureDimension::Tex2DArray;
        case BaseType::Texture3D:
            return TextureDimension::Tex3D;
        case BaseType::TextureCube:
            return TextureDimension::Cube;
        default:
            return TextureDimension::Tex2D;
        }
    }

    StringView Shader::GetPropertyTextureDefaultName(i32 index) const
    {
        const ShaderProperty* p = PropertyAt(Description(), index);
        return p ? StringView(p->defaultTexture) : StringView();
    }

    void Shader::GetLocalKeywords(Array<String>& out) const
    {
        out.Clear();
        const ShaderDescription* d = Description();
        if (!d)
            return;
        for (const ShaderKeyword& k : d->keywords)
        {
            if (k.global)
                continue;
            if (k.valueNames.Empty())
                out.Push(k.name);
            for (const String& member : k.valueNames)
                out.Push(Format("{}_{}", k.name, member));
        }
    }

    bool Shader::IsLocalKeyword(StringView keyword) const
    {
        Array<String> all;
        GetLocalKeywords(all);
        for (const String& k : all)
            if (k == keyword)
                return true;
        return false;
    }

    // ---------------------------------------------------------------------------------------------- keywords

    void Shader::EnableKeyword(StringView keyword)
    {
        ShaderGlobals& g = G();
        for (const String& k : g.keywords)
            if (k == keyword)
                return;
        g.keywords.Push(String(keyword));
        g.keywordsVersion++;
    }

    void Shader::DisableKeyword(StringView keyword)
    {
        ShaderGlobals& g = G();
        for (usize i = 0; i < g.keywords.Size(); i++)
            if (g.keywords[i] == keyword)
            {
                g.keywords.RemoveAt(i);
                g.keywordsVersion++;
                return;
            }
    }

    u32 Shader::GlobalKeywordsVersion() { return G().keywordsVersion; }

    void Shader::ClearVariants()
    {
        if (!program_)
            return;
        // Only this shader's: the program's other shaders keep theirs.
        Array<u64> mine;
        for (auto e : program_->variants)
            if (e.value->description == Description())
                mine.Push(e.key);
        for (u64 k : mine)
        {
            delete *program_->variants.Find(k);
            program_->variants.Remove(k);
        }
    }

    bool Shader::IsKeywordEnabled(StringView keyword)
    {
        for (const String& k : G().keywords)
            if (k == keyword)
                return true;
        return false;
    }

    void Shader::GetEnabledGlobalKeywords(Array<String>& out) { out = G().keywords; }

    PropertySheet& Shader::Globals() { return G().properties; }

    void Shader::KeywordValues(Span<const String> enabled, Array<KeywordValue>& out) const
    {
        out.Clear();
        const ShaderDescription* d = Description();
        if (!d)
            return;
        auto on = [&](const ShaderKeyword& k, StringView name) {
            for (const String& e : enabled)
                if (e == name)
                    return true;
            return k.global && IsKeywordEnabled(name);
        };
        for (const ShaderKeyword& k : d->keywords)
        {
            KeywordValue v;
            v.name = k.name;
            v.value = 0;
            if (k.valueNames.Empty())
                v.value = on(k, k.name) ? 1 : 0;
            else
            {
                // An enum keyword: the member enabled as name_member, the first member otherwise.
                v.value = k.values.Empty() ? 0 : k.values[0];
                for (usize m = 0; m < k.valueNames.Size() && m < k.values.Size(); m++)
                    if (on(k, Format("{}_{}", k.name, k.valueNames[m])))
                    {
                        v.value = k.values[m];
                        break;
                    }
            }
            out.Push(v);
        }
    }

    // ---------------------------------------------------------------------------------------------- variants

    void Shader::SetShaderCacheDirectory(StringView directory) { G().cache = std::make_unique<ShaderCache>(directory); }

    ShaderCache& Shader::Cache()
    {
        ShaderGlobals& g = G();
        if (!g.cache)
            g.cache = std::make_unique<ShaderCache>();
        return *g.cache;
    }

    const ShaderVariant* Shader::GetVariant(u32 pass, Span<const KeywordValue> keywords, ShaderTarget target)
    {
        const ShaderDescription* d = Description();
        if (!d || pass >= d->passes.Size())
        {
            Shader* error = ErrorShader();
            return error && error != this ? error->GetVariant(0, {}, target) : nullptr;
        }
        u64 key = VariantKey(index_, pass, target, keywords);
        ShaderVariant* v = nullptr;
        if (ShaderVariant** found = program_->variants.Find(key))
            v = *found;
        else
        {
            v = new ShaderVariant();
            static u32 serials = 0;
            v->serial = ++serials;
            v->pass = pass;
            v->description = d;
            v->target = target;
            v->state = &d->passes[pass].state;
            // The keyword names must outlive the variant: they point into the description.
            for (const KeywordValue& k : keywords)
                if (const ShaderKeyword* sk = d->FindKeyword(k.name))
                    v->keywords.Push({StringView(sk->name), k.value});
            v->ok = Compile(*d, d->passes[pass], *program_, target, *v);
            if (v->ok)
                PlanWrites(*v);
            program_->variants.Set(key, v);
            if (!v->ok)
                HX_LOG_ERROR(ShaderLog, "Shader error in '{}', pass {}: {}", d->name, d->passes[pass].name, v->error);
        }
        if (v->ok)
            return v;
        Shader* error = ErrorShader();
        return error && error != this ? error->GetVariant(0, {}, target) : nullptr;
    }
}

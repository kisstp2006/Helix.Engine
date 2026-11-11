#include <Helix/Engine/Asset/AssetLoader.h>
#include <Helix/Engine/Shader/Implementation/ShaderAccess.h>

#include <Helix/Core/Strings/Format.h>

namespace helix::engine
{
    namespace
    {
        struct DecodedShaders
        {
            std::shared_ptr<ShaderProgram> program;
        };

        // Files next to the assembly, through its source.
        struct Neighbours
        {
            AssetSource* source;
            String folder;
        };

        String NextTo(const Neighbours& n, StringView file) { return n.folder.Empty() ? String(file) : String(n.folder).Append('/').Append(file); }

        bool ReadReference(void* context, StringView name, Array<u8>& dll, Array<u8>& pdb)
        {
            const Neighbours& n = *static_cast<const Neighbours*>(context);
            String error;
            if (!n.source->Read(NextTo(n, Format("{}.dll", name)), dll, error))
                return false;
            if (!n.source->Read(NextTo(n, Format("{}.pdb", name)), pdb, error))
                pdb.Clear();
            return true;
        }

        void* Decode(const AssetDecodeInput& in, String& error)
        {
            usize slash = in.path.FindLast('/');
            Neighbours n{in.source, slash == StringView::npos ? String() : String(in.path.Substr(0, slash))};
            // The debug information for file:line in errors: name.pdb next to name.dll, or embedded.
            Array<u8> pdb;
            String ignored;
            if (!in.source->Read(String(in.path.Substr(0, in.path.Size() - 4)).Append(".pdb"), pdb, ignored))
                pdb.Clear();
            Array<u8> image;
            if (in.owner)
                image = Move(*in.owner);
            else
            {
                image.Resize(in.bytes.Size());
                for (usize i = 0; i < in.bytes.Size(); i++)
                    image[i] = in.bytes[i];
            }
            DecodedShaders* d = new DecodedShaders();
            if (!LoadShaderProgram(Move(image), Move(pdb), &ReadReference, &n, d->program, error))
            {
                delete d;
                return nullptr;
            }
            return d;
        }

        // One Shader per definition; an assembly without any (a library) makes no objects.
        bool Integrate(void* decoded, AssetObjects& out, Array<String>&)
        {
            DecodedShaders& d = *static_cast<DecodedShaders*>(decoded);
            bool first = true;
            for (u32 i = 0; i < d.program->shaders.Size(); i++)
            {
                // The definitions of the referenced assemblies are there for UsePass only.
                if (d.program->shaders[i].assembly != &d.program->main->assembly)
                    continue;
                Shader* s = Shader::Create(d.program, i);
                out.Add(AssetObjectFileId(ClassId::Shader, d.program->shaders[i].name), s, first);
                first = false;
            }
            return true;
        }
    }

    AssetLoader ShaderAssetLoader()
    {
        AssetLoader l;
        l.name = "Shader";
        l.extensions.Push(String("dll"));
        l.referenceType = kAssetTypeImported;
        l.decode = &Decode;
        l.integrate = &Integrate;
        l.release = [](void* d) { delete static_cast<DecodedShaders*>(d); };
        return l;
    }
}

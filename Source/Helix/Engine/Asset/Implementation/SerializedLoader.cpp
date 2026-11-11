#include <Helix/Engine/Asset/AssetLoader.h>
#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Scene/Implementation/SceneAccess.h>

#include <cstring>

namespace helix::engine
{
    namespace
    {
        // The engine's own files (YAML or binary): parsed on the job, built in the asset slot on the main thread.
        struct DecodedFile
        {
            // The parsed form points into the bytes.
            Array<u8> bytes;
            const SceneFormat* format = nullptr;
            ParsedScene* parsed = nullptr;
            ~DecodedFile() { delete parsed; }
        };

        void* Decode(const AssetDecodeInput& in, String& error)
        {
            const SceneFormat* format = FindSceneFormat(in.bytes);
            if (!format)
                return error = "no scene format reads the file", nullptr;
            DecodedFile* d = new DecodedFile();
            if (in.owner)
                d->bytes = Move(*in.owner);
            else
            {
                d->bytes.Resize(in.bytes.Size());
                if (in.bytes.Size())
                    std::memcpy(d->bytes.Data(), in.bytes.Data(), in.bytes.Size());
            }
            ParsedScene* parsed = format->Parse(Span<const u8>(d->bytes.Data(), d->bytes.Size()), error);
            if (!parsed)
            {
                delete d;
                return nullptr;
            }
            d->format = format;
            d->parsed = parsed;
            return d;
        }

        bool Integrate(void* decoded, AssetObjects& out, Array<String>& errors)
        {
            DecodedFile& d = *static_cast<DecodedFile*>(decoded);
            SceneBuildOptions options;
            options.persistent = true;
            options.enable = false;
            Array<EntityId> roots, created;
            Array<i64> fileIds;
            bool ok = SceneAccess::Build(*d.format, *d.parsed, kAssetSlot, errors, options, &roots, &created, &fileIds);
            // The main object: a prefab's root, else the object at its class's main fileID, else the first.
            i64 main = 0;
            if (!roots.Empty())
                for (usize i = 0; i < created.Size(); i++)
                    if (created[i] == roots[0])
                        main = fileIds[i];
            for (usize i = 0; i < created.Size() && main == 0; i++)
                if (Object* o = EntityIdToObject(created[i]); o && fileIds[i] == MainAssetFileId(o->Class().classId))
                    main = fileIds[i];
            if (roots.Size() > 1)
                errors.Push(Format("a prefab file has one root GameObject, this one has {}", roots.Size()));
            for (usize i = 0; i < created.Size(); i++)
                if (Object* o = EntityIdToObject(created[i]))
                    out.Add(fileIds[i], o, main ? fileIds[i] == main : i == 0);
            return ok || !created.Empty();
        }
    }

    AssetLoader SerializedAssetLoader()
    {
        AssetLoader l;
        l.name = "Serialized";
        for (StringView e : {StringView("asset"), StringView("prefab")})
            l.extensions.Push(String(e));
        l.referenceType = kAssetTypeSerialized;
        l.decode = &Decode;
        l.integrate = &Integrate;
        l.release = [](void* d) { delete static_cast<DecodedFile*>(d); };
        return l;
    }
}

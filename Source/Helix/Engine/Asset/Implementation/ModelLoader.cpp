#include <Helix/Engine/Asset/AssetLoader.h>
#include <Helix/Engine/Asset/Implementation/MetaFields.h>
#include <Helix/Engine/Asset/Implementation/TextureImport.h>
#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/GameObject/Implementation/GameObjectAccess.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Scene/Implementation/SceneAccess.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>
#include <Helix/Engine/Transform/Transform.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Image/Codecs/ImageDecoder.h>
#include <Helix/Model/Fbx/Fbx.h>
#include <Helix/Model/Gltf/Gltf.h>
#include <Helix/Model/Mesh/Mesh.h>
#include <Helix/Model/Obj/Obj.h>
#include <Helix/Model/Scene/Convert.h>

#include <cmath>
#include <cstring>

namespace helix::engine
{
    namespace
    {
        LogChannel ModelLog{"Engine"};

        // The readers by extension.
        struct ModelFormat
        {
            StringView extension;
            bool (*read)(Span<const u8>, model::Scene&, String&, const model::ReadOptions&);
        };
        constexpr ModelFormat kModelFormats[] = {
            {"gltf", &model::ReadGltf},
            {"glb", &model::ReadGltf},
            {"fbx", &model::ReadFbx},
            {"obj", &model::ReadObj},
        };

        struct ModelImportSettings
        {
            f32 scale = 1;
            bool convertUnits = true;
            bool readable = false;
        };

        constexpr MetaField kModelFields[] = {
            {"globalScale", [](void* s, double v) { static_cast<ModelImportSettings*>(s)->scale = f32(v); }},
            {"useFileScale", [](void* s, double v) { static_cast<ModelImportSettings*>(s)->convertUnits = v != 0; }},
            {"isReadable", [](void* s, double v) { static_cast<ModelImportSettings*>(s)->readable = v != 0; }},
        };

        // The model's streams that become vertex attributes (skins and custom attributes wait for their milestones).
        struct SemanticAttribute
        {
            model::Semantic semantic;
            VertexAttribute attribute;
            u32 components;
            // For primitives that lack the stream.
            f32 fill[4];
        };
        constexpr SemanticAttribute kAttributes[] = {
            {model::Semantic::Position, VertexAttribute::Position, 3, {0, 0, 0, 0}},
            {model::Semantic::Normal, VertexAttribute::Normal, 3, {0, 0, 1, 0}},
            {model::Semantic::Tangent, VertexAttribute::Tangent, 4, {1, 0, 0, 1}},
            {model::Semantic::Color, VertexAttribute::Color, 4, {1, 1, 1, 1}},
        };

        // Material maps whose colors are sRGB; every other map is linear data.
        constexpr model::TextureRef model::Material::* kSrgbMaps[] = {
            &model::Material::baseColorTexture,
            &model::Material::emissiveTexture,
            &model::Material::specularColorTexture,
            &model::Material::sheenColorTexture,
            &model::Material::specularColorClassicTexture,
        };
        constexpr model::TextureRef model::Material::* kLinearMaps[] = {
            &model::Material::metallicRoughnessTexture, &model::Material::normalTexture, &model::Material::occlusionTexture,
            &model::Material::transmissionTexture, &model::Material::thicknessTexture, &model::Material::clearcoatTexture,
            &model::Material::clearcoatRoughnessTexture, &model::Material::clearcoatNormalTexture, &model::Material::sheenRoughnessTexture,
            &model::Material::specularTexture, &model::Material::iridescenceTexture, &model::Material::iridescenceThicknessTexture,
            &model::Material::anisotropyTexture, &model::Material::roughnessTexture, &model::Material::metallicTexture,
            &model::Material::alphaTexture, &model::Material::bumpTexture, &model::Material::displacementTexture,
            &model::Material::shininessTexture,
        };

        struct DecodedMesh
        {
            String name;
            u32 vertexCount = 0;
            Array<VertexAttributeDescriptor> layout;
            Array<u8> vertices;
            IndexFormat indexFormat = IndexFormat::UInt16;
            Array<u8> indices;
            Array<SubMeshDescriptor> subMeshes;
            Bounds bounds;
        };

        struct DecodedNode
        {
            String name;
            String path;
            u32 parent = model::kNone;
            Vector3 position;
            Quaternion rotation;
            Vector3 scale = Vector3(1, 1, 1);
            u32 mesh = model::kNone;
        };

        struct DecodedModel
        {
            String name;
            bool readable = false;
            Array<DecodedMesh> meshes;
            Array<DecodedNode> nodes;
            Array<DecodedTexture> textures;
            Array<String> warnings;
        };

        StringView Stem(StringView path)
        {
            usize slash = path.FindLast('/');
            StringView name = slash == StringView::npos ? path : path.Substr(slash + 1);
            usize dot = name.FindLast('.');
            return dot == StringView::npos ? name : name.Substr(0, dot);
        }

        StringView Folder(StringView path)
        {
            usize slash = path.FindLast('/');
            return slash == StringView::npos ? StringView() : path.Substr(0, slash);
        }

        String LowerExtension(StringView path)
        {
            usize dot = path.FindLast('.');
            String e;
            if (dot != StringView::npos)
                for (char c : path.Substr(dot + 1))
                    e.Append(c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
            return e;
        }

        // Files next to the model, through the asset's source.
        struct Neighbours
        {
            AssetSource* source;
            String folder;
        };

        bool ReadNextTo(void* context, StringView uri, Array<u8>& out)
        {
            const Neighbours& n = *static_cast<const Neighbours*>(context);
            while (uri.StartsWith("./"))
                uri = uri.Substr(2);
            String path = n.folder.Empty() ? String(uri) : String(n.folder).Append('/').Append(uri);
            String error;
            return n.source->Read(path, out, error);
        }

        // One mesh of the model: every primitive a submesh of one interleaved float stream.
        void BuildMesh(model::Scene& scene, const model::Mesh& mesh, DecodedMesh& out)
        {
            out.name = mesh.name;
            // The attributes any primitive has, in the order of VertexAttribute.
            struct Slot
            {
                VertexAttribute attribute;
                model::Semantic semantic;
                u32 set;
                u32 components;
                const f32* fill;
            };
            Array<Slot> slots;
            static constexpr f32 kZero[4] = {0, 0, 0, 0};
            for (const SemanticAttribute& a : kAttributes)
            {
                bool any = false;
                for (u32 p = mesh.primitives.begin; p < mesh.primitives.begin + mesh.primitives.count; p++)
                    any |= scene.Find(scene.primitives[p], a.semantic) != nullptr;
                if (any || a.attribute == VertexAttribute::Position)
                    slots.Push({a.attribute, a.semantic, 0, a.components, a.fill});
            }
            for (u32 set = 0; set < 8; set++)
            {
                u32 components = 0;
                for (u32 p = mesh.primitives.begin; p < mesh.primitives.begin + mesh.primitives.count; p++)
                    if (const model::VertexStream* s = scene.Find(scene.primitives[p], model::Semantic::TexCoord, set))
                        components = s->components > components ? s->components : components;
                if (components)
                    slots.Push({VertexAttribute(i32(VertexAttribute::TexCoord0) + i32(set)), model::Semantic::TexCoord, set, components < 2 ? 2 : components > 4 ? 4 : components, kZero});
            }
            u32 stride = 0;
            for (const Slot& s : slots)
            {
                out.layout.Push(VertexAttributeDescriptor(s.attribute, VertexAttributeFormat::Float32, i32(s.components), 0));
                stride += s.components * 4;
            }
            u32 total = 0;
            usize indexTotal = 0;
            for (u32 p = mesh.primitives.begin; p < mesh.primitives.begin + mesh.primitives.count; p++)
            {
                const model::Primitive& prim = scene.primitives[p];
                total += prim.vertexCount;
                indexTotal += prim.indices.count ? prim.indices.count : prim.vertexCount;
                if (prim.topology == model::Topology::LineLoop)
                    indexTotal++;
            }
            out.vertexCount = total;
            out.vertices.Resize(usize(total) * stride);
            out.indexFormat = total > 0xFFFF ? IndexFormat::UInt32 : IndexFormat::UInt16;
            usize indexSize = out.indexFormat == IndexFormat::UInt16 ? 2 : 4;
            out.indices.Resize(indexTotal * indexSize);
            u32 base = 0;
            usize at = 0;
            bool first = true;
            for (u32 p = mesh.primitives.begin; p < mesh.primitives.begin + mesh.primitives.count; p++)
            {
                const model::Primitive& prim = scene.primitives[p];
                // Vertices: each attribute from its stream, or its fill value.
                u32 offset = 0;
                for (const Slot& s : slots)
                {
                    const model::VertexStream* stream = scene.Find(prim, s.semantic, s.set);
                    const f32* src = stream ? scene.Data(*stream) : nullptr;
                    u32 n = stream ? (stream->components < s.components ? stream->components : s.components) : 0;
                    for (u32 v = 0; v < prim.vertexCount; v++)
                    {
                        f32 value[4];
                        for (u32 k = 0; k < s.components; k++)
                            value[k] = k < n ? src[usize(v) * stream->components + k] : s.fill[k];
                        // Colors without alpha are opaque.
                        if (stream && s.semantic == model::Semantic::Color && n == 3)
                            value[3] = 1;
                        std::memcpy(out.vertices.Data() + (usize(base) + v) * stride + offset, value, s.components * 4);
                    }
                    offset += s.components * 4;
                }
                // Indices, after the vertices of the primitives before.
                SubMeshDescriptor sub;
                sub.indexStart = i32(at);
                switch (prim.topology)
                {
                case model::Topology::Points:
                    sub.topology = MeshTopology::Points;
                    break;
                case model::Topology::Lines:
                    sub.topology = MeshTopology::Lines;
                    break;
                case model::Topology::LineStrip:
                case model::Topology::LineLoop:
                    sub.topology = MeshTopology::LineStrip;
                    break;
                default:
                    sub.topology = MeshTopology::Triangles;
                    break;
                }
                u32 count = prim.indices.count ? prim.indices.count : prim.vertexCount;
                Vector3 mn(INFINITY, INFINITY, INFINITY), mx(-INFINITY, -INFINITY, -INFINITY);
                const model::VertexStream* pos = scene.Find(prim, model::Semantic::Position);
                auto put = [&](u32 local) {
                    u32 index = base + local;
                    if (indexSize == 2)
                    {
                        u16 v = u16(index);
                        std::memcpy(out.indices.Data() + at * 2, &v, 2);
                    }
                    else
                        std::memcpy(out.indices.Data() + at * 4, &index, 4);
                    at++;
                    if (pos && local < prim.vertexCount)
                    {
                        const f32* q = scene.Data(*pos) + usize(local) * pos->components;
                        mn = Vector3(q[0] < mn.x ? q[0] : mn.x, q[1] < mn.y ? q[1] : mn.y, q[2] < mn.z ? q[2] : mn.z);
                        mx = Vector3(q[0] > mx.x ? q[0] : mx.x, q[1] > mx.y ? q[1] : mx.y, q[2] > mx.z ? q[2] : mx.z);
                    }
                };
                for (u32 i = 0; i < count; i++)
                    put(prim.indices.count ? scene.indices[prim.indices.begin + i] : i);
                if (prim.topology == model::Topology::LineLoop && count)
                    put(prim.indices.count ? scene.indices[prim.indices.begin] : 0);
                sub.indexCount = i32(usize(at) - usize(sub.indexStart));
                sub.firstVertex = i32(base);
                sub.vertexCount = i32(prim.vertexCount);
                if (sub.indexCount && mn.x <= mx.x)
                {
                    sub.bounds.SetMinMax(mn, mx);
                    if (first)
                        out.bounds = sub.bounds;
                    else
                        out.bounds.Encapsulate(sub.bounds);
                    first = false;
                }
                out.subMeshes.Push(sub);
                base += prim.vertexCount;
            }
        }

        TextureSettings SamplingOf(const model::Texture& t)
        {
            TextureSettings s;
            switch (t.minFilter)
            {
            case model::Filter::Nearest:
            case model::Filter::NearestMipmapNearest:
                s.filterMode = FilterMode::Point;
                break;
            case model::Filter::LinearMipmapLinear:
            case model::Filter::NearestMipmapLinear:
                s.filterMode = FilterMode::Trilinear;
                break;
            default:
                s.filterMode = FilterMode::Bilinear;
                break;
            }
            auto wrap = [](model::Wrap w) {
                return w == model::Wrap::ClampToEdge ? TextureWrapMode::Clamp : w == model::Wrap::MirroredRepeat ? TextureWrapMode::Mirror : TextureWrapMode::Repeat;
            };
            s.wrapU = wrap(t.wrapS);
            s.wrapV = wrap(t.wrapT);
            return s;
        }

        void DecodeImages(model::Scene& scene, Neighbours& files, DecodedModel& out)
        {
            // An image is sRGB when a color map uses it, linear when only data maps do.
            Array<u8> srgb, linear;
            srgb.Resize(scene.images.Size());
            linear.Resize(scene.images.Size());
            for (usize i = 0; i < scene.images.Size(); i++)
                srgb[i] = linear[i] = 0;
            auto mark = [&](const model::TextureRef& r, Array<u8>& flags) {
                if (r.Present() && r.texture < scene.textures.Size() && scene.textures[r.texture].image < scene.images.Size())
                    flags[scene.textures[r.texture].image] = 1;
            };
            for (const model::Material& m : scene.materials)
            {
                for (auto map : kSrgbMaps)
                    mark(m.*map, srgb);
                for (auto map : kLinearMaps)
                    mark(m.*map, linear);
            }
            for (usize i = 0; i < scene.images.Size(); i++)
            {
                const model::Image& image = scene.images[i];
                Array<u8> bytes;
                Span<const u8> data;
                if (image.bytes.count)
                    data = Span<const u8>(scene.bytes.Data() + image.bytes.begin, image.bytes.count);
                else if (!image.uri.Empty() && ReadNextTo(&files, image.uri, bytes))
                    data = Span<const u8>(bytes.Data(), bytes.Size());
                else
                {
                    out.warnings.Push(Format("the image '{}' could not be read", image.uri.Empty() ? StringView(image.name) : StringView(image.uri)));
                    continue;
                }
                image::TextureData pixels;
                String error;
                if (!image::DecodeImage(data, pixels, error, LowerExtension(image.uri)))
                {
                    out.warnings.Push(Format("the image '{}' could not be decoded: {}", image.uri.Empty() ? StringView(image.name) : StringView(image.uri), error));
                    continue;
                }
                TextureImportSettings settings;
                settings.srgb = srgb[i] || !linear[i];
                for (const model::Texture& t : scene.textures)
                    if (t.image == i)
                    {
                        settings.sampling = SamplingOf(t);
                        break;
                    }
                DecodedTexture& d = out.textures.Emplace();
                d.name = !image.name.Empty() ? String(image.name) : !image.uri.Empty() ? String(Stem(image.uri)) : Format("Image{}", i);
                if (!ImportTexture(Move(pixels), settings, d, error))
                {
                    out.warnings.Push(Format("the image '{}' could not become a texture: {}", d.name, error));
                    out.textures.RemoveAt(out.textures.Size() - 1);
                }
            }
        }

        void* Decode(const AssetDecodeInput& in, String& error)
        {
            String ext = LowerExtension(in.path);
            const ModelFormat* format = nullptr;
            for (const ModelFormat& f : kModelFormats)
                if (f.extension == StringView(ext))
                    format = &f;
            if (!format)
                return error = Format("no model reader for .{}", ext), nullptr;
            ModelImportSettings settings;
            ReadMetaFields(in.meta, "ModelImporter:", Span<const MetaField>(kModelFields, sizeof(kModelFields) / sizeof(kModelFields[0])), &settings);
            Neighbours files{in.source, String(Folder(in.path))};
            model::ReadOptions options;
            options.loader = &ReadNextTo;
            options.context = &files;
            model::Scene scene;
            if (!format->read(in.bytes, scene, error, options))
                return nullptr;
            model::ConvertOptions convert;
            convert.scaleFactor = settings.scale;
            convert.convertUnits = settings.convertUnits;
            model::ConvertScene(scene, convert);
            // The importer's mesh processing: triangle lists, normals and tangents where the file has none, the vertex
            // cache and fetch order.
            for (u32 p = 0; p < scene.primitives.Size(); p++)
            {
                model::ToTriangleList(scene, p);
                if (scene.primitives[p].topology != model::Topology::Triangles)
                    continue;
                if (!scene.Find(scene.primitives[p], model::Semantic::Normal))
                    model::CalculateNormals(scene, p);
                if (!scene.Find(scene.primitives[p], model::Semantic::Tangent) && scene.Find(scene.primitives[p], model::Semantic::TexCoord))
                    model::CalculateTangents(scene, p);
                if (scene.primitives[p].indices.count)
                {
                    model::OptimizeVertexCache(scene, p);
                    model::OptimizeVertexFetch(scene, p);
                }
            }
            DecodedModel* out = new DecodedModel();
            out->name = String(Stem(in.path));
            out->readable = settings.readable;
            for (const String& w : scene.warnings)
                out->warnings.Push(w);
            for (const model::Mesh& m : scene.meshes)
                BuildMesh(scene, m, out->meshes.Emplace());
            for (usize i = 0; i < out->meshes.Size(); i++)
                if (out->meshes[i].name.Empty())
                    out->meshes[i].name = Format("Mesh{}", i);
            // Nodes parents first (a node's parent comes before it), with their paths for the fileIDs.
            Array<u32> order;
            Array<u32> place;
            place.Resize(scene.nodes.Size());
            Array<u32> stack;
            for (usize i = scene.roots.Size(); i-- > 0;)
                stack.Push(scene.roots[i]);
            while (!stack.Empty())
            {
                u32 n = stack[stack.Size() - 1];
                stack.RemoveAt(stack.Size() - 1);
                place[n] = u32(order.Size());
                order.Push(n);
                const model::Node& node = scene.nodes[n];
                for (u32 c = node.children.count; c-- > 0;)
                    stack.Push(scene.nodeChildren[node.children.begin + c]);
            }
            for (u32 n : order)
            {
                const model::Node& node = scene.nodes[n];
                DecodedNode& d = out->nodes.Emplace();
                d.name = node.name.Empty() ? Format("Node{}", n) : String(node.name);
                d.parent = node.parent == model::kNone ? model::kNone : place[node.parent];
                d.path = d.parent == model::kNone ? String(d.name) : String(out->nodes[d.parent].path).Append('/').Append(d.name);
                d.position = Vector3(node.translation[0], node.translation[1], node.translation[2]);
                d.rotation = Quaternion(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
                d.scale = Vector3(node.scale[0], node.scale[1], node.scale[2]);
                d.mesh = node.mesh;
            }
            DecodeImages(scene, files, *out);
            return out;
        }

        // A fileID not used yet in the asset.
        i64 UniqueFileId(HashMap<i64, u8>& used, i32 classId, StringView name)
        {
            i64 id = AssetObjectFileId(classId, name);
            for (u32 k = 1; used.Find(id); k++)
                id = AssetObjectFileId(classId, Format("{}#{}", name, k));
            used.Set(id, u8(1));
            return id;
        }

        bool Integrate(void* decoded, AssetObjects& out, Array<String>& errors)
        {
            DecodedModel& d = *static_cast<DecodedModel*>(decoded);
            for (const String& w : d.warnings)
                HX_LOG_WARNING(ModelLog, "Model '{}': {}", out.path(), w);
            HashMap<i64, u8> used;
            for (DecodedTexture& t : d.textures)
                if (Texture2D* texture = MakeTexture(t))
                    out.Add(UniqueFileId(used, ClassId::Texture2D, texture->Name()), texture);
            for (DecodedMesh& m : d.meshes)
            {
                Mesh* mesh = NewObject<Mesh>();
                mesh->SetName(m.name);
                if (!mesh->SetMeshData(m.vertexCount, Span<const VertexAttributeDescriptor>(m.layout.Data(), m.layout.Size()), Move(m.vertices), m.indexFormat,
                                       Move(m.indices), Move(m.subMeshes), m.bounds))
                    errors.Push(Format("the mesh '{}' could not be made", m.name));
                mesh->SetReadable(d.readable);
                out.Add(UniqueFileId(used, ClassId::Mesh, m.name), mesh);
            }
            // The prefab: a root named after the file over the model's root nodes, in no scene and never active.
            u32& newRoots = TransformAccess::NewRootScene();
            u32 previous = newRoots;
            newRoots = kAssetSlot;
            GameObject* root = GameObject::Create(d.name);
            Array<GameObject*> made;
            made.Reserve(d.nodes.Size());
            for (const DecodedNode& n : d.nodes)
            {
                GameObject* g = GameObject::Create(n.name);
                Transform* t = g->transform();
                t->SetParent(n.parent == model::kNone ? root->transform() : made[n.parent]->transform(), false);
                t->SetLocalPositionAndRotation(n.position, n.rotation);
                t->SetLocalScale(n.scale);
                made.Push(g);
            }
            newRoots = previous;
            GameObjectAccess::ActiveInHierarchy(*root) = false;
            out.Add(UniqueFileId(used, ClassId::GameObject, d.name), root, true);
            out.Add(UniqueFileId(used, ClassId::Transform, d.name), root->transform());
            for (usize i = 0; i < made.Size(); i++)
            {
                GameObjectAccess::ActiveInHierarchy(*made[i]) = false;
                StringView path = d.nodes[i].path;
                out.Add(UniqueFileId(used, ClassId::GameObject, String(d.name).Append('/').Append(path)), made[i]);
                out.Add(UniqueFileId(used, ClassId::Transform, String(d.name).Append('/').Append(path)), made[i]->transform());
            }
            return true;
        }
    }

    AssetLoader ModelAssetLoader()
    {
        AssetLoader l;
        l.name = "Model";
        for (const ModelFormat& f : kModelFormats)
            l.extensions.Push(String(f.extension));
        l.referenceType = kAssetTypeImported;
        l.decode = &Decode;
        l.integrate = &Integrate;
        l.release = [](void* d) { delete static_cast<DecodedModel*>(d); };
        return l;
    }
}

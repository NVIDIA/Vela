// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/importers.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"

#if VSR_USE_MINISCENE

#include "vsr/io/importers/detail/importer_common.hpp"
// miniScene -- kept to this file: its headers define unprefixed macros
// (NOTIMPLEMENTED, __both__, ...)
#include "miniScene/Scene.h"
// std
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsr::io {

using namespace vsr::scene;
using vsr::math::float2;
using vsr::math::float3;
using vsr::math::float4;
using vsr::math::mat4;
using vsr::math::uint3;

namespace {

// Conversion helpers /////////////////////////////////////////////////////////

float3 fromMini(const mini::vec3f &v)
{
  return float3(v.x, v.y, v.z);
}

// Both sides store column vectors: miniScene's linear-space axes are ANARI's
// mat4 columns, its affine translation the fourth column.
mat4 fromMini(const mini::affine3f &x)
{
  return mat4(float4(fromMini(x.l.vx), 0.f),
      float4(fromMini(x.l.vy), 0.f),
      float4(fromMini(x.l.vz), 0.f),
      float4(fromMini(x.p), 1.f));
}

float average(const float3 &v)
{
  return (v.x + v.y + v.z) / 3.f;
}

// Import state ///////////////////////////////////////////////////////////////

struct MiniSceneImporter
{
  MiniSceneImporter(Scene &scene, const std::string &filename)
      : scene(scene),
        cache(&scene),
        filename(filename),
        sourcePrefix("mini:" + filename + ":")
  {}

  void importInstances(const mini::Scene &in, LayerNodeRef root);
  void importLights(const mini::Scene &in, LayerNodeRef root);

  void logDropped() const;

 private:
  void drop(const std::string &what, size_t count = 1);

  const std::vector<SurfaceRef> &surfacesFor(const mini::Object::SP &object);
  SurfaceRef surfaceFor(const mini::Mesh::SP &mesh);
  MaterialRef materialFor(const mini::Material::SP &material);
  SamplerRef samplerFor(const mini::Texture::SP &texture, const char *slot);

  MaterialRef importANARIMaterial(const mini::ANARIMaterial &m);
  MaterialRef importDisneyMaterial(const mini::DisneyMaterial &m);
  MaterialRef importBlenderMaterial(const mini::BlenderMaterial &m);
  MaterialRef physicallyBased();

  void setParameter(Material &m,
      const char *name,
      float value,
      const mini::Texture::SP &texture = {});
  void setParameter(Material &m,
      const char *name,
      const mini::vec3f &value,
      const mini::Texture::SP &texture = {});

  Scene &scene;
  ImageCache cache;
  std::string filename;
  std::string sourcePrefix;

  std::map<mini::Object::SP, std::vector<SurfaceRef>> objects;
  std::map<mini::Mesh::SP, SurfaceRef> surfaces;
  std::map<mini::Material::SP, MaterialRef> materials;
  std::map<mini::Texture::SP, size_t> textureIds;

  std::map<std::string, size_t> dropped;
};

void MiniSceneImporter::drop(const std::string &what, size_t count)
{
  dropped[what] += count;
}

void MiniSceneImporter::logDropped() const
{
  for (auto &[what, count] : dropped)
    vsr::core::logWarning("[import_MINI] %s: %zu", what.c_str(), count);
}

// Instances //////////////////////////////////////////////////////////////////

void MiniSceneImporter::importInstances(
    const mini::Scene &in, LayerNodeRef root)
{
  // Instances of one object share its Surfaces. An object placed once gets a
  // transform node; one placed many times gets a single transform array node,
  // which keeps the layer tree small for scenes instanced millions of times.
  std::map<mini::Object::SP, std::vector<mat4>> placements;
  std::vector<mini::Object::SP> order;
  for (auto &inst : in.instances) {
    if (!inst || !inst->object) {
      drop("instances skipped (no object)");
      continue;
    }
    auto &xfms = placements[inst->object];
    if (xfms.empty())
      order.push_back(inst->object);
    xfms.push_back(fromMini(inst->xfm));
  }

  for (size_t i = 0; i < order.size(); i++) {
    auto &object = order[i];
    auto &objectSurfaces = surfacesFor(object);
    if (objectSurfaces.empty())
      continue;

    const auto name = "object" + std::to_string(i);
    auto &xfms = placements[object];

    LayerNodeRef node;
    if (xfms.size() == 1) {
      node = scene.insertChildTransformNode(root, xfms[0], name.c_str());
    } else {
      auto array = scene.createArray(ANARI_FLOAT32_MAT4, xfms.size());
      array->setData(xfms.data());
      node =
          scene.insertChildTransformArrayNode(root, array.data(), name.c_str());
    }

    for (auto &surface : objectSurfaces)
      scene.insertChildObjectNode(node, surface, surface->name().c_str());
  }
}

const std::vector<SurfaceRef> &MiniSceneImporter::surfacesFor(
    const mini::Object::SP &object)
{
  if (auto it = objects.find(object); it != objects.end())
    return it->second;

  auto &objectSurfaces = objects[object];
  for (auto &mesh : object->meshes) {
    if (auto surface = surfaceFor(mesh))
      objectSurfaces.push_back(surface);
  }
  return objectSurfaces;
}

// Geometry ///////////////////////////////////////////////////////////////////

SurfaceRef MiniSceneImporter::surfaceFor(const mini::Mesh::SP &mesh)
{
  // Null meshes are legal: a split scene keeps another node's meshes' slots.
  if (!mesh)
    return {};

  if (auto it = surfaces.find(mesh); it != surfaces.end())
    return it->second;

  auto &surface = surfaces[mesh];

  const size_t numVertices = mesh->vertices.size();
  const size_t numTriangles = mesh->indices.size();
  if (numVertices == 0 || numTriangles == 0) {
    drop("meshes skipped (no vertices or triangles)");
    return surface;
  }

  const bool indicesInRange = std::all_of(
      mesh->indices.begin(), mesh->indices.end(), [&](const mini::vec3i &t) {
        return t.x >= 0 && t.y >= 0 && t.z >= 0 && size_t(t.x) < numVertices
            && size_t(t.y) < numVertices && size_t(t.z) < numVertices;
      });
  if (!indicesInRange) {
    drop("meshes skipped (vertex index out of range)");
    return surface;
  }

  const auto name = "mesh" + std::to_string(surfaces.size() - 1);

  auto geom = scene.createObject<Geometry>(tokens::geometry::triangle);
  geom->setName(name.c_str());

  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, numVertices);
  positions->setData(mesh->vertices.data()); // vec3f is three packed floats
  geom->setParameterObject("vertex.position", *positions);

  auto index = scene.createArray(ANARI_UINT32_VEC3, numTriangles);
  index->setData(mesh->indices.data()); // in range, so non-negative
  geom->setParameterObject("primitive.index", *index);

  // One entry per vertex, or three per triangle for face-varying data, the
  // way hayStack tells the two apart. Texcoords go to attribute0 as stored:
  // they and the texture rows only have to agree with each other (ADR 0043).
  auto setAttribute = [&](const char *attribute,
                          anari::DataType type,
                          const void *data,
                          size_t count) {
    if (count == 0)
      return;
    std::string scope;
    if (count == numVertices)
      scope = "vertex.";
    else if (count == 3 * numTriangles)
      scope = "faceVarying.";
    else {
      drop(std::string(attribute)
          + "s dropped (count matches neither vertices nor triangle corners)");
      return;
    }
    auto array = scene.createArray(type, count);
    array->setData(data);
    geom->setParameterObject((scope + attribute).c_str(), *array);
  };

  setAttribute(
      "normal", ANARI_FLOAT32_VEC3, mesh->normals.data(), mesh->normals.size());
  setAttribute("attribute0",
      ANARI_FLOAT32_VEC2,
      mesh->texcoords.data(),
      mesh->texcoords.size());

  surface =
      scene.createSurface(name.c_str(), geom, materialFor(mesh->material));
  return surface;
}

// Textures ///////////////////////////////////////////////////////////////////

SamplerRef MiniSceneImporter::samplerFor(
    const mini::Texture::SP &texture, const char *slot)
{
  if (!texture)
    return {};

  anari::DataType elementType = ANARI_UNKNOWN;
  size_t texelSize = 0;
  switch (texture->format) {
  case mini::Texture::RGBA_UINT8:
    // miniScene has no sRGB flag, and hayStack uploads 8-bit texels as linear
    // (ADR 0042), so they are imported as linear too.
    elementType = ANARI_UFIXED8_VEC4;
    texelSize = 4;
    break;
  case mini::Texture::FLOAT4:
    elementType = ANARI_FLOAT32_VEC4;
    texelSize = 4 * sizeof(float);
    break;
  case mini::Texture::FLOAT1:
    elementType = ANARI_FLOAT32;
    texelSize = sizeof(float);
    break;
  case mini::Texture::EMBEDDED_PTEX:
    drop("textures dropped (embedded ptex)");
    return {};
  default:
    drop("textures dropped (unknown format)");
    return {};
  }

  const size_t width = size_t(std::max(texture->size.x, 0));
  const size_t height = size_t(std::max(texture->size.y, 0));
  if (width == 0 || height == 0
      || texture->data.size() < width * height * texelSize) {
    drop("textures dropped (size does not match data)");
    return {};
  }

  auto [it, inserted] = textureIds.try_emplace(texture, textureIds.size());
  const auto imageId = "texture" + std::to_string(it->second);

  // Rows are adopted as stored and declared top-down. A .mini file written by
  // Vela stores them top-down; one written by obj2mini stores them bottom-up
  // with v running up. Either way rows and texcoords agree, and passing both
  // through unchanged renders as hayStack renders them (ADR 0043).
  auto image =
      cache.acquireDecoded({sourcePrefix + imageId, ColorSpace::LINEAR},
          elementType,
          width,
          height,
          RowOrder::TOP_DOWN,
          texture->data.data());
  if (!image)
    return {};

  SamplerSettings settings;
  if (texture->filterMode == mini::Texture::FILTER_NEAREST)
    settings.filter = "nearest";

  return makeImageSampler(
      cache, image, std::string(slot) + ":" + imageId, settings);
}

// Materials //////////////////////////////////////////////////////////////////

void MiniSceneImporter::setParameter(Material &m,
    const char *name,
    float value,
    const mini::Texture::SP &texture)
{
  if (auto sampler = samplerFor(texture, name))
    m.setParameterObject(name, *sampler);
  else
    m.setParameter(name, value);
}

void MiniSceneImporter::setParameter(Material &m,
    const char *name,
    const mini::vec3f &value,
    const mini::Texture::SP &texture)
{
  if (auto sampler = samplerFor(texture, name))
    m.setParameterObject(name, *sampler);
  else
    m.setParameter(name, fromMini(value));
}

MaterialRef MiniSceneImporter::physicallyBased()
{
  return scene.createObject<Material>(tokens::material::physicallyBased);
}

MaterialRef MiniSceneImporter::materialFor(const mini::Material::SP &material)
{
  if (!material)
    return scene.defaultMaterial();

  if (auto it = materials.find(material); it != materials.end())
    return it->second;

  auto &result = materials[material];
  const auto name = "material" + std::to_string(materials.size() - 1);

  // Each kind is mapped the way hayStack maps it, so the scene looks here as
  // it does there; Disney emission and BlenderMaterial are the exceptions
  // (ADR 0043).
  if (auto anari = material->as<mini::ANARIMaterial>()) {
    result = importANARIMaterial(*anari);
  } else if (auto matte = material->as<mini::Matte>()) {
    result = scene.createObject<Material>(tokens::material::matte);
    result->setParameter("color", fromMini(matte->reflectance));
  } else if (auto disney = material->as<mini::DisneyMaterial>()) {
    result = importDisneyMaterial(*disney);
  } else if (auto blender = material->as<mini::BlenderMaterial>()) {
    result = importBlenderMaterial(*blender);
  } else if (auto metal = material->as<mini::Metal>()) {
    result = physicallyBased();
    result->setParameter("alphaMode", "blend");
    result->setParameter("baseColor", fromMini(metal->k) * float(1.0 / M_PI));
    result->setParameter("metallic", 1.f);
    result->setParameter("roughness", metal->roughness);
    result->setParameter("ior", metal->eta.x);
  } else if (auto paint = material->as<mini::MetallicPaint>()) {
    result = physicallyBased();
    result->setParameter("alphaMode", "blend");
    result->setParameter("baseColor", fromMini(paint->shadeColor));
    result->setParameter("metallic", 1.f);
    result->setParameter("roughness", paint->glitterSpread);
    result->setParameter("ior", 1.f / paint->eta);
    result->setParameter("specular", 0.f);
  } else if (auto plastic = material->as<mini::Plastic>()) {
    result = physicallyBased();
    result->setParameter("alphaMode", "blend");
    const auto &ks = plastic->Ks;
    const auto &pigment = plastic->pigmentColor;
    result->setParameter("baseColor",
        float3(std::min(ks.x, pigment.x),
            std::min(ks.y, pigment.y),
            std::min(ks.z, pigment.z)));
    result->setParameter("metallic", 0.f);
    result->setParameter("roughness", plastic->roughness);
    result->setParameter("ior", plastic->eta);
    result->setParameter("specular", 1.f);
    result->setParameter("specularColor", float3(1.f));
  } else if (auto dielectric = material->as<mini::Dielectric>()) {
    result = physicallyBased();
    result->setParameter("alphaMode", "blend");
    result->setParameter("metallic", 0.f);
    result->setParameter("roughness", 0.f);
    result->setParameter("transmission", 1.f);
    result->setParameter("ior", dielectric->etaInside);
    result->setParameter("specular", 1.f);
    result->setParameter("specularColor", float3(1.f));
  } else {
    drop("materials replaced by gray matte ('" + material->toString()
        + "' material)");
    result = scene.createObject<Material>(tokens::material::matte);
    result->setParameter("color", float3(.7f));
  }

  result->setName(name.c_str());
  return result;
}

MaterialRef MiniSceneImporter::importANARIMaterial(const mini::ANARIMaterial &m)
{
  auto r = physicallyBased();
  setParameter(*r, "baseColor", m.baseColor, m.baseColor_texture);
  setParameter(*r, "opacity", m.opacity, m.opacity_texture);
  setParameter(*r, "metallic", m.metallic, m.metallic_texture);
  setParameter(*r, "roughness", m.roughness, m.roughness_texture);
  // miniScene holds normal maps encoded and leaves the decode to its
  // renderer; ANARI wants the decoded normal from the sampler.
  if (auto s = samplerFor(m.normal_texture, "normal")) {
    setOutputTransform(*s, normalMapDecode());
    r->setParameterObject("normal", *s);
  }
  setParameter(*r, "emissive", m.emissive, m.emissive_texture);
  if (auto s = samplerFor(m.occlusion_texture, "occlusion"))
    r->setParameterObject("occlusion", *s);

  // Written as an int; hayStack passes the int on, VSR wants ANARI's string.
  switch (m.alphaMode) {
  case mini::ANARIMaterial::AM_OPAQUE:
    r->setParameter("alphaMode", "opaque");
    break;
  case mini::ANARIMaterial::AM_MASK:
    r->setParameter("alphaMode", "mask");
    break;
  default:
    r->setParameter("alphaMode", "blend");
    break;
  }
  r->setParameter("alphaCutoff", m.alphaCutoff);

  setParameter(*r, "specular", m.specular, m.specular_texture);
  setParameter(*r, "specularColor", m.specularColor, m.specularColor_texture);
  setParameter(*r, "clearcoat", m.clearcoat, m.clearcoat_texture);
  setParameter(*r,
      "clearcoatRoughness",
      m.clearcoatRoughness,
      m.clearcoatRoughness_texture);
  if (auto s = samplerFor(m.clearcoatNormal_texture, "clearcoatNormal")) {
    setOutputTransform(*s, normalMapDecode());
    r->setParameterObject("clearcoatNormal", *s);
  }
  setParameter(*r, "transmission", m.transmission, m.transmission_texture);
  setParameter(*r, "ior", m.ior, m.ior_texture);
  setParameter(*r, "thickness", m.thickness, m.thickness_texture);
  r->setParameter("attenuationDistance", m.attenuationDistance);
  setParameter(
      *r, "attenuationColor", m.attenuationColor, m.attenuationColor_texture);
  setParameter(*r, "sheenColor", m.sheenColor, m.sheenColor_texture);
  setParameter(
      *r, "sheenRoughness", m.sheenRoughness, m.sheenRoughness_texture);
  setParameter(*r, "iridescence", m.iridescence, m.iridescence_texture);
  setParameter(
      *r, "iridescenceIor", m.iridescenceIor, m.iridescenceIor_texture);
  setParameter(*r,
      "iridescenceThickness",
      m.iridescenceThickness,
      m.iridescenceThickness_texture);
  return r;
}

MaterialRef MiniSceneImporter::importDisneyMaterial(
    const mini::DisneyMaterial &m)
{
  auto r = physicallyBased();
  r->setParameter("alphaMode", "blend");
  setParameter(*r, "baseColor", m.baseColor, m.colorTexture);
  r->setParameter("metallic", m.metallic);
  r->setParameter("roughness", m.roughness);
  r->setParameter("opacity", 1.f - m.transmission);
  r->setParameter("ior", m.ior);
  // Disney's implicit specular of 0.5 is F0 = 0.04, which is ANARI
  // specular = 1 at ior 1.5.
  r->setParameter("specular", 1.f);
  r->setParameter("specularColor", float3(1.f));
  // hayStack ignores emission; it has an exact ANARI counterpart, so keep it.
  r->setParameter("emissive", fromMini(m.emission));
  if (m.alphaTexture)
    drop("Disney alpha textures dropped");
  return r;
}

MaterialRef MiniSceneImporter::importBlenderMaterial(
    const mini::BlenderMaterial &m)
{
  // hayStack renders these gray; the principled parameters that have an ANARI
  // counterpart carry over instead.
  auto r = physicallyBased();
  r->setParameter("alphaMode", "blend");
  setParameter(*r, "baseColor", m.baseColor, m.baseColorTexture);
  setParameter(*r, "opacity", m.alpha, m.alphaTexture);
  r->setParameter("metallic", m.metallic);
  r->setParameter("roughness", m.roughness);
  r->setParameter("ior", m.ior);
  r->setParameter("transmission", m.transmission);
  r->setParameter("clearcoat", m.clearcoat);
  r->setParameter("clearcoatRoughness", m.clearcoatRoughness);
  return r;
}

// Lights /////////////////////////////////////////////////////////////////////

void MiniSceneImporter::importLights(const mini::Scene &in, LayerNodeRef root)
{
  // Radiance splits into a color normalized to average 1 and a scalar
  // strength, so a light's strength is one parameter to edit.
  auto split = [](const float3 &radiance) {
    const float strength = average(radiance);
    return strength > 0.f ? std::make_pair(radiance / strength, strength)
                          : std::make_pair(float3(1.f), 0.f);
  };

  for (auto &dl : in.dirLights) {
    // hayStack sets irradiance = 2 * average(radiance) (ADR 0042).
    auto [color, strength] = split(fromMini(dl.radiance));
    auto [node, light] = scene.insertNewChildObjectNode<Light>(
        root, tokens::light::directional, "dirLight");
    light->setName("dirLight");
    light->setParameter("direction", fromMini(dl.direction));
    light->setParameter("color", color);
    light->setParameter("irradiance", 2.f * strength);
  }

  for (auto &ql : in.quadLights) {
    auto edge1 = fromMini(ql.edge0);
    auto edge2 = fromMini(ql.edge1);
    // ANARI quads emit toward cross(edge1, edge2); miniScene carries the
    // emitting side as its own normal.
    if (vsr::math::dot(vsr::math::cross(edge1, edge2), fromMini(ql.normal))
        < 0.f)
      std::swap(edge1, edge2);

    auto [color, strength] = split(fromMini(ql.emission));
    auto [node, light] = scene.insertNewChildObjectNode<Light>(
        root, tokens::light::quad, "quadLight");
    light->setName("quadLight");
    light->setParameter("position", fromMini(ql.corner));
    light->setParameter("edge1", edge1);
    light->setParameter("edge2", edge2);
    light->setParameter("color", color);
    light->setParameter("radiance", strength);
  }

  if (auto env = in.envMapLight) {
    auto tex = env->texture;
    if (!tex || tex->format != mini::Texture::FLOAT4 || tex->size.x <= 0
        || tex->size.y <= 0
        || tex->data.size()
            < size_t(tex->size.x) * size_t(tex->size.y) * sizeof(float4)) {
      drop("env-map lights skipped (texture is not a float4 image)");
      return;
    }

    // .mini env maps are top row first and an hdri light wants its radiance
    // bottom row first (ADR 0014), so flip -- what hayStack does on load.
    // Built without the Image Cache, as import_HDRI's radiance is.
    const size_t w = tex->size.x;
    const size_t h = tex->size.y;
    const auto *texels = reinterpret_cast<const float4 *>(tex->data.data());
    std::vector<float3> rgb(w * h);
    for (size_t y = 0; y < h; y++) {
      for (size_t x = 0; x < w; x++) {
        const auto &t = texels[(h - 1 - y) * w + x];
        rgb[y * w + x] = float3(t.x, t.y, t.z);
      }
    }
    auto radiance = scene.createArray(ANARI_FLOAT32_VEC3, w, h);
    radiance->setData(rgb.data());

    // hayStack reads the frame as up = vz and direction = -vx (ADR 0042).
    auto [node, light] = scene.insertNewChildObjectNode<Light>(
        root, tokens::light::hdri, "envMapLight");
    light->setName("envMapLight");
    light->setParameterObject("radiance", *radiance);
    light->setParameter(
        "up", vsr::math::normalize(fromMini(env->transform.l.vz)));
    light->setParameter(
        "direction", vsr::math::normalize(-fromMini(env->transform.l.vx)));
  }
}

} // namespace

void import_MINI(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filepath,
    LayerNodeRef location)
{
  (void)animMgr;

  mini::Scene::SP in;
  try {
    in = mini::Scene::load(filepath);
  } catch (const std::exception &e) {
    vsr::core::logError("[import_MINI] %s", e.what());
    return;
  }

  const auto filename = fileOf(filepath);
  auto root = scene.insertChildNode(
      location ? location : scene.defaultLayer()->root(), filename.c_str());

  MiniSceneImporter importer(scene, filename);
  importer.importInstances(*in, root);
  importer.importLights(*in, root);
  importer.logDropped();
}

} // namespace vsr::io

#else

namespace vsr::io {

void import_MINI(
    Scene &, vsr::animation::AnimationManager &, const char *, LayerNodeRef)
{
  vsr::core::logError(
      "[import_MINI] miniScene not enabled in VSR build "
      "(VSR_USE_MINISCENE=OFF)");
}

} // namespace vsr::io

#endif

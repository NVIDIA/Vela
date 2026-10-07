// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr
#include "vsr/scene/Scene.hpp"
#include "vsr/core/Logging.hpp"
#include "vsr/io/exporters.hpp"
#include "vsr/io/images/ImageCache.hpp"

#if VSR_USE_MINISCENE

#include "vsr/scene/objects/Array.hpp"
#include "vsr/scene/objects/Geometry.hpp"
#include "vsr/scene/objects/Light.hpp"
#include "vsr/scene/objects/Material.hpp"
#include "vsr/scene/objects/Sampler.hpp"
#include "vsr/scene/objects/Surface.hpp"
// anari
#include <anari/frontend/type_utility.h>
// miniScene -- kept to this file: its headers define unprefixed macros
// (NOTIMPLEMENTED, __both__, ...)
#include "miniScene/Scene.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsr::io {

using namespace vsr::scene;
using vsr::math::float2;
using vsr::math::float3;
using vsr::math::float4;
using vsr::math::mat4;

namespace {

// Conversion helpers /////////////////////////////////////////////////////////

mini::vec3f toMini(const float3 &v)
{
  return mini::vec3f(v.x, v.y, v.z);
}

// Both sides store column vectors: ANARI's mat4 columns are miniScene's
// linear-space axes, its fourth column the affine translation.
mini::affine3f toMini(const mat4 &m)
{
  mini::affine3f r;
  r.l.vx = mini::vec3f(m[0].x, m[0].y, m[0].z);
  r.l.vy = mini::vec3f(m[1].x, m[1].y, m[1].z);
  r.l.vz = mini::vec3f(m[2].x, m[2].y, m[2].z);
  r.p = mini::vec3f(m[3].x, m[3].y, m[3].z);
  return r;
}

float3 xfmPoint(const mat4 &m, const float3 &p)
{
  const auto r = vsr::math::mul(m, float4(p, 1.f));
  return float3(r.x, r.y, r.z);
}

float3 xfmVector(const mat4 &m, const float3 &v)
{
  const auto r = vsr::math::mul(m, float4(v, 0.f));
  return float3(r.x, r.y, r.z);
}

float halfToFloat(uint16_t h)
{
  const uint32_t sign = uint32_t(h & 0x8000) << 16;
  uint32_t exponent = (h >> 10) & 0x1f;
  uint32_t mantissa = h & 0x3ff;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa != 0) { // subnormal: renormalize
      exponent = 127 - 15 + 1;
      while ((mantissa & 0x400) == 0) {
        mantissa <<= 1;
        exponent--;
      }
      bits = sign | (exponent << 23) | ((mantissa & 0x3ff) << 13);
    } else {
      bits = sign;
    }
  } else if (exponent == 0x1f) {
    bits = sign | 0x7f800000 | (mantissa << 13);
  } else {
    bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
  }
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

// A parameter that is absent or disabled reads as unset, as it does when the
// render index hands the object to ANARI.
const Parameter *enabledParameter(const Object &o, const char *name)
{
  auto *p = o.parameter(name);
  return p && p->isEnabled() ? p : nullptr;
}

template <typename T>
T valueOr(const Object &o, const char *name, T fallback)
{
  auto *p = enabledParameter(o, name);
  return p && p->value().is<T>() ? p->value().get<T>() : fallback;
}

std::string stringOr(const Object &o, const char *name, std::string fallback)
{
  auto *p = enabledParameter(o, name);
  return p && p->value().type() == ANARI_STRING ? p->value().getString()
                                                : fallback;
}

const Array *hostArrayParameter(const Object &o, const char *name)
{
  if (!enabledParameter(o, name))
    return nullptr;
  return o.parameterValueAsObject<Array>(name);
}

// Texel access: every supported element type read as a normalized float4,
// with missing channels filled as ANARI samplers fill them, (x, 0, 0, 1).
bool isUfixed8(anari::DataType t)
{
  switch (t) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    return true;
  default:
    return false;
  }
}

bool isUfixed16(anari::DataType t)
{
  return t == ANARI_UFIXED16 || t == ANARI_UFIXED16_VEC2
      || t == ANARI_UFIXED16_VEC3 || t == ANARI_UFIXED16_VEC4;
}

bool isFloat16(anari::DataType t)
{
  return t == ANARI_FLOAT16 || t == ANARI_FLOAT16_VEC2
      || t == ANARI_FLOAT16_VEC3 || t == ANARI_FLOAT16_VEC4;
}

bool isFloat32(anari::DataType t)
{
  return t == ANARI_FLOAT32 || t == ANARI_FLOAT32_VEC2
      || t == ANARI_FLOAT32_VEC3 || t == ANARI_FLOAT32_VEC4;
}

float4 fetchTexel(const Array &image, size_t i)
{
  const auto type = image.elementType();
  const size_t components = anari::componentsOf(type);
  const auto *texel =
      static_cast<const uint8_t *>(image.data()) + i * image.elementSize();

  float c[4] = {0.f, 0.f, 0.f, 1.f};
  for (size_t k = 0; k < components && k < 4; k++) {
    if (isUfixed8(type))
      c[k] = texel[k] / 255.f;
    else if (isUfixed16(type))
      c[k] = reinterpret_cast<const uint16_t *>(texel)[k] / 65535.f;
    else if (isFloat16(type))
      c[k] = halfToFloat(reinterpret_cast<const uint16_t *>(texel)[k]);
    else
      c[k] = reinterpret_cast<const float *>(texel)[k];
  }
  return float4(c[0], c[1], c[2], c[3]);
}

// Export state ///////////////////////////////////////////////////////////////

struct MiniSceneExporter : public LayerVisitor
{
  MiniSceneExporter() : out(mini::Scene::create())
  {
    xfms.push(vsr::math::IDENTITY_MAT4);
    groups.emplace();
  }

  bool preChildren_const(const LayerNode &n, int level) override;
  void postChildren_const(const LayerNode &n, int level) override;

  void logDropped() const;

  mini::Scene::SP out;

 private:
  // Objects placed by the same transform (or transform array) node; mirrors
  // RenderToAnariObjectsVisitor's grouping so each group is one ANARI
  // instance in the renderer and one mini::Instance per placement here.
  struct Group
  {
    std::vector<const Surface *> surfaces;
    std::vector<const Light *> lights;
  };

  void drop(const std::string &what, size_t count = 1);

  void emitGroup(const Group &g, const std::vector<mat4> &placements);
  void emitLight(const Light &l, const mat4 &xfm);

  mini::Object::SP objectFor(std::vector<const Surface *> surfaces);
  mini::Mesh::SP meshFor(const Surface &s);
  mini::Material::SP materialFor(const Material *m);
  mini::Texture::SP textureFor(
      const Sampler &s, const OutputTransform &implied = {});
  mini::Texture::SP convertImage(const Array &image, bool flipRows);

  void readParameter(const Material &m,
      const char *name,
      float &value,
      mini::Texture::SP *texture = nullptr);
  void readParameter(const Material &m,
      const char *name,
      mini::vec3f &value,
      mini::Texture::SP *texture = nullptr);
  bool readSampledParameter(const Material &m,
      const char *name,
      mini::Texture::SP *texture,
      const OutputTransform &implied = {});

  std::stack<mat4> xfms;
  std::stack<Group> groups;
  std::stack<const Array *> xfmArrays;

  std::map<std::vector<const Surface *>, mini::Object::SP> objects;
  std::unordered_map<const Surface *, mini::Mesh::SP> meshes;
  std::unordered_map<const Material *, mini::Material::SP> materials;
  std::unordered_map<const Sampler *, mini::Texture::SP> textures;

  std::map<std::string, size_t> dropped;
};

void MiniSceneExporter::drop(const std::string &what, size_t count)
{
  dropped[what] += count;
}

void MiniSceneExporter::logDropped() const
{
  for (auto &[what, count] : dropped)
    vsr::core::logWarning("[export_miniScene] %s: %zu", what.c_str(), count);
}

// Traversal //////////////////////////////////////////////////////////////////

bool MiniSceneExporter::preChildren_const(const LayerNode &n, int level)
{
  if (!n->isEnabled())
    return false;

  switch (n->type()) {
  case ANARI_SURFACE:
    if (auto *s = n->getObject())
      groups.top().surfaces.push_back(static_cast<const Surface *>(s));
    break;
  case ANARI_LIGHT:
    if (auto *l = n->getObject())
      groups.top().lights.push_back(static_cast<const Light *>(l));
    break;
  case ANARI_VOLUME:
    if (n->getObject())
      drop("volumes skipped (miniScene has no volumes)");
    break;
  case ANARI_CAMERA:
    if (n->getObject())
      drop("cameras skipped (miniScene has no cameras)");
    break;
  case ANARI_FLOAT32_MAT4:
    xfms.push(vsr::math::mul(xfms.top(), n->getTransform()));
    groups.emplace();
    break;
  case ANARI_ARRAY1D:
    if (auto *a = n->getTransformArray(); a) {
      xfmArrays.push(a);
      groups.emplace();
    }
    break;
  default:
    break;
  }

  return true;
}

void MiniSceneExporter::postChildren_const(const LayerNode &n, int level)
{
  if (!n->isEnabled())
    return;

  switch (n->type()) {
  case ANARI_FLOAT32_MAT4:
    emitGroup(groups.top(), {xfms.top()});
    groups.pop();
    xfms.pop();
    break;
  case ANARI_ARRAY1D: {
    if (!n->getTransformArray())
      break;
    const auto *a = xfmArrays.top();
    std::vector<mat4> placements;
    if (a->isHost() && a->data()) {
      const auto *m = a->dataAs<mat4>();
      placements.reserve(a->size());
      for (size_t i = 0; i < a->size(); i++)
        placements.push_back(vsr::math::mul(xfms.top(), m[i]));
    } else {
      drop("transform arrays skipped (not in host memory)");
    }
    emitGroup(groups.top(), placements);
    groups.pop();
    xfmArrays.pop();
    break;
  }
  default:
    break;
  }
}

void MiniSceneExporter::emitGroup(
    const Group &g, const std::vector<mat4> &placements)
{
  if (!g.surfaces.empty()) {
    if (auto object = objectFor(g.surfaces)) {
      for (auto &xfm : placements)
        out->instances.push_back(mini::Instance::create(object, toMini(xfm)));
    }
  }

  for (auto &xfm : placements) {
    for (auto *l : g.lights)
      emitLight(*l, xfm);
  }
}

// Geometry ///////////////////////////////////////////////////////////////////

mini::Object::SP MiniSceneExporter::objectFor(
    std::vector<const Surface *> surfaces)
{
  std::sort(surfaces.begin(), surfaces.end());
  if (auto it = objects.find(surfaces); it != objects.end())
    return it->second;

  std::vector<mini::Mesh::SP> groupMeshes;
  for (auto *s : surfaces) {
    if (auto mesh = meshFor(*s))
      groupMeshes.push_back(mesh);
  }

  auto object =
      groupMeshes.empty() ? nullptr : mini::Object::create(groupMeshes);
  objects[surfaces] = object;
  return object;
}

mini::Mesh::SP MiniSceneExporter::meshFor(const Surface &s)
{
  if (auto it = meshes.find(&s); it != meshes.end())
    return it->second;

  auto &mesh = meshes[&s];

  auto *geom = s.parameterValueAsObject<Geometry>(tokens::surface::geometry);
  if (!geom) {
    drop("surfaces skipped (no geometry)");
    return mesh;
  }

  const bool isQuad = geom->subtype() == tokens::geometry::quad;
  if (!isQuad && geom->subtype() != tokens::geometry::triangle) {
    drop("surfaces skipped ('" + std::string(geom->subtype().c_str())
        + "' geometry; miniScene holds triangles only)");
    return mesh;
  }

  const auto *positions = hostArrayParameter(*geom, "vertex.position");
  if (!positions || !positions->isHost() || !positions->data()
      || positions->elementType() != ANARI_FLOAT32_VEC3) {
    drop(
        "surfaces skipped (vertex.position missing, not float3, or not in "
        "host memory)");
    return mesh;
  }

  // Vertex indices of each primitive's corners, 3 or 4 per primitive.
  const size_t cornersPerPrim = isQuad ? 4 : 3;
  const size_t numVertices = positions->size();
  std::vector<uint64_t> corners;

  if (const auto *index = hostArrayParameter(*geom, "primitive.index")) {
    const auto type = index->elementType();
    const bool readable = index->isHost() && index->data()
        && anari::componentsOf(type) == cornersPerPrim
        && (type == ANARI_UINT32_VEC3 || type == ANARI_UINT32_VEC4
            || type == ANARI_INT32_VEC3 || type == ANARI_INT32_VEC4
            || type == ANARI_UINT64_VEC3 || type == ANARI_UINT64_VEC4);
    if (!readable) {
      drop("surfaces skipped (unsupported primitive.index)");
      return mesh;
    }
    const size_t n = index->size() * cornersPerPrim;
    corners.resize(n);
    if (anari::sizeOf(type) == cornersPerPrim * sizeof(uint64_t)) {
      const auto *src = static_cast<const uint64_t *>(index->data());
      std::copy(src, src + n, corners.begin());
    } else {
      const auto *src = static_cast<const uint32_t *>(index->data());
      std::copy(src, src + n, corners.begin());
    }
  } else {
    corners.resize(numVertices - numVertices % cornersPerPrim);
    for (size_t i = 0; i < corners.size(); i++)
      corners[i] = i;
  }

  // Triangles as positions into 'corners'; a quad (0,1,2,3) splits into
  // (0,1,2) and (0,2,3).
  std::vector<size_t> triCorners;
  const size_t numPrims = corners.size() / cornersPerPrim;
  triCorners.reserve(numPrims * (isQuad ? 6 : 3));
  for (size_t p = 0; p < numPrims; p++) {
    const size_t c = p * cornersPerPrim;
    if (isQuad) {
      for (size_t k : {0, 1, 2, 0, 2, 3})
        triCorners.push_back(c + k);
    } else {
      for (size_t k : {0, 1, 2})
        triCorners.push_back(c + k);
    }
  }

  auto m = mini::Mesh::create(materialFor(
      s.parameterValueAsObject<Material>(tokens::surface::material)));

  const auto *p = positions->dataAs<float3>();
  m->vertices.reserve(numVertices);
  for (size_t i = 0; i < numVertices; i++)
    m->vertices.push_back(toMini(p[i]));

  m->indices.reserve(triCorners.size() / 3);
  for (size_t t = 0; t < triCorners.size(); t += 3) {
    m->indices.push_back(mini::vec3i(int(corners[triCorners[t]]),
        int(corners[triCorners[t + 1]]),
        int(corners[triCorners[t + 2]])));
  }

  // Per-vertex data copies over as is; face-varying data is re-expanded to
  // miniScene's three-per-triangle layout through the quad split.
  auto readAttribute = [&](const char *vertexName,
                           const char *faceVaryingName,
                           anari::DataType type,
                           auto &dst,
                           auto convert) {
    if (const auto *a = hostArrayParameter(*geom, faceVaryingName); a
        && a->isHost() && a->data() && a->elementType() == type
        && a->size() >= corners.size()) {
      for (size_t c : triCorners)
        dst.push_back(convert(
            static_cast<const uint8_t *>(a->data()) + c * a->elementSize()));
    } else if (const auto *a = hostArrayParameter(*geom, vertexName); a
               && a->isHost() && a->data() && a->elementType() == type
               && a->size() == numVertices) {
      for (size_t i = 0; i < numVertices; i++)
        dst.push_back(convert(
            static_cast<const uint8_t *>(a->data()) + i * a->elementSize()));
    }
  };

  readAttribute("vertex.normal",
      "faceVarying.normal",
      ANARI_FLOAT32_VEC3,
      m->normals,
      [](const uint8_t *v) {
        auto *f = reinterpret_cast<const float *>(v);
        return mini::vec3f(f[0], f[1], f[2]);
      });
  readAttribute("vertex.attribute0",
      "faceVarying.attribute0",
      ANARI_FLOAT32_VEC2,
      m->texcoords,
      [](const uint8_t *v) {
        auto *f = reinterpret_cast<const float *>(v);
        return mini::vec2f(f[0], f[1]);
      });

  for (const char *scope : {"vertex.", "faceVarying.", "primitive."}) {
    for (const char *attr :
        {"color", "attribute0", "attribute1", "attribute2", "attribute3"}) {
      const std::string name = std::string(scope) + attr;
      if (!hostArrayParameter(*geom, name.c_str()))
        continue;
      const bool exportedAsTexcoords = m->texcoords.size() > 0
          && std::string(attr) == "attribute0"
          && std::string(scope) != "primitive.";
      if (!exportedAsTexcoords)
        drop(
            "geometry attributes dropped (miniScene has normals and "
            "texcoords only)");
    }
  }

  mesh = m;
  return mesh;
}

// Materials //////////////////////////////////////////////////////////////////

// `implied` is the output transform the miniScene slot stands for already,
// which a sampler carrying it loses nothing by dropping.
bool MiniSceneExporter::readSampledParameter(const Material &m,
    const char *name,
    mini::Texture::SP *texture,
    const OutputTransform &implied)
{
  auto *p = enabledParameter(m, name);
  if (!p)
    return false;

  if (p->value().type() == ANARI_STRING) {
    drop("material parameters bound to geometry attributes (default used)");
    return true;
  }

  if (p->value().type() == ANARI_SAMPLER) {
    if (auto *s = m.parameterValueAsObject<Sampler>(name)) {
      if (texture)
        *texture = textureFor(*s, implied);
      else
        drop("textured material parameters with no miniScene texture slot");
    }
    return true;
  }

  return false;
}

void MiniSceneExporter::readParameter(const Material &m,
    const char *name,
    float &value,
    mini::Texture::SP *texture)
{
  if (!readSampledParameter(m, name, texture))
    value = valueOr<float>(m, name, value);
}

void MiniSceneExporter::readParameter(const Material &m,
    const char *name,
    mini::vec3f &value,
    mini::Texture::SP *texture)
{
  if (!readSampledParameter(m, name, texture))
    value = toMini(valueOr<float3>(m, name, float3(value.x, value.y, value.z)));
}

mini::Material::SP MiniSceneExporter::materialFor(const Material *m)
{
  if (!m)
    return nullptr; // miniScene's default material

  if (auto it = materials.find(m); it != materials.end())
    return it->second;

  auto &material = materials[m];

  auto alphaMode = [&]() {
    const auto mode = stringOr(*m, "alphaMode", "blend");
    if (mode == "opaque")
      return int(mini::ANARIMaterial::AM_OPAQUE);
    if (mode == "mask")
      return int(mini::ANARIMaterial::AM_MASK);
    return int(mini::ANARIMaterial::AM_BLEND);
  };

  if (m->subtype() == tokens::material::matte) {
    // mini::Matte has neither textures nor opacity; a matte material using
    // either becomes the equivalent non-metallic, fully rough ANARIMaterial.
    auto *color = enabledParameter(*m, "color");
    auto *opacity = enabledParameter(*m, "opacity");
    const bool plain = (!color || color->value().is<float3>())
        && (!opacity
            || (opacity->value().is<float>()
                && opacity->value().get<float>() >= 1.f));
    if (plain) {
      auto matte = mini::Matte::create();
      readParameter(*m, "color", matte->reflectance);
      material = matte;
    } else {
      auto pbr = mini::ANARIMaterial::create();
      pbr->metallic = 0.f;
      pbr->roughness = 1.f;
      pbr->baseColor = mini::vec3f(1.f, 0.f, 0.f); // VSR's matte default
      readParameter(*m, "color", pbr->baseColor, &pbr->baseColor_texture);
      readParameter(*m, "opacity", pbr->opacity, &pbr->opacity_texture);
      pbr->alphaMode = alphaMode();
      pbr->alphaCutoff = valueOr<float>(*m, "alphaCutoff", pbr->alphaCutoff);
      material = pbr;
    }
  } else if (m->subtype() == tokens::material::physicallyBased) {
    auto pbr = mini::ANARIMaterial::create();
    readParameter(*m, "baseColor", pbr->baseColor, &pbr->baseColor_texture);
    readParameter(*m, "opacity", pbr->opacity, &pbr->opacity_texture);
    readParameter(*m, "metallic", pbr->metallic, &pbr->metallic_texture);
    readParameter(*m, "roughness", pbr->roughness, &pbr->roughness_texture);
    // miniScene stores normal maps encoded and its renderer decodes them, so
    // the ANARI decode is what its normal slots mean.
    readSampledParameter(*m, "normal", &pbr->normal_texture, normalMapDecode());
    readParameter(*m, "emissive", pbr->emissive, &pbr->emissive_texture);
    readSampledParameter(*m, "occlusion", &pbr->occlusion_texture);
    pbr->alphaMode = alphaMode();
    pbr->alphaCutoff = valueOr<float>(*m, "alphaCutoff", pbr->alphaCutoff);
    readParameter(*m, "specular", pbr->specular, &pbr->specular_texture);
    readParameter(
        *m, "specularColor", pbr->specularColor, &pbr->specularColor_texture);
    readParameter(*m, "clearcoat", pbr->clearcoat, &pbr->clearcoat_texture);
    readParameter(*m,
        "clearcoatRoughness",
        pbr->clearcoatRoughness,
        &pbr->clearcoatRoughness_texture);
    readSampledParameter(*m,
        "clearcoatNormal",
        &pbr->clearcoatNormal_texture,
        normalMapDecode());
    readParameter(
        *m, "transmission", pbr->transmission, &pbr->transmission_texture);
    readParameter(*m, "ior", pbr->ior, &pbr->ior_texture);
    readParameter(*m, "thickness", pbr->thickness, &pbr->thickness_texture);
    readParameter(*m, "attenuationDistance", pbr->attenuationDistance);
    readParameter(*m,
        "attenuationColor",
        pbr->attenuationColor,
        &pbr->attenuationColor_texture);
    readParameter(*m, "sheenColor", pbr->sheenColor, &pbr->sheenColor_texture);
    readParameter(*m,
        "sheenRoughness",
        pbr->sheenRoughness,
        &pbr->sheenRoughness_texture);
    readParameter(
        *m, "iridescence", pbr->iridescence, &pbr->iridescence_texture);
    readParameter(*m,
        "iridescenceIor",
        pbr->iridescenceIor,
        &pbr->iridescenceIor_texture);
    readParameter(*m,
        "iridescenceThickness",
        pbr->iridescenceThickness,
        &pbr->iridescenceThickness_texture);
    material = pbr;
  } else {
    drop("materials replaced by miniScene's default ('"
        + std::string(m->subtype().c_str()) + "' material)");
  }

  return material;
}

// Textures ///////////////////////////////////////////////////////////////////

mini::Texture::SP MiniSceneExporter::textureFor(
    const Sampler &s, const OutputTransform &implied)
{
  if (auto it = textures.find(&s); it != textures.end())
    return it->second;

  auto &texture = textures[&s];

  if (s.subtype() != tokens::sampler::image2D) {
    drop("textures dropped ('" + std::string(s.subtype().c_str())
        + "' sampler; only image2D is supported)");
    return texture;
  }

  if (stringOr(s, "inAttribute", "attribute0") != "attribute0") {
    drop(
        "textures dropped (sampler reads texcoords from other than "
        "attribute0)");
    return texture;
  }

  const auto identity = vsr::math::IDENTITY_MAT4;
  if (valueOr<mat4>(s, "inTransform", identity) != identity
      || valueOr<float4>(s, "inOffset", float4(0.f)) != float4(0.f)
      || outputTransformOf(s) != implied) {
    drop("texture transforms ignored (miniScene has none)");
  }

  const auto *image = hostArrayParameter(s, "image");
  if (!image) {
    drop("textures dropped (sampler has no image)");
    return texture;
  }

  // Rows and texcoords go out as VSR holds them -- top row first, v running
  // down the image (ADR 0014). They only have to agree with each other, and
  // ANARI consumers sample them as given (ADR 0042).
  texture = convertImage(*image, false);
  if (texture && stringOr(s, "filter", "linear") == "nearest")
    texture->filterMode = mini::Texture::FILTER_NEAREST;
  return texture;
}

mini::Texture::SP MiniSceneExporter::convertImage(
    const Array &image, bool flipRows)
{
  const auto type = image.elementType();
  if (!image.isHost() || !image.data()) {
    drop("textures dropped (image not in host memory)");
    return nullptr;
  }
  if (!isUfixed8(type) && !isUfixed16(type) && !isFloat16(type)
      && !isFloat32(type)) {
    drop("textures dropped (unsupported texel type)");
    return nullptr;
  }

  const size_t width = image.dim(0);
  const size_t height = std::max<size_t>(image.dim(1), 1);

  auto tex = mini::Texture::create();
  tex->size = mini::vec2i(int(width), int(height));

  // 8-bit images stay 8-bit (bytes copied unchanged, sRGB or not, as
  // miniScene's own OBJ importer does); single-channel images become FLOAT1;
  // everything else FLOAT4.
  if (isUfixed8(type)) {
    tex->format = mini::Texture::RGBA_UINT8;
    tex->data.resize(width * height * 4);
  } else if (anari::componentsOf(type) == 1) {
    tex->format = mini::Texture::FLOAT1;
    tex->data.resize(width * height * sizeof(float));
  } else {
    tex->format = mini::Texture::FLOAT4;
    tex->data.resize(width * height * 4 * sizeof(float));
  }

  const auto *src = static_cast<const uint8_t *>(image.data());
  const size_t components = anari::componentsOf(type);
  for (size_t y = 0; y < height; y++) {
    const size_t srcRow = flipRows ? height - 1 - y : y;
    for (size_t x = 0; x < width; x++) {
      const size_t s = srcRow * width + x;
      const size_t d = y * width + x;
      if (tex->format == mini::Texture::RGBA_UINT8) {
        const uint8_t *texel = src + s * image.elementSize();
        uint8_t *out = tex->data.data() + d * 4;
        for (size_t k = 0; k < 4; k++)
          out[k] = k < components ? texel[k] : (k == 3 ? 255 : 0);
      } else if (tex->format == mini::Texture::FLOAT1) {
        reinterpret_cast<float *>(tex->data.data())[d] = fetchTexel(image, s).x;
      } else {
        const auto t = fetchTexel(image, s);
        auto *out = reinterpret_cast<float *>(tex->data.data()) + d * 4;
        out[0] = t.x;
        out[1] = t.y;
        out[2] = t.z;
        out[3] = t.w;
      }
    }
  }

  return tex;
}

// Lights /////////////////////////////////////////////////////////////////////

void MiniSceneExporter::emitLight(const Light &l, const mat4 &xfm)
{
  const auto color = valueOr<float3>(l, "color", float3(1.f));

  if (l.subtype() == tokens::light::directional) {
    float3 direction(0.f, 0.f, -1.f);
    if (auto *p = enabledParameter(l, "direction")) {
      if (p->value().is<float2>()) // azimuth/elevation, as the UI edits it
        direction = vsr::math::azelToDir(p->value().get<float2>());
      else if (p->value().is<float3>())
        direction = p->value().get<float3>();
    }
    const float irradiance = valueOr<float>(l, "irradiance", 1.f);

    // hayStack turns a DirLight into an ANARI directional light with
    // irradiance = 2 * average(radiance); halve here so the light arrives
    // with the irradiance it has in VSR (ADR 0042).
    mini::DirLight light;
    light.direction = toMini(vsr::math::normalize(xfmVector(xfm, direction)));
    light.radiance = toMini(color * irradiance * 0.5f);
    out->dirLights.push_back(light);
  } else if (l.subtype() == tokens::light::quad) {
    auto position = xfmPoint(xfm, valueOr<float3>(l, "position", float3(0.f)));
    auto edge1 =
        xfmVector(xfm, valueOr<float3>(l, "edge1", float3(1.f, 0.f, 0.f)));
    auto edge2 =
        xfmVector(xfm, valueOr<float3>(l, "edge2", float3(0.f, 1.f, 0.f)));

    const auto side = stringOr(l, "side", "front");
    if (side == "back")
      std::swap(edge1, edge2);
    else if (side == "both")
      drop("two-sided quad lights exported front side only");

    const auto n = vsr::math::cross(edge1, edge2);
    const float area = vsr::math::length(n);
    if (area <= 0.f) {
      drop("quad lights skipped (zero area)");
      return;
    }

    // ANARI 'radiance' takes precedence over 'intensity' (W/sr), which is
    // radiance integrated over the light's area.
    float radiance = 0.f;
    if (auto *p = enabledParameter(l, "radiance"); p && p->value().is<float>())
      radiance = p->value().get<float>();
    else
      radiance = valueOr<float>(l, "intensity", 1.f) / area;

    mini::QuadLight light;
    light.corner = toMini(position);
    light.edge0 = toMini(edge1);
    light.edge1 = toMini(edge2);
    light.emission = toMini(color * radiance);
    light.normal = toMini(n / area);
    light.area = area;
    out->quadLights.push_back(light);
  } else if (l.subtype() == tokens::light::hdri) {
    if (out->envMapLight) {
      drop("hdri lights skipped (miniScene holds one environment map)");
      return;
    }

    const auto *radiance = hostArrayParameter(l, "radiance");
    if (!radiance) {
      drop("hdri lights skipped (no radiance image)");
      return;
    }

    // An hdri light's radiance is held bottom row first (ADR 0014), but
    // .mini env maps are top row first: miniScene's addEnvLight writes them
    // that way and hayStack flips every one on load (ADR 0042).
    auto texture = convertImage(*radiance, true);
    if (!texture)
      return;
    if (texture->format != mini::Texture::FLOAT4) {
      drop("hdri lights skipped (radiance must be a float3/float4 image)");
      return;
    }

    const float scale = valueOr<float>(l, "scale", 1.f);
    auto *texels = reinterpret_cast<float *>(texture->data.data());
    const size_t numTexels = texture->data.size() / (4 * sizeof(float));
    for (size_t i = 0; i < numTexels; i++) {
      texels[i * 4 + 0] *= scale;
      texels[i * 4 + 1] *= scale;
      texels[i * 4 + 2] *= scale;
      texels[i * 4 + 3] = 0.f; // as addEnvLight writes it
    }

    // hayStack reads an EnvMapLight frame back as up = vz and
    // direction = -vx.
    const auto up = vsr::math::normalize(
        xfmVector(xfm, valueOr<float3>(l, "up", float3(0.f, 1.f, 0.f))));
    const auto direction =
        xfmVector(xfm, valueOr<float3>(l, "direction", float3(1.f, 0.f, 0.f)));
    const auto vx =
        vsr::math::normalize(-(direction - vsr::math::dot(direction, up) * up));

    auto env = mini::EnvMapLight::create();
    env->texture = texture;
    env->transform.l.vz = toMini(up);
    env->transform.l.vx = toMini(vx);
    env->transform.l.vy = toMini(vsr::math::cross(up, vx));
    out->envMapLight = env;
  } else {
    drop("lights skipped ('" + std::string(l.subtype().c_str())
        + "' light; miniScene has directional, quad, and hdri only)");
  }
}

} // namespace

bool export_SceneToMiniScene(const Scene &scene, const char *filename)
{
  vsr::core::logStatus("[export_miniScene] writing '%s'", filename);

  MiniSceneExporter exporter;
  for (const auto *layer : scene.getActiveLayers())
    layer->traverse_const(layer->root(), exporter);
  exporter.logDropped();

  try {
    exporter.out->save(filename);
  } catch (const std::exception &e) {
    vsr::core::logError("[export_miniScene] %s", e.what());
    return false;
  }

  vsr::core::logStatus(
      "[export_miniScene] wrote %zu instances", exporter.out->instances.size());
  return true;
}

} // namespace vsr::io

#else

namespace vsr::io {

bool export_SceneToMiniScene(const scene::Scene &, const char *)
{
  vsr::core::logError(
      "[export_miniScene] miniScene not enabled in VSR build "
      "(VSR_USE_MINISCENE=OFF)");
  return false;
}

} // namespace vsr::io

#endif

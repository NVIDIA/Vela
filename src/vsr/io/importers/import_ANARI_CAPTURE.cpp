// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/importers.hpp"
#include "vsr/io/importers/detail/AnariCaptureText.hpp"
// std
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <unordered_map>

namespace vsr::io {

using namespace vsr::scene;
namespace cap = detail::anari_capture;

namespace {

// Helpers ////////////////////////////////////////////////////////////////////

// How an object array writes an element that refers to no object.
constexpr const char *NULL_ID = "null";

std::optional<std::string> readFile(const std::filesystem::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return {};
  return std::string(
      std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The scene object type an object block's label names, or ANARI_UNKNOWN for
// the labels that do not become scene objects (frame, renderer, world,
// instance, group, arrays).
anari::DataType sceneObjectType(const std::string &label)
{
  if (label == "camera")
    return ANARI_CAMERA;
  if (label == "surface")
    return ANARI_SURFACE;
  if (label == "geometry")
    return ANARI_GEOMETRY;
  if (label == "material")
    return ANARI_MATERIAL;
  if (label == "sampler")
    return ANARI_SAMPLER;
  if (label == "spatialField")
    return ANARI_SPATIAL_FIELD;
  if (label == "volume")
    return ANARI_VOLUME;
  if (label == "light")
    return ANARI_LIGHT;
  return ANARI_UNKNOWN;
}

// The world and group parameters that become object nodes.
bool isPlacedContent(const std::string &name)
{
  return name == "surface" || name == "volume" || name == "light";
}

// "name TYPE value", as the capture wrote the line.
std::string entryText(const cap::Entry &e)
{
  std::string s = e.name + ' ' + e.type + ' ';
  if (!e.isList)
    return s + e.value;
  s += '[';
  for (size_t i = 0; i < e.items.size(); i++)
    s += (i ? ", " : "") + e.items[i];
  return s + ']';
}

std::string join(const std::string &path, const std::string &name)
{
  return path.empty() ? name : path + '.' + name;
}

std::string indexed(const std::string &path, size_t i)
{
  return path + '[' + std::to_string(i) + ']';
}

// A non-object parameter value as an Any, or empty with `reason` set when the
// capture holds no value VSR can keep.
std::optional<Any> decodeParameterValue(
    anari::DataType type, const cap::Entry &e, std::string &reason)
{
  if (e.value == "<opaque>") {
    reason = e.type + " values are not recorded";
    return {};
  }
  if (e.isList) {
    reason = e.type + " values are not represented";
    return {};
  }
  if (type == ANARI_STRING) {
    auto s = cap::unquote(e.value);
    if (!s) {
      reason = "unreadable string " + e.value;
      return {};
    }
    return Any(*s);
  }
  std::vector<std::byte> bytes(anari::sizeOf(type));
  if (!cap::decodeValue(type, e.value, bytes.data())) {
    reason = "'" + e.value + "' is not a " + e.type;
    return {};
  }
  return Any(type, bytes.data());
}

// Importer ///////////////////////////////////////////////////////////////////

// The elements of one captured array, decoded but not yet in the scene.
struct ArrayContents
{
  anari::DataType elementType{ANARI_UNKNOWN};
  size_t dims[3]{0, 0, 0};
  size_t count{0};
  std::vector<std::byte> bytes; // numeric elements
  std::vector<std::string> ids; // object elements, NULL_ID for none
};

/*
 * Builds VSR scene content from one parsed ANARI Capture, following
 * references from the frame. Every capture ID becomes at most one VSR object,
 * which is what makes instances of one group share their objects.
 */
struct CaptureImporter
{
  CaptureImporter(Scene *scene,
      const cap::CaptureText *capture,
      std::filesystem::path blobDir,
      AnariCaptureImportReport *report);

  void importFrame(LayerNodeRef fileNode);

 private:
  void drop(const std::string &path, std::string reason);
  const cap::Block *block(const std::string &id) const;

  // Arrays //
  std::optional<ArrayContents> readArray(
      const cap::Block &b, std::string &reason) const;
  std::optional<ArrayContents> readArrayAt(
      const std::string &id, std::string &reason) const;
  Array *importArray(const std::string &id, const std::string &path);

  // Scene objects //
  Object *fail(
      const std::string &id, const std::string &path, std::string reason);
  Object *importObject(const std::string &id, const std::string &path);
  void setParameters(Object &o, const cap::Block &b, const std::string &path);
  bool setParameter(Object &o, const cap::Entry &e, const std::string &path);

  // Instancing //
  void placeContent(
      const cap::Entry &e, LayerNodeRef node, const std::string &path);
  void importWorld(const cap::Block &world, LayerNodeRef fileNode);
  void importInstances(
      const cap::Entry &e, LayerNodeRef fileNode, const std::string &path);
  void importInstance(
      const cap::Block &inst, LayerNodeRef fileNode, const std::string &path);
  LayerNodeRef placeInstance(const cap::Block &inst,
      LayerNodeRef fileNode,
      const std::string &name,
      const std::string &path,
      std::set<std::string> &consumed);
  template <typename T>
  std::optional<T> firstElement(const cap::Entry *e, anari::DataType type);
  void setInstanceParameter(
      LayerNodeData &node, const cap::Entry &e, const std::string &path);

  // Frame //
  void reportRenderer(const std::string &id);

  Scene *m_scene{nullptr};
  const cap::CaptureText *m_capture{nullptr};
  std::filesystem::path m_blobDir;
  AnariCaptureImportReport *m_report{nullptr};

  // Every scene object and array made so far, by capture ID; an ID that
  // failed maps to the reason, so a second reference reports it again.
  std::unordered_map<std::string, Object *> m_objects;
  std::unordered_map<std::string, std::string> m_failures;
  std::unordered_map<std::string, Array *> m_arraysByBlob;
};

CaptureImporter::CaptureImporter(Scene *scene,
    const cap::CaptureText *capture,
    std::filesystem::path blobDir,
    AnariCaptureImportReport *report)
    : m_scene(scene),
      m_capture(capture),
      m_blobDir(std::move(blobDir)),
      m_report(report)
{}

void CaptureImporter::drop(const std::string &path, std::string reason)
{
  m_report->dropped.push_back({path, std::move(reason)});
}

const cap::Block *CaptureImporter::block(const std::string &id) const
{
  auto it = m_capture->blocks.find(id);
  return it == m_capture->blocks.end() ? nullptr : &it->second;
}

// Arrays //

std::optional<ArrayContents> CaptureImporter::readArray(
    const cap::Block &b, std::string &reason) const
{
  ArrayContents a;
  auto *et = b.find("elementType");
  auto *dims = b.find("dimensions");
  if (!et || !dims) {
    reason = b.id + " has no elementType or dimensions";
    return {};
  }
  a.elementType = cap::dataTypeFromName(et->value);
  if (a.elementType == ANARI_UNKNOWN) {
    reason = b.id + " has unknown element type " + et->value;
    return {};
  }

  const size_t rank = b.label == "array1d" ? 1 : b.label == "array2d" ? 2 : 3;
  const auto dimType = rank == 1 ? ANARI_UINT64
      : rank == 2                ? ANARI_UINT64_VEC2
                                 : ANARI_UINT64_VEC3;
  uint64_t dimValues[3] = {0, 0, 0};
  if (!cap::decodeValue(
          dimType, dims->value, reinterpret_cast<std::byte *>(dimValues))) {
    reason = b.id + " has unreadable dimensions " + dims->value;
    return {};
  }
  a.count = 1;
  for (size_t d = 0; d < rank; d++) {
    a.dims[d] = dimValues[d];
    a.count *= dimValues[d];
  }

  const size_t elementSize = anari::sizeOf(a.elementType);
  if (auto *data = b.find("data")) {
    if (data->items.size() != a.count) {
      reason = b.id + " lists " + std::to_string(data->items.size())
          + " elements for dimensions " + dims->value;
      return {};
    }
    if (anari::isObject(a.elementType)) {
      a.ids = data->items;
      return a;
    }
    a.bytes.resize(a.count * elementSize);
    for (size_t i = 0; i < a.count; i++) {
      if (!cap::decodeValue(a.elementType,
              data->items[i],
              a.bytes.data() + i * elementSize)) {
        reason = b.id + " element " + std::to_string(i) + " '" + data->items[i]
            + "' is not a " + anari::toString(a.elementType);
        return {};
      }
    }
    return a;
  }

  auto *blob = b.find("blob");
  if (!blob) {
    reason = b.id + " has neither data nor a blob";
    return {};
  }
  const auto file = m_blobDir / (blob->value + ".bin");
  auto bytes = readFile(file);
  if (!bytes) {
    reason = "blob " + blob->value + " is missing (" + file.string() + ")";
    return {};
  }
  if (bytes->size() != a.count * elementSize) {
    reason = "blob " + blob->value + " has " + std::to_string(bytes->size())
        + " bytes, expected " + std::to_string(a.count * elementSize);
    return {};
  }
  a.bytes.resize(bytes->size());
  std::memcpy(a.bytes.data(), bytes->data(), bytes->size());
  return a;
}

std::optional<ArrayContents> CaptureImporter::readArrayAt(
    const std::string &id, std::string &reason) const
{
  auto *b = block(id);
  if (!b || !b->isArray()) {
    reason = "refers to " + id + ", which is not an array in the capture";
    return {};
  }
  return readArray(*b, reason);
}

// The scene Array for a captured array, or nullptr after reporting `path`
// as dropped.
Array *CaptureImporter::importArray(
    const std::string &id, const std::string &path)
{
  if (auto it = m_objects.find(id); it != m_objects.end())
    return static_cast<Array *>(it->second);
  if (auto it = m_failures.find(id); it != m_failures.end()) {
    drop(path, it->second);
    return nullptr;
  }

  // Arrays whose blob names match hold the same type, shape and bytes, so
  // one already in the scene is reused without reading its blob again.
  const auto *b = block(id);
  const auto *blob = b ? b->find("blob") : nullptr;
  if (blob) {
    if (auto it = m_arraysByBlob.find(blob->value);
        it != m_arraysByBlob.end()) {
      m_objects[id] = it->second;
      return it->second;
    }
  }

  std::string reason;
  auto contents = readArrayAt(id, reason);
  if (!contents)
    return static_cast<Array *>(fail(id, path, reason));

  std::vector<size_t> indices;
  for (size_t i = 0; i < contents->ids.size(); i++) {
    const auto &elementId = contents->ids[i];
    if (elementId == NULL_ID) {
      indices.push_back(INVALID_INDEX);
      continue;
    }
    // An element VSR cannot hold is reported at its own path and leaves an
    // empty slot, so the array keeps its shape and the rest of it.
    auto *o = importObject(elementId, indexed(path, i));
    indices.push_back(o ? o->index() : INVALID_INDEX);
  }

  auto array = m_scene->createArray(contents->elementType,
      contents->dims[0],
      contents->dims[1],
      contents->dims[2]);
  array->setName(id);
  if (!indices.empty())
    array->setData(indices.data());
  else if (!contents->bytes.empty())
    array->setData(contents->bytes.data());

  m_objects[id] = array.data();
  if (blob)
    m_arraysByBlob[blob->value] = array.data();
  return array.data();
}

// Scene objects //

Object *CaptureImporter::fail(
    const std::string &id, const std::string &path, std::string reason)
{
  m_failures[id] = reason;
  drop(path, std::move(reason));
  return nullptr;
}

Object *CaptureImporter::importObject(
    const std::string &id, const std::string &path)
{
  if (auto it = m_objects.find(id); it != m_objects.end())
    return it->second;
  if (auto it = m_failures.find(id); it != m_failures.end()) {
    drop(path, it->second);
    return nullptr;
  }

  auto *b = block(id);
  if (!b) {
    return fail(
        id, path, "refers to " + id + ", which the capture does not hold");
  }
  if (b->isArray())
    return importArray(id, path);

  const auto type = sceneObjectType(b->label);
  if (type == ANARI_UNKNOWN)
    return fail(id, path, "a " + b->label + " cannot be a parameter in VSR");

  auto *o =
      m_scene->createObject(type, vsr::core::Token(b->subtype.value_or("")));
  if (!o)
    return fail(id, path, "VSR could not create a " + b->label);
  o->setName(id);
  // Registered before its parameters, so a reference back to it resolves.
  m_objects[id] = o;
  setParameters(*o, *b, path);
  return o;
}

// Sets the captured parameters and disables the rest: VSR's seeded defaults
// stay visible in an editor, but only what the application committed is
// passed on to a device.
void CaptureImporter::setParameters(
    Object &o, const cap::Block &b, const std::string &path)
{
  std::set<std::string> captured;
  for (auto &e : b.entries) {
    if (setParameter(o, e, join(path, e.name)))
      captured.insert(e.name);
  }
  for (size_t i = 0; i < o.numParameters(); i++) {
    auto &p = o.parameterAt(i);
    if (!captured.count(p.name().str()))
      p.setEnabled(false);
  }
}

// Returns whether `e` became a parameter of `o`.
bool CaptureImporter::setParameter(
    Object &o, const cap::Entry &e, const std::string &path)
{
  const auto type = cap::dataTypeFromName(e.type);
  if (type == ANARI_UNKNOWN) {
    drop(path, "unknown type " + e.type);
    return false;
  }

  if (anari::isObject(type) && e.value != "<opaque>") {
    auto *ref = importObject(e.value, path);
    if (!ref)
      return false;
    o.setParameterObject(vsr::core::Token(e.name), *ref);
    return true;
  }

  std::string reason;
  auto v = decodeParameterValue(type, e, reason);
  if (!v) {
    drop(path, reason);
    return false;
  }
  if (type == ANARI_STRING) {
    const auto s = v->getString();
    if (e.name == "name") {
      o.setName(s);
      return false;
    }
    o.setParameter(vsr::core::Token(e.name), type, s.c_str());
  } else {
    o.setParameter(vsr::core::Token(e.name), type, v->data());
  }
  return true;
}

// Instancing //

// Places a world's or group's surfaces, volumes or lights under `node`.
void CaptureImporter::placeContent(
    const cap::Entry &e, LayerNodeRef node, const std::string &path)
{
  std::string reason;
  auto contents = readArrayAt(e.value, reason);
  if (!contents) {
    drop(path, reason);
    return;
  }
  for (size_t i = 0; i < contents->ids.size(); i++) {
    const auto &id = contents->ids[i];
    if (id == NULL_ID)
      continue; // an empty slot, which the format allows
    if (auto *o = importObject(id, indexed(path, i)))
      m_scene->insertChildObjectNode(node, o->type(), o->index());
  }
}

void CaptureImporter::importWorld(
    const cap::Block &world, LayerNodeRef fileNode)
{
  LayerNodeRef worldNode;
  for (auto &e : world.entries) {
    const auto path = join("world", e.name);
    if (e.name == "instance") {
      importInstances(e, fileNode, path);
    } else if (isPlacedContent(e.name)) {
      if (!worldNode)
        worldNode = m_scene->insertChildNode(fileNode, "world");
      placeContent(e, worldNode, path);
    } else {
      drop(path, "a world parameter VSR does not represent");
    }
  }
}

void CaptureImporter::importInstances(
    const cap::Entry &e, LayerNodeRef fileNode, const std::string &path)
{
  std::string reason;
  auto contents = readArrayAt(e.value, reason);
  if (!contents) {
    drop(path, reason);
    return;
  }
  for (size_t i = 0; i < contents->ids.size(); i++) {
    const auto &id = contents->ids[i];
    if (id == NULL_ID)
      continue; // an empty slot, which the format allows
    auto *inst = block(id);
    if (!inst || inst->label != "instance") {
      drop(indexed(path, i), "refers to " + id + ", not an instance");
      continue;
    }
    importInstance(*inst, fileNode, indexed(path, i));
  }
}

// The first element of a captured array of `type`, for the motion instances
// whose rest pose is all VSR keeps.
template <typename T>
std::optional<T> CaptureImporter::firstElement(
    const cap::Entry *e, anari::DataType type)
{
  if (!e)
    return {};
  std::string reason;
  auto contents = readArrayAt(e->value, reason);
  if (!contents || contents->elementType != type || contents->count == 0)
    return {};
  T v;
  std::memcpy(&v, contents->bytes.data(), sizeof(T));
  return v;
}

// The node an instance's transform makes under `fileNode`, or an empty ref
// when it has none. Adds the parameters it read to `consumed`.
LayerNodeRef CaptureImporter::placeInstance(const cap::Block &inst,
    LayerNodeRef fileNode,
    const std::string &name,
    const std::string &path,
    std::set<std::string> &consumed)
{
  const auto subtype = inst.subtype.value_or("");

  if (subtype == "transform") {
    consumed.insert("transform");
    auto *xfm = inst.find("transform");
    if (!xfm)
      return {};
    const auto type = cap::dataTypeFromName(xfm->type);
    if (type == ANARI_ARRAY1D) {
      auto *a = importArray(xfm->value, join(path, "transform"));
      return a
          ? m_scene->insertChildTransformArrayNode(fileNode, a, name.c_str())
          : LayerNodeRef{};
    }
    math::mat4 m = math::IDENTITY_MAT4;
    if (type == ANARI_FLOAT32_MAT4
        && cap::decodeValue(
            type, xfm->value, reinterpret_cast<std::byte *>(&m)))
      return m_scene->insertChildTransformNode(fileNode, m, name.c_str());
    drop(join(path, "transform"), "'" + xfm->value + "' is not a mat4");
    return {};
  }

  if (subtype == "motionTransform") {
    for (auto *n : {"motion.transform", "time"})
      consumed.insert(n);
    auto m = firstElement<math::mat4>(
        inst.find("motion.transform"), ANARI_FLOAT32_MAT4);
    drop(path, "motion is not represented; the first transform is used");
    return m_scene->insertChildTransformNode(
        fileNode, m.value_or(math::IDENTITY_MAT4), name.c_str());
  }

  if (subtype == "motionScaleRotationTranslation") {
    for (auto *n :
        {"motion.scale", "motion.rotation", "motion.translation", "time"})
      consumed.insert(n);
    const auto s = firstElement<math::float3>(
        inst.find("motion.scale"), ANARI_FLOAT32_VEC3);
    const auto r = firstElement<math::float4>(
        inst.find("motion.rotation"), ANARI_FLOAT32_QUAT_IJKW);
    const auto t = firstElement<math::float3>(
        inst.find("motion.translation"), ANARI_FLOAT32_VEC3);
    const math::mat4 m = math::mul(
        math::pose_matrix(r.value_or(math::float4(0.f, 0.f, 0.f, 1.f)),
            t.value_or(math::float3(0.f))),
        math::scaling_matrix(s.value_or(math::float3(1.f))));
    drop(path,
        "motion is not represented; the first scale, rotation and "
        "translation are used");
    return m_scene->insertChildTransformNode(fileNode, m, name.c_str());
  }

  drop(path + ".@subtype",
      "instance subtype \"" + subtype
          + "\" is not represented; placed untransformed");
  return {};
}

void CaptureImporter::importInstance(
    const cap::Block &inst, LayerNodeRef fileNode, const std::string &path)
{
  std::string name = inst.id;
  if (auto *n = inst.find("name");
      n && cap::dataTypeFromName(n->type) == ANARI_STRING) {
    if (auto s = cap::unquote(n->value))
      name = *s;
  }

  std::set<std::string> consumed = {"group", "name"};
  auto node = placeInstance(inst, fileNode, name, path, consumed);
  if (!node) {
    node = m_scene->insertChildTransformNode(
        fileNode, math::IDENTITY_MAT4, name.c_str());
  }

  for (auto &e : inst.entries) {
    if (!consumed.count(e.name))
      setInstanceParameter(node->value(), e, join(path, e.name));
  }

  auto *groupEntry = inst.find("group");
  auto *group = groupEntry ? block(groupEntry->value) : nullptr;
  if (!group || group->label != "group") {
    drop(join(path, "group"), "the instance has no group");
    return;
  }
  for (auto &e : group->entries) {
    const auto groupPath = join(join(path, "group"), e.name);
    if (isPlacedContent(e.name))
      placeContent(e, node, groupPath);
    else
      drop(groupPath, "a group parameter VSR does not represent");
  }
}

void CaptureImporter::setInstanceParameter(
    LayerNodeData &node, const cap::Entry &e, const std::string &path)
{
  const auto type = cap::dataTypeFromName(e.type);
  if (type == ANARI_UNKNOWN) {
    drop(path, "unknown type " + e.type);
    return;
  }
  if (anari::isObject(type) && e.value != "<opaque>") {
    if (auto *o = importObject(e.value, path))
      node.setInstanceParameter(e.name, Any(o->type(), o->index()));
    return;
  }
  std::string reason;
  if (auto v = decodeParameterValue(type, e, reason))
    node.setInstanceParameter(e.name, *v);
  else
    drop(path, reason);
}

// Frame //

void CaptureImporter::importFrame(LayerNodeRef fileNode)
{
  auto &frame = m_capture->blocks.at(m_capture->frameId);
  for (auto &e : frame.entries) {
    const auto type = cap::dataTypeFromName(e.type);
    if (e.name == "camera" && type == ANARI_CAMERA) {
      importObject(e.value, "camera");
    } else if (e.name == "renderer" && type == ANARI_RENDERER) {
      reportRenderer(e.value);
    } else if (e.name == "world" && type == ANARI_WORLD) {
      if (auto *w = block(e.value))
        importWorld(*w, fileNode);
      else
        drop("world", "refers to " + e.value + ", which is missing");
    } else {
      m_report->frameParameters.push_back(entryText(e));
    }
  }
}

void CaptureImporter::reportRenderer(const std::string &id)
{
  auto *r = block(id);
  if (!r)
    return;
  m_report->rendererSubtype = r->subtype.value_or("");
  for (auto &e : r->entries)
    m_report->rendererParameters.push_back(entryText(e));
}

} // namespace

AnariCaptureImportReport import_ANARI_CAPTURE(Scene &scene,
    vsr::animation::AnimationManager & /*animMgr*/,
    const char *filename,
    LayerNodeRef location)
{
  AnariCaptureImportReport report;

  const std::filesystem::path file(filename);
  auto text = readFile(file);
  if (!text) {
    report.error = "cannot open '" + file.string() + "'";
    return report;
  }

  auto capture = cap::parseCaptureText(*text, report.error);
  if (!capture)
    return report;

  report.read = true;
  report.formatVersion = capture->version;
  report.extensions = capture->extensions;
  report.callCounts = capture->callCounts;
  report.captureWarnings = capture->warnings;

  auto root = location ? location : scene.defaultLayer()->root();
  auto fileNode = scene.insertChildNode(root, file.stem().string().c_str());

  CaptureImporter importer(
      &scene, &*capture, file.parent_path() / "blobs", &report);
  importer.importFrame(fileNode);
  return report;
}

} // namespace vsr::io

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ColorMaps.h"

#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/scene/Scene.hpp"

namespace vsr::scivis_studio::color_map {

// Samples in the Array a color map record names.
static constexpr size_t COLOR_MAP_SAMPLES = 256;

static std::string colorMapArrayName(const ColorMapID &id)
{
  return id + "_colormap";
}

static vsr::scene::ArrayRef createColorMapArray(
    vsr::scene::Scene &scene, const ColorMapID &id)
{
  auto array = scene.createArray(ANARI_FLOAT32_VEC4, COLOR_MAP_SAMPLES);
  array->setData(vsr::core::makeDefaultColorMap(COLOR_MAP_SAMPLES));
  array->setName(colorMapArrayName(id));
  return array;
}

ColorMapRecord &createColorMap(
    Project &project, vsr::scene::Scene &scene, const std::string &name)
{
  ColorMapRecord record;
  record.id = project::nextColorMapId(project);
  record.name = name;
  createColorMapArray(scene, record.id);
  project.colorMaps.push_back(std::move(record));
  return project.colorMaps.back();
}

bool removeColorMap(Project &project,
    vsr::scene::Scene &scene,
    const ColorMapID &id,
    std::string *error)
{
  const auto *record = project::findColorMap(project, id);
  if (!record) {
    if (error)
      *error = "color map not found";
    return false;
  }
  const auto itr =
      project.colorMaps.begin() + (record - project.colorMaps.data());

  if (auto array = resolveColorMapArray(scene, id))
    scene.removeObject(array.data());
  project.colorMaps.erase(itr);
  return true;
}

vsr::scene::ArrayRef resolveColorMapArray(
    const vsr::scene::Scene &scene, const ColorMapID &id)
{
  const auto arrayName = colorMapArrayName(id);
  size_t index = VSR_INVALID_INDEX;
  vsr::core::foreach_item_const(
      scene.objectDB().array, [&](const vsr::scene::Array *array) {
        if (array && array->name() == arrayName)
          index = array->index();
      });
  if (index == VSR_INVALID_INDEX)
    return {};
  return scene.getObject<vsr::scene::Array>(index);
}

void ensureColorMapArrays(const Project &project, vsr::scene::Scene &scene)
{
  for (const auto &record : project.colorMaps) {
    if (!resolveColorMapArray(scene, record.id))
      createColorMapArray(scene, record.id);
  }
}

} // namespace vsr::scivis_studio::color_map

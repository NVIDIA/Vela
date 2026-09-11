// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SceneEditMessages.h"
#include "PayloadCommon.h"

namespace vsr::scivis_studio::protocol {

void toNode(const SetObjectParameter &p, vsr::core::DataNode &n)
{
  writeChildNode(n, "object", p.object);
  writeChild(n, "name", p.name);
  n["value"].setValue(p.value);
}

bool fromNode(const vsr::core::DataNode &n, SetObjectParameter &p)
{
  if (!readChildNode(n, "object", p.object) || !readChild(n, "name", p.name))
    return false;
  const auto *value = n.child("value");
  if (!value || value->holdsArray() || !value->getValue().valid())
    return false;
  p.value = value->getValue();
  return true;
}

void toNode(const ObjectMetadataEntry &e, vsr::core::DataNode &n)
{
  writeChild(n, "name", e.name);
  if (e.holdsArray()) {
    writeChild(n, "arrayElementType", int(e.arrayElementType));
    if (e.arrayElementCount != 0 && !e.arrayData.empty()) {
      n["array"].setValueAsArray(
          e.arrayElementType, e.arrayData.data(), e.arrayElementCount);
    }
  } else if (e.value.valid()) {
    n["value"].setValue(e.value);
  }
}

bool fromNode(const vsr::core::DataNode &n, ObjectMetadataEntry &e)
{
  e.value = {};
  e.arrayElementType = ANARI_UNKNOWN;
  e.arrayElementCount = 0;
  e.arrayData.clear();

  if (!readChild(n, "name", e.name))
    return false;

  const auto *value = n.child("value");
  const auto *elementType = n.child("arrayElementType");

  if (elementType != nullptr) {
    if (value != nullptr)
      return false; // an entry is an array one or a value one, never both
    int type = int(ANARI_UNKNOWN);
    if (!readChild(n, "arrayElementType", type) || type == int(ANARI_UNKNOWN))
      return false;
    e.arrayElementType = anari::DataType(type);

    const auto *array = n.child("array");
    if (!array)
      return true; // an array key that holds nothing

    anari::DataType carried = ANARI_UNKNOWN;
    const void *ptr = nullptr;
    size_t count = 0;
    array->getValueAsArray(&carried, &ptr, &count);
    if (!ptr || carried != e.arrayElementType)
      return false;
    const auto *bytes = static_cast<const std::byte *>(ptr);
    e.arrayElementCount = count;
    e.arrayData.assign(bytes, bytes + count * anari::sizeOf(carried));
    return true;
  }

  if (!value) // the key was removed
    return true;
  if (value->holdsArray())
    return false;
  e.value = value->getValue();
  return true;
}

} // namespace vsr::scivis_studio::protocol

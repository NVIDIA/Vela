// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/scene/Scene.hpp"
// imgui
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
// std
#include <array>
#include <cstddef>

namespace vsr::ui {

constexpr float INDENT_AMOUNT = 25.f;

// An edit the object editor offers that a host application may not be able
// to carry out. Each names an affordance, not a value: what the widget
// hides or disables when the policy below refuses it.
enum class ObjectEdit
{
  CreateObject, // "set type > object > new": makes a new scene object
  SetUsageHint, // "set type > uniform": retypes a value and its usage hint
  SetStringList, // "set type > attribute", and string-list parameters
  BindArray, // "set type > object > array": binds an array-valued parameter
  ClearValue, // the "clear" button: drops a parameter's value entirely
  COUNT
};

/*
 * Which of those edits the object editor may offer. Permissive by default:
 * an application that edits a scene it owns outright (the local SciVis
 * Studio, vsrViewer, the demos) takes the default and nothing changes.
 *
 * A host that cannot carry an edit out refuses it with the reason the user
 * sees on the disabled affordance -- the SciVis Studio client refuses the
 * five edits above because no client-to-server message expresses them,
 * while every value-only edit (scalars, vectors, colors, matrices, plain
 * strings, object references) still round-trips.
 *
 * `reason` is not copied: pass a string literal.
 *
 * Example:
 *   ObjectEditPolicy policy;
 *   policy.refuse(ObjectEdit::ClearValue,
 *       "clearing a parameter has no message on the wire");
 *   objectEditor->setEditPolicy(policy);
 */
struct ObjectEditPolicy
{
  bool allows(ObjectEdit edit) const;
  // Why `edit` is refused, or nullptr when it is allowed.
  const char *refusal(ObjectEdit edit) const;
  void refuse(ObjectEdit edit, const char *reason);

 private:
  // Indexed by ObjectEdit; a null entry is an allowed edit.
  std::array<const char *, size_t(ObjectEdit::COUNT)> m_refusals{};
};

void buildUI_object(vsr::scene::Object &o,
    vsr::scene::Scene &scene,
    bool useTableForParameters = false,
    int level = 0,
    const ObjectEditPolicy &policy = {});
bool buildUI_parameter(vsr::scene::Object &o,
    vsr::scene::Parameter &p,
    vsr::scene::Scene &scene,
    bool asTable = false,
    const ObjectEditPolicy &policy = {});
size_t buildUI_objects_menulist(
    const vsr::scene::Scene &scene, anari::DataType &type);

void tooltipForPreviousItem(const char *text, bool showWhenDisabled = true);

// Inlined definitions ////////////////////////////////////////////////////////

inline bool ObjectEditPolicy::allows(ObjectEdit edit) const
{
  return refusal(edit) == nullptr;
}

inline const char *ObjectEditPolicy::refusal(ObjectEdit edit) const
{
  return m_refusals[size_t(edit)];
}

inline void ObjectEditPolicy::refuse(ObjectEdit edit, const char *reason)
{
  m_refusals[size_t(edit)] = reason;
}

} // namespace vsr::ui

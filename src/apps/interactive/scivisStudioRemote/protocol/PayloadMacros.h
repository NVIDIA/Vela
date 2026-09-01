// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"

/*
 * Definition macros for the payload shapes that recur across request files:
 * an empty payload, a bare {requestId}, {requestId, <id or name>},
 * {requestId, <id>, newName}, the two archive shapes and a one-field result.
 * Each expands to the toNode()/fromNode() pair for T; the wire child is named
 * after the field. Macros rather than templates because the field is named
 * by the caller and toNode()/fromNode() must be real overloads of T for the
 * codec to find them. Included by .cpp files only.
 *
 * Example:
 *   VSR_STUDIO_ID_REQUEST(RemoveShot, shotId)
 *   VSR_STUDIO_RENAME_REQUEST(RenameLightRig, lightRigId)
 */

// toNode writes nothing, fromNode always succeeds.
#define VSR_STUDIO_EMPTY_PAYLOAD(T)                                            \
  void toNode(const T &, vsr::core::DataNode &) {}                             \
  bool fromNode(const vsr::core::DataNode &, T &)                              \
  {                                                                            \
    return true;                                                               \
  }

// {requestId}
#define VSR_STUDIO_BARE_REQUEST(T)                                             \
  void toNode(const T &p, vsr::core::DataNode &n)                              \
  {                                                                            \
    writeChild(n, "requestId", p.requestId);                                   \
  }                                                                            \
  bool fromNode(const vsr::core::DataNode &n, T &p)                            \
  {                                                                            \
    return readChild(n, "requestId", p.requestId);                             \
  }

// {requestId, field}
#define VSR_STUDIO_ID_REQUEST(T, field)                                        \
  void toNode(const T &p, vsr::core::DataNode &n)                              \
  {                                                                            \
    writeChild(n, "requestId", p.requestId);                                   \
    writeChild(n, #field, p.field);                                            \
  }                                                                            \
  bool fromNode(const vsr::core::DataNode &n, T &p)                            \
  {                                                                            \
    return readChild(n, "requestId", p.requestId)                              \
        && readChild(n, #field, p.field);                                      \
  }

// {requestId, field, newName}
#define VSR_STUDIO_RENAME_REQUEST(T, field)                                    \
  void toNode(const T &p, vsr::core::DataNode &n)                              \
  {                                                                            \
    writeChild(n, "requestId", p.requestId);                                   \
    writeChild(n, #field, p.field);                                            \
    writeChild(n, "newName", p.newName);                                       \
  }                                                                            \
  bool fromNode(const vsr::core::DataNode &n, T &p)                            \
  {                                                                            \
    return readChild(n, "requestId", p.requestId)                              \
        && readChild(n, #field, p.field)                                       \
        && readChild(n, "newName", p.newName);                                 \
  }

// {requestId, file}: load an archive from a server path.
#define VSR_STUDIO_ARCHIVE_LOAD_REQUEST(T)                                     \
  void toNode(const T &p, vsr::core::DataNode &n)                              \
  {                                                                            \
    writeChild(n, "requestId", p.requestId);                                   \
    writePath(n, "file", p.file);                                              \
  }                                                                            \
  bool fromNode(const vsr::core::DataNode &n, T &p)                            \
  {                                                                            \
    return readChild(n, "requestId", p.requestId)                              \
        && readPath(n, "file", p.file);                                        \
  }

// {requestId, field, file}: save the entity `field` names to a server path.
#define VSR_STUDIO_ARCHIVE_SAVE_REQUEST(T, field)                              \
  void toNode(const T &p, vsr::core::DataNode &n)                              \
  {                                                                            \
    writeChild(n, "requestId", p.requestId);                                   \
    writeChild(n, #field, p.field);                                            \
    writePath(n, "file", p.file);                                              \
  }                                                                            \
  bool fromNode(const vsr::core::DataNode &n, T &p)                            \
  {                                                                            \
    return readChild(n, "requestId", p.requestId)                              \
        && readChild(n, #field, p.field) && readPath(n, "file", p.file);       \
  }

// A result payload holding one scalar `field`.
#define VSR_STUDIO_ID_RESULT(T, field)                                         \
  void toNode(const T &p, vsr::core::DataNode &n)                              \
  {                                                                            \
    writeChild(n, #field, p.field);                                            \
  }                                                                            \
  bool fromNode(const vsr::core::DataNode &n, T &p)                            \
  {                                                                            \
    return readChild(n, #field, p.field);                                      \
  }

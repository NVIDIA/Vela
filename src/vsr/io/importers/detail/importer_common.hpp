// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/animation/Animation.hpp"
#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/io/images.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstdio>
#include <string>
#include <vector>
#if VSR_USE_VTK
// vtk
#include <vtkDataArray.h>
#endif

namespace vsr::io {

std::string pathOf(const std::string &filepath);
std::string fileOf(const std::string &filepath);
std::string extensionOf(const std::string &filepath);
bool isAbsolute(const std::string &filepath);

std::vector<std::string> splitString(const std::string &s, char delim);

vsr::scene::ArrayRef readArray(
    vsr::scene::Scene &scene, anari::DataType elementType, std::FILE *fp);

// Thin shims over vsr::io::images; the Sampler lands in the Scene the given
// ImageCache holds, so no caller can name a different one. See the note above
// their definitions.
vsr::scene::SamplerRef importTexture(ImageCache &cache,
    std::string filepath,
    bool isLinear = false,
    const SamplerSettings &settings = {});
vsr::scene::SamplerRef importTextureFromMemory(ImageCache &cache,
    const std::string &cacheKey,
    const std::string &displayName,
    const void *data,
    size_t numBytes,
    bool isLinear = false,
    const std::string &formatHint = "",
    const SamplerSettings &settings = {});
vsr::scene::SamplerRef importRawTexture2D(ImageCache &cache,
    const std::string &cacheKey,
    const std::string &displayName,
    const void *data,
    size_t width,
    size_t height,
    bool isLinear = false,
    const SamplerSettings &settings = {});

vsr::scene::SamplerRef makeDefaultColorMapSampler(
    vsr::scene::Scene &scene, const vsr::math::float2 &range);

// Transfer function import functions
vsr::core::TransferFunction importTransferFunction(const std::string &filepath);

// Sample a TransferFunction into a 256-entry RGBA colormap and apply it to a
// volume (sets color array, valueRange, and opacityControlPoints metadata).
void applyTransferFunction(vsr::scene::Scene &scene,
    vsr::scene::VolumeRef volume,
    const vsr::core::TransferFunction &transferFunction);

bool calcTangentsForTriangleMesh(const vsr::math::uint3 *indices,
    const vsr::math::float3 *vertexPositions,
    const vsr::math::float3 *vertexNormals,
    const vsr::math::float2 *texCoords,
    vsr::math::float4 *tangents,
    size_t numIndices,
    size_t numVertices,
    // mikktspace wants v-up coordinates, while the coordinates importers hand
    // ANARI run down the image, so the default reverses them back.
    bool flipTexCoordY = true,
    bool faceVaryingTangents = false);

#if VSR_USE_VTK
anari::DataType vtkTypeToANARIType(
    int vtkType, int numComps, const char *errorIdentifier = "");
vsr::scene::ArrayRef makeArray1DFromVTK(vsr::scene::Scene &scene,
    vtkDataArray *array,
    const char *errorIdentifier = "");
vsr::scene::ArrayRef makeArray3DFromVTK(vsr::scene::Scene &scene,
    vtkDataArray *array,
    size_t w,
    size_t h,
    size_t d,
    const char *errorIdentifier = "");
#endif

// Animation helpers ///////////////////////////////////////////////////////////

// Create a linear time base [0..1] with `count` evenly spaced samples.
std::vector<float> makeLinearTimeBase(size_t count);

// Build bindings for the "one value-array per parameter" pattern (e.g. camera
// animation where each Array has N elements of the parameter's scalar type).
void addValueTimeStepBindings(vsr::animation::Animation &anim,
    vsr::scene::Object *target,
    const std::vector<vsr::core::Token> &paramNames,
    const std::vector<vsr::scene::ObjectUsePtr<vsr::scene::Array>> &dataArrays,
    const std::vector<float> &timeBase,
    vsr::animation::InterpolationRule interp =
        vsr::animation::InterpolationRule::STEP);

// Build bindings for the "array-of-arrays per parameter" pattern (e.g. geometry
// animation where each timestep swaps a different Array object).
void addArrayTimeStepBindings(vsr::animation::Animation &anim,
    vsr::scene::Object *target,
    const std::vector<vsr::core::Token> &paramNames,
    const std::vector<std::vector<vsr::scene::ObjectUsePtr<vsr::scene::Array>>>
        &arraysPerParam,
    const std::vector<float> &timeBase);

// Build a TransformBinding from a sequence of mat4 frames (decomposes each
// frame into rotation quaternion, translation, and scale).
void addTransformStepBinding(vsr::animation::Animation &anim,
    vsr::scene::LayerNodeRef target,
    const std::vector<vsr::math::mat4> &frames,
    const std::vector<float> &timeBase);

} // namespace vsr::io

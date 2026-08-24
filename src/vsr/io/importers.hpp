// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/core/FlatMap.hpp"
#include "vsr/io/UsdImport.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::animation {
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::core {
class DataNode;
} // namespace vsr::core

namespace vsr::io {

using namespace vsr::scene;

// clang-format off

// Full scene importers //

void import_AGX(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_ASSIMP(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {}, bool flatten = false);
void import_AXYZ(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_DLAF(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {}, bool useDefaultMaterial = false);
void import_E57XYZ(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_ENSIGHT(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filename,
    LayerNodeRef location = {},
    int timestep = 0);
void import_ENSIGHT(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filename,
    LayerNodeRef location,
    const std::vector<std::string> &fields,
    int timestep = 0);
void import_ENSIGHT(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filename,
    LayerNodeRef location,
    const std::vector<std::string> &fields,
    const vsr::core::DataNode &settings,
    int timestep = 0);
void import_ENSIGHT(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filename,
    LayerNodeRef location,
    const std::vector<std::string> &fields,
    const vsr::core::DataNode &settings,
    MaterialRef overrideMaterial,
    int timestep = 0);
void import_ENSIGHT(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filename,
    LayerNodeRef location,
    const std::vector<std::string> &fields,
    const vsr::core::DataNode &settings,
    MaterialRef overrideMaterial,
    const vsr::core::FlatMap<std::string, MaterialRef> &perPartMaterials,
    int timestep = 0);
void import_GLTF(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_HDRI(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_HSMESH(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_NBODY(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {}, bool useDefaultMaterial = false);
void import_OBJ(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {}, bool useDefaultMaterial = false);
void import_PDB(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_PBRT(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_PLY(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_POINTSBIN(Scene &scene, vsr::animation::AnimationManager &animMgr, const std::vector<std::string> &filepaths, LayerNodeRef location = {});
void import_PT(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_SILO(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location);
void import_SMESH(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {}, bool isAnimation = false);
void import_SWC(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_SWC_SDF(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
void import_TRK(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});
UsdImportReport import_USD(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {}, const UsdImportOptions &options = {});
void import_VTP(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filepath, LayerNodeRef location = {});
void import_VTU(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filepath, LayerNodeRef location, std::optional<std::string> propertyName = std::nullopt);
void import_XYZDP(Scene &scene, vsr::animation::AnimationManager &animMgr, const char *filename, LayerNodeRef location = {});

// Spatial field importers //

// Dispatch to the appropriate spatial field importer based on file extension.
// Supports: .raw, .flash/.hdf5, .nvdb/.vdb, .mhd, .vtu, .silo/.sil
// Note: .vti is not supported here; use import_volume() for VTI files.
SpatialFieldRef import_spatial_field(Scene &scene, const char *filename, std::optional<std::string> propertyName = std::nullopt);

SpatialFieldRef import_RAW(Scene &scene, const char *filename);
SpatialFieldRef import_FLASH(Scene &scene, const char *filename);
SpatialFieldRef import_NVDB(Scene &scene, const char *filename);
SpatialFieldRef import_MHD(Scene &scene, const char *filename);
SpatialFieldRef import_VTI(Scene &scene,
    const char *filename,
    LayerNodeRef location = {},
    std::vector<SpatialFieldRef> *extraFields = nullptr);
SpatialFieldRef import_VTU(Scene &scene, const char *filename, std::optional<std::string> propertyName = std::nullopt);
SpatialFieldRef import_SILO(Scene &scene, const char *filename);

// clang-format on

///////////////////////////////////////////////////////////////////////////////
// Import volume files (dispatch to different spatial field importers) ////////
///////////////////////////////////////////////////////////////////////////////

VolumeRef import_volume(
    Scene &scene, const char *filename, LayerNodeRef location = {});

VolumeRef import_volume(Scene &scene,
    const char *filename,
    const core::TransferFunction &transferFunction,
    LayerNodeRef location = {});

// Load a sequence of spatial field files as an animated volume.
// Creates a Volume for the first frame and registers a CallbackBinding on a
// new Animation in animMgr so that subsequent frames are loaded on demand.
// Time t=0.0 selects files[0]; t=1.0 selects files[N-1].
VolumeRef import_volume_animation(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<std::string> &files,
    const TransferFunction &transferFunction,
    LayerNodeRef location = {});

///////////////////////////////////////////////////////////////////////////////
// Import entire files, dispatches to above importer functions ////////////////
///////////////////////////////////////////////////////////////////////////////

enum class ImporterType
{
  AGX,
  ASSIMP,
  ASSIMP_FLAT,
  AXYZ,
  DLAF,
  E57XYZ,
  ENSIGHT,
  GLTF,
  HDRI,
  HSMESH,
  NBODY,
  OBJ,
  PDB,
  PBRT,
  PLY,
  POINTSBIN_MULTIFILE,
  PT,
  SILO,
  SMESH,
  SMESH_ANIMATION, // time series version
  SWC,
  SWC_SDF,
  TRK,
  USD,
  USD_MTLX, // native MaterialX materials instead of a portable mapping
  VTP,
  VTU,
  XYZDP,
  VOLUME,
  VOLUME_ANIMATION, // time series of spatial field files
  XF, // Special case for transfer function files
      // Not an actual scene importer, but used to set transfer function from
      // CLI
  BLANK, // Must be last import type before 'NONE'
  NONE
};

using ImportFile = std::pair<ImporterType, std::string>;
using ImportAnimationFiles = std::pair<ImporterType, std::vector<std::string>>;

struct UserColorMap
{
  std::string name;
  std::filesystem::path path;
  std::vector<vsr::core::ColorPoint> colorPoints;
};

std::filesystem::path userColorMapDirectory();
std::vector<UserColorMap> loadUserColorMaps();
std::vector<UserColorMap> loadUserColorMaps(
    const std::filesystem::path &directory);
vsr::core::TransferFunction importTransferFunction(const std::string &filepath);

void import_file(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const ImportFile &file,
    vsr::scene::LayerNodeRef root = {});
void import_file(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const ImportFile &file,
    vsr::core::TransferFunction &transferFunction,
    vsr::scene::LayerNodeRef root = {});

void import_files(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportFile> &files,
    vsr::scene::LayerNodeRef root = {});
void import_files(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportFile> &files,
    vsr::core::TransferFunction transferFunction,
    vsr::scene::LayerNodeRef root = {});

void import_animations(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportAnimationFiles> &files,
    vsr::scene::LayerNodeRef root = {});

void import_animations(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportAnimationFiles> &files,
    const vsr::core::TransferFunction &transferFunction,
    vsr::scene::LayerNodeRef root = {});

} // namespace vsr::io

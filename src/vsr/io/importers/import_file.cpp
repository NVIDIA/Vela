// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/importers.hpp"
#include "vsr/io/importers/detail/importer_common.hpp"
// vsr_animation
#include "vsr/animation/AnimationManager.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>

namespace vsr::io {

namespace {

void ensureDefaultTransferFunction(vsr::core::TransferFunction &tf)
{
  if (!tf.colorPoints.empty() || !tf.opacityPoints.empty())
    return;

  for (const auto &c : core::colormap::viridis) {
    tf.colorPoints.push_back({float(tf.colorPoints.size())
            / float(core::colormap::viridis.size() - 1),
        c.x,
        c.y,
        c.z});
  }
  tf.opacityPoints = {{0.0f, 0.0f}, {1.0f, 1.0f}};
  tf.range = {};
}

} // namespace

void import_file(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const ImportFile &f,
    vsr::scene::LayerNodeRef root)
{
  vsr::core::TransferFunction tf;
  ensureDefaultTransferFunction(tf);
  import_file(scene, animMgr, f, tf, root);
}

void import_file(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const ImportFile &f,
    vsr::core::TransferFunction &tf,
    vsr::scene::LayerNodeRef root)
{
  const bool customLocation = root;

  auto files = splitString(f.second, ';');
  std::string file = files[0];
  std::string layerName = files.size() > 1 ? files[1] : "";
  if (layerName.empty())
    layerName = "default";

  if (!customLocation) {
    vsr::core::logStatus(
        "...loading file '%s' in layer '%s'", file.c_str(), layerName.c_str());
    root = scene.addLayer(layerName)->root();
  } else {
    vsr::core::logStatus("...loading file '%s'", file.c_str());
  }

  if (f.first == ImporterType::AGX)
    vsr::io::import_AGX(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::ASSIMP)
    vsr::io::import_ASSIMP(scene, animMgr, file.c_str(), root, false);
  else if (f.first == ImporterType::ASSIMP_FLAT)
    vsr::io::import_ASSIMP(scene, animMgr, file.c_str(), root, true);
  else if (f.first == ImporterType::AXYZ)
    vsr::io::import_AXYZ(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::DLAF)
    vsr::io::import_DLAF(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::E57XYZ)
    vsr::io::import_E57XYZ(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::ENSIGHT)
    vsr::io::import_ENSIGHT(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::GLTF)
    vsr::io::import_GLTF(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::HDRI)
    vsr::io::import_HDRI(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::HSMESH)
    vsr::io::import_HSMESH(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::NBODY)
    vsr::io::import_NBODY(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::OBJ)
    vsr::io::import_OBJ(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::PDB)
    vsr::io::import_PDB(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::PBRT)
    vsr::io::import_PBRT(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::PLY)
    vsr::io::import_PLY(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::POINTSBIN_MULTIFILE)
    vsr::io::import_POINTSBIN(scene, animMgr, {file.c_str()}, root);
  else if (f.first == ImporterType::PT)
    vsr::io::import_PT(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::SILO)
    vsr::io::import_SILO(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::SMESH)
    vsr::io::import_SMESH(scene, animMgr, file.c_str(), root, false);
  else if (f.first == ImporterType::SMESH_ANIMATION)
    vsr::io::import_SMESH(scene, animMgr, file.c_str(), root, true);
  else if (f.first == ImporterType::SWC)
    vsr::io::import_SWC(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::SWC_SDF)
    vsr::io::import_SWC_SDF(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::TRK)
    vsr::io::import_TRK(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::USD) {
    widenAnimationClock(
        animMgr, vsr::io::import_USD(scene, animMgr, file.c_str(), root));
  } else if (f.first == ImporterType::USD_MTLX) {
    UsdImportOptions options;
    options.materialMode = UsdMaterialMode::MATERIALX;
    widenAnimationClock(animMgr,
        vsr::io::import_USD(scene, animMgr, file.c_str(), root, options));
  } else if (f.first == ImporterType::VTP)
    vsr::io::import_VTP(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::VTU) {
    std::optional<std::string> prop;
    if (files.size() > 2 && !files[2].empty())
      prop = files[2];
    vsr::io::import_VTU(scene, animMgr, file.c_str(), root, std::move(prop));
  } else if (f.first == ImporterType::XYZDP)
    vsr::io::import_XYZDP(scene, animMgr, file.c_str(), root);
  else if (f.first == ImporterType::VOLUME)
    vsr::io::import_volume(scene, file.c_str(), tf, root);
  else if (f.first == ImporterType::XF)
    tf = vsr::io::importTransferFunction(file);
  else if (f.first == ImporterType::BLANK) {
    // no-op
  } else {
    vsr::core::logWarning(
        "...skipping unknown file type for '%s'", file.c_str());
  }
}

void import_files(Scene &s,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportFile> &files,
    vsr::scene::LayerNodeRef root)
{
  import_files(s, animMgr, files, {}, root);
}

void import_files(Scene &s,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportFile> &files,
    vsr::core::TransferFunction tf,
    vsr::scene::LayerNodeRef root)
{
  ensureDefaultTransferFunction(tf);

  const size_t rank = s.mpiRank();
  const size_t numRanks = s.mpiNumRanks();
  for (size_t i = 0; i < files.size(); i++) {
    const bool importOnAllRanks = files[i].first == ImporterType::XF;
    if (!importOnAllRanks && numRanks > 1 && (i % numRanks != rank))
      continue;
    import_file(s, animMgr, files[i], tf, root);
  }
}

void import_animations(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportAnimationFiles> &files,
    vsr::scene::LayerNodeRef root)
{
  vsr::core::TransferFunction tf;
  ensureDefaultTransferFunction(tf);
  import_animations(scene, animMgr, files, tf, root);
}

void import_animations(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::vector<ImportAnimationFiles> &files,
    const vsr::core::TransferFunction &tf,
    vsr::scene::LayerNodeRef root)
{
  for (auto &anim : files) {
    if (anim.second.empty()) {
      vsr::core::logWarning("...skipping animation import for empty file list");
      continue;
    }

    if (anim.first == ImporterType::POINTSBIN_MULTIFILE)
      import_POINTSBIN(scene, animMgr, anim.second, root);
    else if (anim.first == ImporterType::VOLUME_ANIMATION)
      import_volume_animation(scene, animMgr, anim.second, tf, root);
    else {
      vsr::core::logWarning("...skipping unknown animation file importer type");
    }
  }
}

} // namespace vsr::io

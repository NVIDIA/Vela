// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/io/archives/AnimationManagerArchive.hpp"
#include "vsr/io/archives/SceneArchive.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/io/procedural.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scripting/LuaBindings.hpp"
#include "vsr/scripting/Sol2Helpers.hpp"

#include <fmt/format.h>
#include <sol/sol.hpp>

namespace vsr::scripting {

namespace {

// Read USD import settings out of a Lua table. Absent keys keep their
// defaults, so the common case stays `vsr.io.importUSD(scene, anim, file)`.
vsr::io::UsdImportOptions usdImportOptionsFromLuaTable(
    const sol::table &settings)
{
  vsr::io::UsdImportOptions retval;

  if (sol::optional<sol::table> purposes = settings["purposes"]) {
    auto readFlag = [&](const char *name, bool &out) {
      if (sol::optional<bool> value = (*purposes)[name])
        out = *value;
    };
    readFlag("default", retval.purposes.defaultPurpose);
    readFlag("render", retval.purposes.render);
    readFlag("proxy", retval.purposes.proxy);
    readFlag("guide", retval.purposes.guide);
  }

  if (sol::optional<sol::table> contexts = settings["renderContexts"]) {
    retval.renderContexts.clear();
    for (size_t i = 1; i <= contexts->size(); ++i) {
      if (sol::optional<std::string> value = (*contexts)[i])
        retval.renderContexts.push_back(*value);
    }
  }

  if (sol::optional<std::string> mode = settings["materialMode"])
    retval.materialMode = vsr::io::usdMaterialModeFromString(*mode);

  if (sol::optional<int> level = settings["refinementLevel"])
    retval.refinementLevel = *level;
  if (sol::optional<std::string> primPath = settings["primPath"])
    retval.primPath = *primPath;

  return retval;
}

} // namespace

#define VSR_LUA_IMPORT_WRAP(import_call, filename)                             \
  try {                                                                        \
    import_call;                                                               \
  } catch (const std::exception &e) {                                          \
    throw std::runtime_error(                                                  \
        fmt::format("Failed to import '{}': {}", filename, e.what()));         \
  }

#define VSR_LUA_IMPORT_WRAP_RETURN(import_call, filename)                      \
  try {                                                                        \
    return import_call;                                                        \
  } catch (const std::exception &e) {                                          \
    throw std::runtime_error(                                                  \
        fmt::format("Failed to import '{}': {}", filename, e.what()));         \
  }

void registerIOBindings(sol::state &lua)
{
  sol::table vsr = lua["vsr"];
  sol::table io = vsr["io"];

  // Importers - geometry/scene formats
  io["importOBJ"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_OBJ(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_OBJ(s, anim, f.c_str(), loc), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          bool useDefaultMat) {
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_OBJ(s, anim, f.c_str(), loc, useDefaultMat), f);
      });

  io["importGLTF"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_GLTF(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_GLTF(s, anim, f.c_str(), loc), f);
      });

  io["importPLY"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PLY(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PLY(s, anim, f.c_str(), loc), f);
      });

  io["importHDRI"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_HDRI(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_HDRI(s, anim, f.c_str(), loc), f);
      });

  // Every USD entry point folds the Stage's reported clock into the shared
  // playback clock, the same way import_file does, so a scripted import
  // scrubs at the Stage's own rate rather than the manager's default.
  auto importUSD = [](scene::Scene &s,
                       animation::AnimationManager &anim,
                       const std::string &f,
                       scene::LayerNodeRef loc,
                       const vsr::io::UsdImportOptions &options) {
    auto report = vsr::io::import_USD(s, anim, f.c_str(), loc, options);
    vsr::io::widenAnimationClock(anim, report);
    return report;
  };

  io["importUSD"] = sol::overload(
      [importUSD](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(importUSD(s, anim, f, {}, {}), f);
      },
      [importUSD](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(importUSD(s, anim, f, loc, {}), f);
      },
      // Settings arrive as a plain table mirroring the option names, so
      // scripted imports can be configured without a binding per field.
      [importUSD](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          sol::table settings) {
        VSR_LUA_IMPORT_WRAP(
            importUSD(s, anim, f, loc, usdImportOptionsFromLuaTable(settings)),
            f);
      });

  io["importPDB"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PDB(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PDB(s, anim, f.c_str(), loc), f);
      });

  io["importPBRT"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PBRT(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PBRT(s, anim, f.c_str(), loc), f);
      });

  io["importSWC"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_SWC(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_SWC(s, anim, f.c_str(), loc), f);
      });

  io["importAGX"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_AGX(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_AGX(s, anim, f.c_str(), loc), f);
      });

  io["importASSIMP"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_ASSIMP(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_ASSIMP(s, anim, f.c_str(), loc), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          bool flatten) {
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_ASSIMP(s, anim, f.c_str(), loc, flatten), f);
      });

  io["importAXYZ"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_AXYZ(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_AXYZ(s, anim, f.c_str(), loc), f);
      });

  io["importDLAF"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_DLAF(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_DLAF(s, anim, f.c_str(), loc), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          bool useDefaultMat) {
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_DLAF(s, anim, f.c_str(), loc, useDefaultMat), f);
      });

  io["importE57XYZ"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_E57XYZ(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_E57XYZ(s, anim, f.c_str(), loc), f);
      });

  io["importENSIGHT"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_ENSIGHT(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_ENSIGHT(s, anim, f.c_str(), loc), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          sol::table fields) {
        std::vector<std::string> fs;
        for (size_t i = 1; i <= fields.size(); i++)
          fs.push_back(fields[i].get<std::string>());
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_ENSIGHT(s, anim, f.c_str(), loc, fs), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          sol::table fields,
          int timestep) {
        std::vector<std::string> fs;
        for (size_t i = 1; i <= fields.size(); i++)
          fs.push_back(fields[i].get<std::string>());
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_ENSIGHT(s, anim, f.c_str(), loc, fs, timestep), f);
      });

  io["importHSMESH"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_HSMESH(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_HSMESH(s, anim, f.c_str(), loc), f);
      });

  io["importNBODY"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_NBODY(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_NBODY(s, anim, f.c_str(), loc), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          bool useDefaultMat) {
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_NBODY(s, anim, f.c_str(), loc, useDefaultMat), f);
      });

  io["importPOINTSBIN"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          sol::table filepaths) {
        std::vector<std::string> paths;
        for (size_t i = 1; i <= filepaths.size(); i++)
          paths.push_back(filepaths[i].get<std::string>());
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_POINTSBIN(s, anim, paths), "POINTSBIN");
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          sol::table filepaths,
          scene::LayerNodeRef loc) {
        std::vector<std::string> paths;
        for (size_t i = 1; i <= filepaths.size(); i++)
          paths.push_back(filepaths[i].get<std::string>());
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_POINTSBIN(s, anim, paths, loc), "POINTSBIN");
      });

  io["importPT"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PT(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_PT(s, anim, f.c_str(), loc), f);
      });

  io["importSilo"] = sol::overload([](scene::Scene &s,
                                       animation::AnimationManager &anim,
                                       const std::string &f,
                                       scene::LayerNodeRef loc) {
    VSR_LUA_IMPORT_WRAP(vsr::io::import_SILO(s, anim, f.c_str(), loc), f);
  });

  io["importSMESH"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_SMESH(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_SMESH(s, anim, f.c_str(), loc), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc,
          bool isAnimation) {
        VSR_LUA_IMPORT_WRAP(
            vsr::io::import_SMESH(s, anim, f.c_str(), loc, isAnimation), f);
      });

  io["importTRK"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_TRK(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_TRK(s, anim, f.c_str(), loc), f);
      });

  io["importXYZDP"] = sol::overload(
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_XYZDP(s, anim, f.c_str()), f);
      },
      [](scene::Scene &s,
          animation::AnimationManager &anim,
          const std::string &f,
          scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP(vsr::io::import_XYZDP(s, anim, f.c_str(), loc), f);
      });

  // Volume importers
  io["importVolume"] = sol::overload(
      [](scene::Scene &s, const std::string &f) {
        VSR_LUA_IMPORT_WRAP_RETURN(vsr::io::import_volume(s, f.c_str()), f);
      },
      [](scene::Scene &s, const std::string &f, scene::LayerNodeRef loc) {
        VSR_LUA_IMPORT_WRAP_RETURN(
            vsr::io::import_volume(s, f.c_str(), loc), f);
      });

  io["importRAW"] = [](scene::Scene &s, const std::string &f) {
    VSR_LUA_IMPORT_WRAP_RETURN(vsr::io::import_RAW(s, f.c_str()), f);
  };

  io["importNVDB"] = [](scene::Scene &s, const std::string &f) {
    VSR_LUA_IMPORT_WRAP_RETURN(vsr::io::import_NVDB(s, f.c_str()), f);
  };

  io["importMHD"] = [](scene::Scene &s, const std::string &f) {
    VSR_LUA_IMPORT_WRAP_RETURN(vsr::io::import_MHD(s, f.c_str()), f);
  };

  io["importFLASH"] = [](scene::Scene &s, const std::string &f) {
    VSR_LUA_IMPORT_WRAP_RETURN(vsr::io::import_FLASH(s, f.c_str()), f);
  };

  io["importVTI"] = [](scene::Scene &s, const std::string &f) {
    VSR_LUA_IMPORT_WRAP_RETURN(vsr::io::import_VTI(s, f.c_str()), f);
  };

  io["importVTU"] = [](scene::Scene &s, const std::string &f) {
    VSR_LUA_IMPORT_WRAP_RETURN(vsr::io::import_VTU(s, f.c_str()), f);
  };

  // Procedural generators
  io["generateRandomSpheres"] =
      sol::overload([](scene::Scene &s) { vsr::io::generate_randomSpheres(s); },
          [](scene::Scene &s, scene::LayerNodeRef loc) {
            vsr::io::generate_randomSpheres(s, loc);
          },
          [](scene::Scene &s, scene::LayerNodeRef loc, bool useDefaultMat) {
            vsr::io::generate_randomSpheres(s, loc, useDefaultMat);
          });

  io["generateIcosphere"] =
      sol::overload([](scene::Scene &s) { vsr::io::generate_icosphere(s); },
          [](scene::Scene &s, scene::LayerNodeRef loc) {
            vsr::io::generate_icosphere(s, loc);
          },
          [](scene::Scene &s, scene::LayerNodeRef loc, uint32_t subdivisions) {
            vsr::io::generate_icosphere(s, loc, subdivisions);
          },
          [](scene::Scene &s,
              scene::LayerNodeRef loc,
              uint32_t subdivisions,
              bool withFloor) {
            vsr::io::generate_icosphere(s, loc, subdivisions, withFloor);
          });

  io["generateMonkey"] =
      sol::overload([](scene::Scene &s) { vsr::io::generate_monkey(s); },
          [](scene::Scene &s, scene::LayerNodeRef loc) {
            vsr::io::generate_monkey(s, loc);
          });

  io["generateCylinders"] =
      sol::overload([](scene::Scene &s) { vsr::io::generate_cylinders(s); },
          [](scene::Scene &s, scene::LayerNodeRef loc) {
            vsr::io::generate_cylinders(s, loc);
          },
          [](scene::Scene &s, scene::LayerNodeRef loc, bool useDefaultMat) {
            vsr::io::generate_cylinders(s, loc, useDefaultMat);
          });

  io["generateDefaultLights"] = [](scene::Scene &s) {
    vsr::io::generate_default_lights(s);
  };

  io["generateEmissiveGeometries"] = sol::overload(
      [](scene::Scene &s) { vsr::io::generate_emissive_geometries(s); },
      [](scene::Scene &s, scene::LayerNodeRef loc) {
        vsr::io::generate_emissive_geometries(s, loc);
      });

  io["generateEmissiveMaterialXComparison"] = sol::overload(
      [](scene::Scene &s) {
        vsr::io::generate_emissive_materialx_comparison(s);
      },
      [](scene::Scene &s, scene::LayerNodeRef loc) {
        vsr::io::generate_emissive_materialx_comparison(s, loc);
      });

  io["generateEmissiveMdlComparison"] = sol::overload(
      [](scene::Scene &s) { vsr::io::generate_emissive_mdl_comparison(s); },
      [](scene::Scene &s, scene::LayerNodeRef loc) {
        vsr::io::generate_emissive_mdl_comparison(s, loc);
      });

  io["generateHdriDome"] =
      sol::overload([](scene::Scene &s) { vsr::io::generate_hdri_dome(s); },
          [](scene::Scene &s, scene::LayerNodeRef loc) {
            vsr::io::generate_hdri_dome(s, loc);
          });

  io["generateRtow"] =
      sol::overload([](scene::Scene &s) { vsr::io::generate_rtow(s); },
          [](scene::Scene &s, scene::LayerNodeRef loc) {
            vsr::io::generate_rtow(s, loc);
          });

  io["generateSphereSetVolume"] = sol::overload(
      [](scene::Scene &s) { vsr::io::generate_sphereSetVolume(s); },
      [](scene::Scene &s, scene::LayerNodeRef loc) {
        vsr::io::generate_sphereSetVolume(s, loc);
      });

  // Utilities
  io["makeDefaultColorMap"] = [](scene::Scene &s, sol::optional<size_t> size) {
    auto colors = core::makeDefaultColorMap(size.value_or(256));
    auto arr = s.createArray(ANARI_FLOAT32_VEC4, colors.size());
    arr->setData(colors.data());
    return arr;
  };

  // Archives
  io["saveSceneArchive"] = [](scene::Scene &s, const std::string &filename) {
    if (!vsr::io::save_SceneArchive(s, filename.c_str()))
      throw std::runtime_error("Failed to save Scene Archive");
  };
  io["loadSceneArchive"] = [](scene::Scene &s, const std::string &filename) {
    if (!vsr::io::load_SceneArchive(s, filename.c_str()))
      throw std::runtime_error("Failed to load Scene Archive");
  };
  io["saveAnimationManagerArchive"] = [](animation::AnimationManager &manager,
                                          const std::string &filename) {
    if (!vsr::io::save_AnimationManagerArchive(manager, filename.c_str()))
      throw std::runtime_error("Failed to save Animation Manager Archive");
  };
  io["loadAnimationManagerArchive"] = [](animation::AnimationManager &manager,
                                          const std::string &filename) {
    if (!vsr::io::load_AnimationManagerArchive(manager, filename.c_str()))
      throw std::runtime_error("Failed to load Animation Manager Archive");
  };
}

#undef VSR_LUA_IMPORT_WRAP
#undef VSR_LUA_IMPORT_WRAP_RETURN

} // namespace vsr::scripting

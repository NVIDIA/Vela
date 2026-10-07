## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_miniscene LANGUAGES CXX)

# miniScene has no releases, so pin a commit. Only the core library is built
# here: miniScene's own CMakeLists always adds its importers and tools, can
# write CMAKE_BUILD_TYPE and output directories, and defines an stb_image
# target -- none of which the exporter needs (see docs/adr/0042).
anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/ingowald/miniScene/archive/ffcb8c5f043e97c01905bff2e127396bdede43de.zip
  MD5 540870baa870817ed3a383f78b6a8f3a
)

# Upstream's SerializedScene only collects textures referenced by Disney and
# Blender materials, so Scene::save() writes every ANARIMaterial texture as
# "no texture". Compile a patched copy until the fix lands upstream; fail
# loudly if a pin bump moves the code the patch targets.
set(MINI_SERIALIZED_SRC ${vela_ext_miniscene_LOCATION}/miniScene/Serialized.cpp)
set(MINI_SERIALIZED_PATCHED ${CMAKE_CURRENT_BINARY_DIR}/Serialized.cpp)
file(READ ${MINI_SERIALIZED_SRC} MINI_SERIALIZED)
set(MINI_PATCH_ANCHOR
"            textures.add(blender->alphaTexture);
          }
")
string(FIND "${MINI_SERIALIZED}" "${MINI_PATCH_ANCHOR}" MINI_PATCH_POS)
if(MINI_PATCH_POS EQUAL -1)
  message(FATAL_ERROR
    "vela_miniscene: Serialized.cpp no longer matches the ANARIMaterial "
    "texture patch; drop or update the patch for this miniScene commit")
endif()
string(REPLACE "${MINI_PATCH_ANCHOR}" "${MINI_PATCH_ANCHOR}\
          ANARIMaterial::SP anari = material->as<ANARIMaterial>();
          if (anari) {
            textures.add(anari->baseColor_texture);
            textures.add(anari->opacity_texture);
            textures.add(anari->metallic_texture);
            textures.add(anari->roughness_texture);
            textures.add(anari->normal_texture);
            textures.add(anari->emissive_texture);
            textures.add(anari->occlusion_texture);
            textures.add(anari->specular_texture);
            textures.add(anari->specularColor_texture);
            textures.add(anari->clearcoat_texture);
            textures.add(anari->clearcoatRoughness_texture);
            textures.add(anari->clearcoatNormal_texture);
            textures.add(anari->transmission_texture);
            textures.add(anari->ior_texture);
            textures.add(anari->thickness_texture);
            textures.add(anari->attenuationColor_texture);
            textures.add(anari->sheenColor_texture);
            textures.add(anari->sheenRoughness_texture);
            textures.add(anari->iridescence_texture);
            textures.add(anari->iridescenceIor_texture);
            textures.add(anari->iridescenceThickness_texture);
          }
" MINI_SERIALIZED "${MINI_SERIALIZED}")
file(CONFIGURE OUTPUT ${MINI_SERIALIZED_PATCHED}
  CONTENT "${MINI_SERIALIZED}" @ONLY)

project_add_library(STATIC
  ${vela_ext_miniscene_LOCATION}/miniScene/Scene.cpp
  ${MINI_SERIALIZED_PATCHED}
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${vela_ext_miniscene_LOCATION}>
)

# miniScene's parallel_for only uses TBB when OWL_HAVE_TBB is set; keep it
# serial so the library needs nothing beyond its own headers.
project_compile_definitions(PUBLIC OWL_DISABLE_TBB)

set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Suppress warnings from miniScene code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  project_compile_options(PRIVATE -w)
elseif(MSVC)
  project_compile_options(PRIVATE /W0)
endif()

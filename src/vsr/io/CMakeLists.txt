## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsr_io)

project_add_library(STATIC)

project_sources(
PRIVATE
  UsdImport.cpp
  archives/SceneArchive.cpp
  archives/ObjectArchive.cpp
  archives/LayerSubtreeArchive.cpp
  archives/CameraArchive.cpp
  archives/AnimationArchive.cpp
  archives/AnimationManagerArchive.cpp
  archives/RendererArchive.cpp
  archives/SubtreeArchiveContent.cpp
  archives/detail/PoolArchive.cpp
  archives/detail/AnimationRemap.cpp
  archives/detail/ArchiveClosure.cpp
  archives/detail/ArchivePlan.cpp
  archives/detail/SubtreeArchiveDeserialization.cpp
  animation/EnSightFileBinding.cpp
  animation/SpatialFieldFileBinding.cpp
  animation/UsdFileBinding.cpp
  animation/UsdGeometryFileBinding.cpp
  animation/UsdInstancerFileBinding.cpp
  exporters/NanoVdbSidecar.cpp
  exporters/SceneToUSD.cpp
  exporters/StructuredVolumeToNanoVDB.cpp
  images/ImageCache.cpp
  images/detail/decoders.cpp
  importers/detail/ensight_io.cpp
  importers/detail/HDRImage.cpp
  importers/detail/importer_common.cpp
  importers/detail/pbrt/PbrtLexer.cpp
  importers/detail/pbrt/PbrtParser.cpp
  importers/detail/pbrt/PbrtScene.cpp
  importers/import_AGX.cpp
  importers/import_ASSIMP.cpp
  importers/import_AXYZ.cpp
  importers/import_DLAF.cpp
  importers/import_E57XYZ.cpp
  importers/import_ENSIGHT.cpp
  importers/import_file.cpp
  importers/import_FLASH.cpp
  importers/import_GLTF.cpp
  importers/import_HDRI.cpp
  importers/import_HSMESH.cpp
  importers/import_MHD.cpp
  importers/import_NBODY.cpp
  importers/import_NVDB.cpp
  importers/import_OBJ.cpp
  importers/import_PBRT.cpp
  importers/import_USD.cpp
  importers/import_PDB.cpp
  importers/import_PLY.cpp
  importers/import_POINTSBIN.cpp
  importers/import_PT.cpp
  importers/import_RAW.cpp
  importers/import_SILO.cpp
  importers/import_SMESH.cpp
  importers/import_SWC.cpp
  importers/import_SWC_SDF.cpp
  importers/import_TRK.cpp
  importers/import_volume.cpp
  importers/import_VTI.cpp
  importers/import_VTP.cpp
  importers/import_VTU.cpp
  importers/import_XYZDP.cpp
  procedural/computeVorticity.cpp
  procedural/detail/checkerboard.cpp
  procedural/generate_cylinders.cpp
  procedural/generate_default_lights.cpp
  procedural/generate_emissive_geometries.cpp
  procedural/generate_emissive_materialx_comparison.cpp
  procedural/generate_emissive_mdl_comparison.cpp
  procedural/generate_hdri_dome.cpp
  procedural/generate_hdri_test_image.cpp
  procedural/generate_icosphere.cpp
  procedural/generate_monkey.cpp
  procedural/generate_noiseVolume.cpp
  procedural/generate_randomSpheres.cpp
  procedural/generate_rtow.cpp
  procedural/generate_sphereSetVolume.cpp
  serialization/Layer.cpp
  serialization/Object.cpp
  serialization/Parameter.cpp
  serialization/serialization_datatree.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>
)

project_link_libraries(
PUBLIC
  vsr_animation vsr_scene
PRIVATE
  $<BUILD_INTERFACE:vela_ext_agx>
  $<BUILD_INTERFACE:vela_ext_mikktspace>
  $<BUILD_INTERFACE:vela_ext_nanovdb>
  $<BUILD_INTERFACE:vela_ext_stb_image>
  $<BUILD_INTERFACE:vela_ext_tinyexr>
  $<BUILD_INTERFACE:vela_ext_tinyexr_impl>
  $<BUILD_INTERFACE:vela_ext_tinyobjloader>
  $<BUILD_INTERFACE:vela_ext_tinyply>
  $<BUILD_INTERFACE:vela_ext_tinygltf>
)

# Always use nanovdb
project_link_libraries(PRIVATE vela_ext_nanovdb)


## Setup optional dependencies ##

# PUBLIC so the test target can assert on the ASSIMP importer only where it
# exists, the same widening VSR_USE_OIIO and VSR_USE_USD make below.
if (VSR_USE_ASSIMP)
  find_package(assimp REQUIRED)
  # Debian's assimpTargets.cmake puts a bare "pugixml" in the interface, which
  # over-links the system libpugixml into every consumer. libassimp.so already
  # carries its own DT_NEEDED for it, so the entry is redundant -- and harmful:
  # a direct DT_NEEDED on the executable loads system pugixml ahead of the copy
  # MaterialX resolves against, which silently breaks UDIM texture paths.
  get_target_property(_assimp_iface assimp::assimp INTERFACE_LINK_LIBRARIES)
  if (_assimp_iface AND "pugixml" IN_LIST _assimp_iface)
    list(REMOVE_ITEM _assimp_iface "pugixml")
    set_target_properties(assimp::assimp
      PROPERTIES INTERFACE_LINK_LIBRARIES "${_assimp_iface}")
  endif()
  project_compile_definitions(PUBLIC -DVSR_USE_ASSIMP=1)
  project_link_libraries(PRIVATE assimp::assimp)
else()
  project_compile_definitions(PUBLIC -DVSR_USE_ASSIMP=0)
endif()

if (VSR_USE_HDF5)
  find_package(HDF5 REQUIRED COMPONENTS CXX)
  project_compile_definitions(PRIVATE -DVSR_USE_HDF5=1)
  project_link_libraries(PRIVATE HDF5::HDF5)
else()
  project_compile_definitions(PRIVATE -DVSR_USE_HDF5=0)
endif()

# OpenUSD's Hio was the alternative here, since a USD-enabled build already
# links it -- but base Hio decodes TIFF only through its hioOiio plugin, which
# is not built in the OpenUSD installs this importer targets (they ship
# hioOpenEXR and hioAvif). Going via Hio would therefore still require
# OpenImageIO, with an extra plugin indirection in front of it, so depend on
# OpenImageIO directly and keep the TIFF path available to non-USD builds too.
#
# PUBLIC so the test target can assert on the TIFF path only where a decoder
# exists, the same widening VSR_USE_USD makes for the same reason.
if (VSR_USE_OIIO)
  find_package(OpenImageIO REQUIRED)
  project_compile_definitions(PUBLIC -DVSR_USE_OIIO=1)
  project_link_libraries(PRIVATE OpenImageIO::OpenImageIO)
else()
  project_compile_definitions(PUBLIC -DVSR_USE_OIIO=0)
endif()

if(VSR_USE_TORCH)
  find_package(Torch REQUIRED)
  if (MSVC)
    set_source_files_properties(
      authoring/importers/import_PT.cpp
      PROPERTIES COMPILE_OPTIONS "/arch:AVX"
    )
  else()
    set_source_files_properties(
      authoring/importers/import_PT.cpp
      PROPERTIES COMPILE_OPTIONS "-mf16c"
    )
  endif()
  project_link_libraries(PRIVATE ${TORCH_LIBRARIES})
  project_compile_definitions(PUBLIC VSR_USE_TORCH=1)
else()
  project_compile_definitions(PUBLIC VSR_USE_TORCH=0)
endif()

if (VSR_USE_USD)
  find_package(OpenGL)
  find_package(X11)
  # The imaging components (usdImaging/hd/hdsi/hio/pxOsd) are what the USD
  # importer consumes: it reads OpenUSD's own resolved scene through a Hydra
  # scene index rather than traversing schemas (ADR 0015). None of them carry a
  # GL or X11 link dependency, so headless and CI builds are unaffected.
  find_package(pxr REQUIRED
    sdf usd usdGeom usdShade usdLux usdSkel usdVol
    tf gf vt work plug
    usdImaging hd hdsi hio pxOsd cameraUtil
  )
  # OpenSubdiv is required for subdivision-surface refinement; pxOsd's refiner
  # factory includes OpenSubdiv headers that are not shipped inside the USD
  # install, so a separate CPU-only OpenSubdiv is needed.
  find_package(OpenSubdiv REQUIRED)
  # PUBLIC so that consumers -- the test target in particular -- can compile
  # conditionally against USD support. A deliberate widening of the library's
  # interface made for testability.
  project_compile_definitions(PUBLIC -DVSR_USE_USD=1)
  project_link_libraries(PRIVATE ${PXR_LIBRARIES} OpenSubdiv::osdCPU)

  # MaterialX passthrough needs OpenUSD's own HdMtlx document conversion, which
  # is only present when OpenUSD was built with PXR_ENABLE_MATERIALX_SUPPORT.
  # Detect it rather than requiring it, so installs without it still build --
  # the importer reports the mode as unavailable and emits portable materials.
  # pxrConfig already points MaterialX_DIR at the build OpenUSD was linked
  # against, so no extra hint is normally needed.
  find_package(MaterialX QUIET)
  if (MaterialX_FOUND
      AND EXISTS "${PXR_INCLUDE_DIRS}/pxr/imaging/hdMtlx/hdMtlx.h")
    project_compile_definitions(PUBLIC -DVSR_USD_HAS_MATERIALX=1)
    project_link_libraries(PRIVATE MaterialXCore MaterialXFormat)
    message(STATUS "VSR: USD import has native MaterialX passthrough")
  else()
    project_compile_definitions(PUBLIC -DVSR_USD_HAS_MATERIALX=0)
    message(STATUS
      "VSR: OpenUSD lacks hdMtlx -- USD import will report MaterialX"
      " passthrough as unavailable and emit portable materials instead")
  endif()

  # The USD importer's converters, which have no meaning without OpenUSD.
  project_sources(
  PRIVATE
    importers/detail/usd/UsdAnimation.cpp
    importers/detail/usd/UsdDialect.cpp
    importers/detail/usd/UsdGeometry.cpp
    importers/detail/usd/UsdImportContext.cpp
    importers/detail/usd/UsdInstancing.cpp
    importers/detail/usd/UsdLights.cpp
    importers/detail/usd/UsdMaterials.cpp
    importers/detail/usd/UsdSubdivision.cpp
    importers/detail/usd/UsdVolume.cpp
    usd/UsdResolvedGeometry.cpp
    usd/UsdStageSession.cpp
  )

  # CaseFileFormat -- SdfFileFormat plugin for EnSight .case files
  add_library(CaseFileFormat SHARED
    usd/CaseFileFormat.cpp
  )
  target_link_libraries(CaseFileFormat
    PRIVATE vsr_io ${PXR_LIBRARIES}
  )
  target_include_directories(CaseFileFormat
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../..
  )
  set_target_properties(CaseFileFormat PROPERTIES
    PREFIX "lib"
  )

  set(CASEFILEFORMAT_PLUGIN_FILENAME
    "${CMAKE_SHARED_LIBRARY_PREFIX}CaseFileFormat${CMAKE_SHARED_LIBRARY_SUFFIX}")
  configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/usd/plugInfo.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/usd/plugInfo.json
    @ONLY
  )

  install(TARGETS CaseFileFormat
    RUNTIME DESTINATION lib
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/usd/plugInfo.json
    DESTINATION lib/usd/CaseFileFormat/resources)
else()
  project_compile_definitions(PUBLIC -DVSR_USE_USD=0)
endif()

if (VSR_USE_VTK)
  # Request only the modules the importers below actually use. A bare
  # find_package(VTK) pulls in all ~175 VTK modules, which lands 165 shared
  # libraries on the link line of every executable that consumes vsr_io --
  # including the tutorials, which use none of them.
  find_package(VTK REQUIRED
  COMPONENTS
    CommonCore        # vtkDataArray, vtkPoints, vtkVariant, vtkNew, ...
    CommonDataModel   # vtkImageData, vtkPolyData, vtkUnstructuredGrid, ...
    FiltersCore       # vtkTriangleFilter, vtkProbeFilter
    FiltersGeneral    # vtkGradientFilter
    IOLegacy          # vtkUnstructuredGridReader
    IOXML             # vtkXML{ImageData,PolyData,UnstructuredGrid}Reader
  )
  project_link_libraries(PRIVATE ${VTK_LIBRARIES})
  project_compile_definitions(PRIVATE -DVSR_USE_VTK=1)
else()
  project_compile_definitions(PRIVATE -DVSR_USE_VTK=0)
endif()

if (VSR_USE_SILO)
  find_package(vela_silo MODULE REQUIRED)
  project_link_libraries(PRIVATE vela::silo)
  project_compile_definitions(PRIVATE -DVSR_USE_SILO=1)
else()
  project_compile_definitions(PRIVATE -DVSR_USE_SILO=0)
endif()

if (VSR_NANOVDB_SKIP_INVALID_VOLUMES)
  project_compile_definitions(PRIVATE -DVSR_NANOVDB_SKIP_INVALID_VOLUMES=1)
else()
  project_compile_definitions(PRIVATE -DVSR_NANOVDB_SKIP_INVALID_VOLUMES=0)
endif()

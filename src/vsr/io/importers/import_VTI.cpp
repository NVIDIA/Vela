// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef VSR_USE_VTK
#define VSR_USE_VTK 1
#endif

#include "vsr/core/Logging.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/io/importers/detail/importer_common.hpp"
#if VSR_USE_VTK
// vtk
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkXMLImageDataReader.h>
#endif
// std
#include <iomanip>
#include <iostream>
#include <vector>

namespace vsr::io {

#if VSR_USE_VTK
SpatialFieldRef import_VTI(Scene &scene,
    const char *filepath,
    LayerNodeRef /*location*/,
    std::vector<SpatialFieldRef> *extraFields)
{
  vtkNew<vtkXMLImageDataReader> reader;
  reader->SetFileName(filepath);
  reader->Update();

  vtkImageData *grid = reader->GetOutput();

  if (!grid) {
    logError("[import_VTI] failed to load .vti file '%s'", filepath);
    return {};
  }

  int dims[3] = {0, 0, 0};
  double spacing[3] = {1.0, 1.0, 1.0};
  double origin[3] = {0.0, 0.0, 0.0};

  grid->GetDimensions(dims);
  grid->GetSpacing(spacing);
  grid->GetOrigin(origin);

  auto field = scene.createObject<SpatialField>(
      tokens::spatial_field::structuredRegular);
  field->setName(fileOf(filepath).c_str());
  field->setParameter("origin", float3(origin[0], origin[1], origin[2]));
  field->setParameter("spacing", float3(spacing[0], spacing[1], spacing[2]));

  // --- Write point data arrays ---
  vtkPointData *pointData = grid->GetPointData();
  bool found = false;
  bool firstField = true;

  auto makeField = [&](const std::string &name) -> SpatialFieldRef {
    auto f = scene.createObject<SpatialField>(
        tokens::spatial_field::structuredRegular);
    f->setName(name.c_str());
    f->setParameter("origin", float3(origin[0], origin[1], origin[2]));
    f->setParameter("spacing", float3(spacing[0], spacing[1], spacing[2]));
    return f;
  };

  auto storeExtra = [&](SpatialFieldRef &f) {
    if (extraFields)
      extraFields->push_back(f);
  };

  for (uint32_t i = 0; i < pointData->GetNumberOfArrays(); ++i) {
    vtkDataArray *array = pointData->GetArray(i);
    const char *arrName = array->GetName();
    std::string baseName = (arrName && arrName[0] != '\0')
        ? std::string(arrName)
        : fileOf(filepath);

    int numComponents = array->GetNumberOfComponents();

    if (numComponents == 1) {
      auto a = makeArray3DFromVTK(
          scene, array, dims[0], dims[1], dims[2], "[import_VTI]");
      if (firstField) {
        field->setName(baseName.c_str());
        field->setParameterObject("data", *a);
        firstField = false;
      } else {
        auto extra = makeField(baseName);
        extra->setParameterObject("data", *a);
        storeExtra(extra);
      }
      found = true;
    } else if (numComponents == 3) {
      // Split into 3 scalar SpatialFields: {name}_x, {name}_y, {name}_z
      std::string nameX = baseName + "_x";
      std::string nameY = baseName + "_y";
      std::string nameZ = baseName + "_z";

      size_t n = (size_t)dims[0] * (size_t)dims[1] * (size_t)dims[2];

      auto arrX = scene.createArray(ANARI_FLOAT32, dims[0], dims[1], dims[2]);
      auto arrY = scene.createArray(ANARI_FLOAT32, dims[0], dims[1], dims[2]);
      auto arrZ = scene.createArray(ANARI_FLOAT32, dims[0], dims[1], dims[2]);

      float *pX = arrX->mapAs<float>();
      float *pY = arrY->mapAs<float>();
      float *pZ = arrZ->mapAs<float>();

      for (vtkIdType idx = 0; idx < (vtkIdType)n; ++idx) {
        double tuple[3] = {0.0, 0.0, 0.0};
        array->GetTuple(idx, tuple);
        pX[idx] = (float)tuple[0];
        pY[idx] = (float)tuple[1];
        pZ[idx] = (float)tuple[2];
      }

      arrX->unmap();
      arrY->unmap();
      arrZ->unmap();

      if (firstField) {
        field->setName(nameX.c_str());
        field->setParameterObject("data", *arrX);
        firstField = false;
      } else {
        auto fieldX = makeField(nameX);
        fieldX->setParameterObject("data", *arrX);
        storeExtra(fieldX);
      }

      auto fieldY = makeField(nameY);
      fieldY->setParameterObject("data", *arrY);
      storeExtra(fieldY);

      auto fieldZ = makeField(nameZ);
      fieldZ->setParameterObject("data", *arrZ);
      storeExtra(fieldZ);

      logStatus(
          "[import_VTI] split 3-component array '%s' into '%s', '%s', '%s'",
          baseName.c_str(),
          nameX.c_str(),
          nameY.c_str(),
          nameZ.c_str());
      found = true;
    } else {
      logWarning(
          "[import_VTI] array '%s' has %d components (only 1 or 3 are "
          "supported) -- skipping",
          array->GetName(),
          numComponents);
    }
  }

  if (!found) {
    logError(
        "[import_VTI] '%s': no usable point data arrays found "
        "(only 1- and 3-component arrays are supported; file has %d array(s))",
        filepath,
        pointData->GetNumberOfArrays());
    return {};
  }

  return field;
}
#else
SpatialFieldRef import_VTI(Scene &scene,
    const char *filepath,
    LayerNodeRef,
    std::vector<SpatialFieldRef> *)
{
  logError("[import_VTI] VTK not enabled in VSR build.");
  return {};
}
#endif

} // namespace vsr::io

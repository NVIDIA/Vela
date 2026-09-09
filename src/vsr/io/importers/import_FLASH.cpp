// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef VSR_USE_HDF5
#define VSR_USE_HDF5 1
#endif

#include "vsr/core/Logging.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/io/importers/detail/importer_common.hpp"
#if VSR_USE_HDF5
// std
#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>
// hdf5
#include <H5Cpp.h>
#endif

namespace vsr::io {

using namespace vsr::core;

#if VSR_USE_HDF5

#define MAX_STRING_LENGTH 80

using BlockBounds = std::array<int, 6>;
struct BlockData
{
  int dims[3];
  std::vector<float> values;
};
struct AMRField
{
  std::vector<int> blockLevel;
  std::vector<vsr::math::int3> blockOrigins;
  std::vector<vsr::math::int3> blockDims;
  std::vector<float> data;
  struct
  {
    float x, y;
  } voxelRange;
};

struct sim_info_t
{
  int file_format_version;
  char setup_call[400];
  char file_creation_time[MAX_STRING_LENGTH];
  char flash_version[MAX_STRING_LENGTH];
  char build_date[MAX_STRING_LENGTH];
  char build_dir[MAX_STRING_LENGTH];
  char build_machine[MAX_STRING_LENGTH];
  char cflags[400];
  char fflags[400];
  char setup_time_stamp[MAX_STRING_LENGTH];
  char build_time_stamp[MAX_STRING_LENGTH];
};

struct grid_t
{
  using char4 = std::array<char, 4>;
  struct __attribute__((packed)) vec3d
  {
    double x, y, z;
  };

  struct aabbd
  {
    vec3d min, max;
  };

  struct __attribute__((packed)) gid_t
  {
    int neighbors[6];
    int parent;
    int children[8];
  };

  std::vector<char4> unknown_names;
  std::vector<int> refine_level;
  std::vector<int> node_type; // node_type 1 ==> leaf
  std::vector<gid_t> gid;
  std::vector<vec3d> coordinates;
  std::vector<vec3d> block_size;
  std::vector<aabbd> bnd_box;
  std::vector<int> which_child;
};

struct variable_t
{
  size_t global_num_blocks;
  size_t nxb;
  size_t nyb;
  size_t nzb;
  std::vector<double> data;
};

inline void read_sim_info(sim_info_t &dest, H5::H5File const &file)
{
  H5::StrType str80(H5::PredType::C_S1, 80);
  H5::StrType str400(H5::PredType::C_S1, 400);

  H5::CompType ct(sizeof(sim_info_t));
  ct.insertMember("file_format_version", 0, H5::PredType::NATIVE_INT);
  ct.insertMember("setup_call", 4, str400);
  ct.insertMember("file_creation_time", 404, str80);
  ct.insertMember("flash_version", 484, str80);
  ct.insertMember("build_date", 564, str80);
  ct.insertMember("build_dir", 644, str80);
  ct.insertMember("build_machine", 724, str80);
  ct.insertMember("cflags", 804, str400);
  ct.insertMember("fflags", 1204, str400);
  ct.insertMember("setup_time_stamp", 1604, str80);
  ct.insertMember("build_time_stamp", 1684, str80);

  H5::DataSet dataset = file.openDataSet("sim info");

  dataset.read(&dest, ct);
}

inline void read_grid(grid_t &dest, H5::H5File const &file)
{
  H5::DataSet dataset;
  H5::DataSpace dataspace;

  {
    H5::StrType str4(H5::PredType::C_S1, 4);

    dataset = file.openDataSet("unknown names");
    dataspace = dataset.getSpace();
    dest.unknown_names.resize(dataspace.getSimpleExtentNpoints());
    dataset.read(dest.unknown_names.data(), str4, dataspace, dataspace);
  }

  {
    dataset = file.openDataSet("refine level");
    dataspace = dataset.getSpace();
    dest.refine_level.resize(dataspace.getSimpleExtentNpoints());
    dataset.read(dest.refine_level.data(),
        H5::PredType::NATIVE_INT,
        dataspace,
        dataspace);
  }

  {
    dataset = file.openDataSet("node type");
    dataspace = dataset.getSpace();
    dest.node_type.resize(dataspace.getSimpleExtentNpoints());
    dataset.read(
        dest.node_type.data(), H5::PredType::NATIVE_INT, dataspace, dataspace);
  }

  {
    dataset = file.openDataSet("gid");
    dataspace = dataset.getSpace();

    hsize_t dims[2];
    dataspace.getSimpleExtentDims(dims);
    dest.gid.resize(dims[0]);
    assert(dims[1] == 15);

    dataset.read(
        dest.gid.data(), H5::PredType::NATIVE_INT, dataspace, dataspace);
  }

  {
    dataset = file.openDataSet("coordinates");
    dataspace = dataset.getSpace();

    hsize_t dims[2];
    dataspace.getSimpleExtentDims(dims);
    dest.coordinates.resize(dims[0]);
    assert(dims[1] == 3);

    dataset.read(dest.coordinates.data(),
        H5::PredType::NATIVE_DOUBLE,
        dataspace,
        dataspace);
  }

  {
    dataset = file.openDataSet("block size");
    dataspace = dataset.getSpace();

    hsize_t dims[2];
    dataspace.getSimpleExtentDims(dims);
    dest.block_size.resize(dims[0]);
    assert(dims[1] == 3);

    dataset.read(dest.block_size.data(),
        H5::PredType::NATIVE_DOUBLE,
        dataspace,
        dataspace);
  }

  {
    dataset = file.openDataSet("bounding box");
    dataspace = dataset.getSpace();

    hsize_t dims[3];
    dataspace.getSimpleExtentDims(dims);
    dest.bnd_box.resize(dims[0] * 2);
    assert(dims[1] == 3);
    assert(dims[2] == 2);

    std::vector<double> temp(dims[0] * dims[1] * dims[2]);

    dataset.read(
        temp.data(), H5::PredType::NATIVE_DOUBLE, dataspace, dataspace);

    dest.bnd_box.resize(dims[0]);
    for (size_t i = 0; i < dims[0]; ++i) {
      dest.bnd_box[i].min.x = temp[i * 6];
      dest.bnd_box[i].max.x = temp[i * 6 + 1];
      dest.bnd_box[i].min.y = temp[i * 6 + 2];
      dest.bnd_box[i].max.y = temp[i * 6 + 3];
      dest.bnd_box[i].min.z = temp[i * 6 + 4];
      dest.bnd_box[i].max.z = temp[i * 6 + 5];
    }
  }

  {
    dataset = file.openDataSet("which child");
    dataspace = dataset.getSpace();
    dest.which_child.resize(dataspace.getSimpleExtentNpoints());
    dataset.read(dest.which_child.data(),
        H5::PredType::NATIVE_INT,
        dataspace,
        dataspace);
  }
}

inline void read_variable(
    variable_t &var, H5::H5File const &file, char const *varname)
{
  H5::DataSet dataset = file.openDataSet(varname);
  H5::DataSpace dataspace = dataset.getSpace();

  hsize_t dims[4];
  dataspace.getSimpleExtentDims(dims);
  var.global_num_blocks = dims[0];
  var.nxb = dims[1];
  var.nyb = dims[2];
  var.nzb = dims[3];
  var.data.resize(dims[0] * dims[1] * dims[2] * dims[3]);
  dataset.read(
      var.data.data(), H5::PredType::NATIVE_DOUBLE, dataspace, dataspace);
}

struct box3d
{
  box3d() : lower(INFINITY), upper(INFINITY) {}
  box3d(double *arr) : lower(arr + 0), upper(arr + 3) {}
  box3d(const grid_t::aabbd &bb)
      : lower(bb.min.x, bb.min.y, bb.min.z), upper(bb.max.x, bb.max.y, bb.max.z)
  {}
  math::double3 size() const
  {
    return upper - lower;
  }
  box3d &extend(const box3d &other)
  {
    lower = min(lower, other.lower);
    upper = min(upper, other.upper);
    return *this;
  }
  math::double3 lower, upper;
};

inline AMRField toAMRField(const grid_t &grid, const variable_t &var)
{
  AMRField result;

  int numBlocks = grid.coordinates.size();
  math::int3 blockDims = math::int3(var.nxb, var.nyb, var.nzb);

  float max_scalar = -FLT_MAX;
  float min_scalar = FLT_MAX;

  math::double3 minBlockSize(INFINITY);
  math::double3 maxBlockSize(0.);
  box3d worldBounds;
  for (int i = 0; i < numBlocks; i++) {
    box3d blockBounds = grid.bnd_box[i];
    worldBounds.extend(blockBounds);
    minBlockSize = min(minBlockSize, blockBounds.size());
    maxBlockSize = max(maxBlockSize, blockBounds.size());
  }
  math::double3 unitCellSize = maxBlockSize / math::double3(blockDims);
  math::int3 unitGridDims = math::int3(worldBounds.size() / unitCellSize + .5);

  int maxRefine = 1;
  int maxLevel = 0;
  for (int i = 0; i < numBlocks; ++i) {
    box3d blockBounds = grid.bnd_box[i];
    math::double3 cellSize = blockBounds.size() / math::double3(blockDims);

    math::int3 origin =
        math::int3((blockBounds.lower - worldBounds.lower) / cellSize + .5);

    for (int z = 0; z < var.nzb; ++z) {
      for (int y = 0; y < var.nyb; ++y) {
        for (int x = 0; x < var.nxb; ++x) {
          size_t index = i * var.nxb * var.nyb * var.nzb + z * var.nyb * var.nxb
              + y * var.nxb + x;
          double val = var.data[index];
          val = val == 0.0 ? 0.0 : log(val);
          float valf(val);
          min_scalar = fminf(min_scalar, valf);
          max_scalar = fmaxf(max_scalar, valf);
          result.data.push_back((float)val);
        }
      }
    }

    result.blockLevel.push_back(grid.refine_level[i]);
    result.blockOrigins.push_back(origin);
    result.blockDims.push_back(blockDims);
    result.voxelRange = {min_scalar, max_scalar};
  }

  logStatus("[import_FLASH] --> value range: %f, %f", min_scalar, max_scalar);

  return result;
}

struct FlashReader
{
  bool open(const char *fileName)
  {
    if (!H5::H5File::isHdf5(fileName))
      return false;

    try {
      file = H5::H5File(fileName, H5F_ACC_RDONLY);
      // Read simulation info
      sim_info_t sim_info;
      read_sim_info(sim_info, file);

      // Read grid data
      read_grid(grid, file);

      logStatus("[import_FLASH] variables found:");
      for (std::size_t i = 0; i < grid.unknown_names.size(); ++i) {
        std::string uname(
            grid.unknown_names[i].data(), grid.unknown_names[i].data() + 4);
        logStatus("    %s", uname.c_str());
        fieldNames.push_back(uname);
      }
    } catch (H5::FileIException error) {
      error.printErrorStack();
      return false;
    }

    return true;
  }

  AMRField getField(int index)
  {
    try {
      logStatus(
          "[import_FLASH] reading field '%s'...", fieldNames[index].c_str());
      read_variable(currentField, file, fieldNames[index].c_str());
      logStatus("[import_FLASH] converting to AMRField...");
      return toAMRField(grid, currentField);
    } catch (H5::DataSpaceIException error) {
      error.printErrorStack();
      exit(EXIT_FAILURE);
    } catch (H5::DataTypeIException error) {
      error.printErrorStack();
      exit(EXIT_FAILURE);
    }

    return {};
  }

  H5::H5File file;
  std::vector<std::string> fieldNames;
  grid_t grid;
  variable_t currentField;
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

SpatialFieldRef import_FLASH(Scene &scene, const char *filepath)
{
  std::string file = fileOf(filepath);

  FlashReader reader;
  if (!reader.open(filepath)) {
    logError("[import_FLASH] failed to open file '%s'", filepath);
    return {};
  }

  auto field = scene.createObject<SpatialField>(tokens::spatial_field::amr);
  field->setName(file.c_str());

  AMRField amrField = reader.getField(0);

  logStatus("[import_FLASH] converting to VSR objects...");

  auto data = scene.createArray(ANARI_FLOAT32, amrField.data.size());
  data->setData(amrField.data.data());

  auto blockOrigins =
      scene.createArray(ANARI_INT32_VEC3, amrField.blockOrigins.size());
  blockOrigins->setData(amrField.blockOrigins.data());

  auto blockDims =
      scene.createArray(ANARI_INT32_VEC3, amrField.blockDims.size());
  blockDims->setData(amrField.blockDims.data());

  auto blockLevel = scene.createArray(ANARI_INT32, amrField.blockLevel.size());
  blockLevel->setData(amrField.blockLevel);

  field->setParameterObject("block.origin", *blockOrigins);
  field->setParameterObject("block.dimensions", *blockDims);
  field->setParameterObject("block.level", *blockLevel);
  field->setParameterObject("data", *data);

  logStatus("[import_FLASH] ...done!");

  return field;
}
#else
SpatialFieldRef import_FLASH(Scene &, const char *)
{
  logError("[import_FLASH] HDF5 not enabled in VSR build.");
  return {};
}
#endif

} // namespace vsr::io

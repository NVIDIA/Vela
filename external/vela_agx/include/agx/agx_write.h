// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// C-style API in C++ for animated geometry export, ANARI-style.

// File format (v1, host-endian; an endianness marker is included):
//   Header:
//     char[4]   magic = "AGXB"
//     uint32_t  version = 1
//     uint32_t  endianMarker = 0x01020304
//     uint32_t  objectType
//     uint32_t  timeSteps
//     uint32_t  constantParamCount
//
//   Optional subtype string:
//     uint32_t  subtypeLen
//     char[]    subtype (subtypeLen bytes, not null-terminated)
//
//   Constant parameter records (constantParamCount times):
//     uint32_t  nameLen
//     char[]    name (nameLen bytes, not null-terminated)
//     uint8_t   isArray (0 = value, 1 = array)
//     if isArray == 0:
//       uint32_t  type       (ANARIDataType)
//       uint32_t  valueBytes (N)
//       uint8_t[] value (N bytes)
//     else:
//       uint32_t  elementType (ANARIDataType)
//       uint64_t  elementCount
//       uint64_t  dataBytes (M)
//       uint8_t[] data (M bytes; M == elementCount * sizeof(elementType))
//
//   For each time step (timeSteps times):
//     uint32_t  timeStepIndex
//     uint32_t  paramCount
//     paramCount parameter records (same layout as above)
//
// Notes:
// - Values are written in host endianness; the endianMarker lets a reader
// detect endianness.
// - Unknown ANARIDataType values will have size 0 and thus write zero bytes of
// payload.
// - Strings are stored without a trailing NUL.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Use ANARI's logical type enums.
#include <anari/frontend/anari_enums.h>

// Opaque exporter handle
typedef struct AGXExporter_t *AGXExporter;

// Create/destroy exporter
AGXExporter agxNewExporter();
void agxReleaseExporter(AGXExporter exporter);

// Set object subtype (optional, default = "")
void agxSetObjectSubtype(AGXExporter exporter, const char *subtype);

// Animation time steps
void agxSetTimeStepCount(AGXExporter exporter, uint32_t count);
uint32_t agxGetTimeStepCount(AGXExporter exporter);

// Optional begin/end bracketing of per-time step edits (no-op but keeps
// ANARI-like style)
void agxBeginTimeStep(AGXExporter exporter, uint32_t timeStepIndex);
void agxEndTimeStep(AGXExporter exporter, uint32_t timeStepIndex);

// Set constant parameters (across the whole animation)
void agxSetParameter(AGXExporter exporter,
    const char *name,
    ANARIDataType type,
    const void *value /* pointer to a single value of 'type' */);

void agxSetParameterArray1D(AGXExporter exporter,
    const char *name,
    ANARIDataType elementType,
    const void *data,
    uint64_t elementCount);

// Set per-time-step parameters
void agxSetTimeStepParameter(AGXExporter exporter,
    uint32_t timeStepIndex,
    const char *name,
    ANARIDataType type,
    const void *value);

void agxSetTimeStepParameterArray1D(AGXExporter exporter,
    uint32_t timeStepIndex,
    const char *name,
    ANARIDataType elementType,
    const void *data,
    uint64_t elementCount);

// Write out dump to a file (returns 0 on success, nonzero on error)
int agxWrite(AGXExporter exporter, const char *filename);

// Helpers
size_t agxSizeOf(ANARIDataType type);
const char *agxDataTypeToString(ANARIDataType type);

#ifdef __cplusplus
} // extern "C"
#endif

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#ifdef AGX_WRITE_IMPL
// anari
#include <anari/frontend/type_utility.h>
// std
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Internal representation of parameter data
struct ParamData
{
  bool isArray{false};
  ANARIDataType type{ANARI_UNKNOWN}; // for single value
  ANARIDataType elementType{ANARI_UNKNOWN}; // for arrays
  uint64_t elementCount{0}; // for arrays
  std::vector<uint8_t> bytes; // raw bytes
};

struct AGXExporter_t
{
  std::string subtype; // optional
  uint32_t timeSteps{0};
  std::unordered_map<std::string, ParamData> constants;
  std::vector<std::unordered_map<std::string, ParamData>>
      perTimeStep; // size = timeSteps
};

static inline uint32_t clampToValidIndex(uint32_t idx, uint32_t max)
{
  return std::min(idx, max);
}

// Sizeof mapping for a subset of ANARIDataType values
size_t agxSizeOf(ANARIDataType t)
{
  return anari::sizeOf(t);
}

// Optional stringification helper (kept for header API completeness)
const char *agxDataTypeToString(ANARIDataType t)
{
  return anari::toString(t);
}

static void copyBytes(ParamData &dst, const void *src, size_t nbytes)
{
  dst.bytes.resize(nbytes);
  if (src && nbytes > 0)
    std::memcpy(dst.bytes.data(), src, nbytes);
}

// Write helpers
template <typename T>
static bool writePOD(std::FILE *f, const T &v)
{
  return std::fwrite(&v, sizeof(T), 1, f) == 1;
}

static bool writeBytes(std::FILE *f, const void *data, size_t n)
{
  if (n == 0)
    return true;
  return std::fwrite(data, 1, n, f) == n;
}

static bool writeString(std::FILE *f, const std::string &s)
{
  uint32_t len = static_cast<uint32_t>(s.size());
  return writePOD(f, len) && writeBytes(f, s.data(), len);
}

static bool writeParamRecord(
    std::FILE *f, const std::string &name, const ParamData &p)
{
  uint8_t isArray = p.isArray ? 1 : 0;
  if (!writeString(f, name))
    return false;
  if (!writePOD(f, isArray))
    return false;

  if (!p.isArray) {
    uint32_t type = static_cast<uint32_t>(p.type);
    uint32_t nbytes = static_cast<uint32_t>(p.bytes.size());
    if (!writePOD(f, type))
      return false;
    if (!writePOD(f, nbytes))
      return false;
    if (!writeBytes(f, p.bytes.data(), nbytes))
      return false;
  } else {
    uint32_t elementType = static_cast<uint32_t>(p.elementType);
    uint64_t elementCount = p.elementCount;
    uint64_t dataBytes = static_cast<uint64_t>(p.bytes.size());
    if (!writePOD(f, elementType))
      return false;
    if (!writePOD(f, elementCount))
      return false;
    if (!writePOD(f, dataBytes))
      return false;
    if (!writeBytes(f, p.bytes.data(), static_cast<size_t>(dataBytes)))
      return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Public API definitions /////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" {

AGXExporter agxNewExporter()
{
  return new (std::nothrow) AGXExporter_t{};
}

void agxReleaseExporter(AGXExporter exporter)
{
  delete exporter;
}

void agxSetObjectSubtype(AGXExporter exporter, const char *subtype)
{
  if (!exporter)
    return;
  exporter->subtype = subtype ? std::string(subtype) : std::string{};
}

void agxSetTimeStepCount(AGXExporter exporter, uint32_t count)
{
  if (!exporter)
    return;
  exporter->timeSteps = count;
  exporter->perTimeStep.resize(count);
}

uint32_t agxGetTimeStepCount(AGXExporter exporter)
{
  if (!exporter)
    return 0;
  return exporter->timeSteps;
}

void agxBeginTimeStep(AGXExporter exporter, uint32_t /*timeStepIndex*/)
{
  (void)exporter; // no-op
}

void agxEndTimeStep(AGXExporter exporter, uint32_t /*timeStepIndex*/)
{
  (void)exporter; // no-op
}

void agxSetParameter(AGXExporter exporter,
    const char *name,
    ANARIDataType type,
    const void *value)
{
  if (!exporter || !name)
    return;
  ParamData p;
  p.isArray = false;
  p.type = type;
  const size_t nbytes = agxSizeOf(type);
  copyBytes(p, value, nbytes);
  exporter->constants[std::string(name)] = std::move(p);
}

void agxSetParameterArray1D(AGXExporter exporter,
    const char *name,
    ANARIDataType elementType,
    const void *data,
    uint64_t elementCount)
{
  if (!exporter || !name)
    return;
  ParamData p;
  p.isArray = true;
  p.elementType = elementType;
  p.elementCount = elementCount;
  const size_t elemBytes = agxSizeOf(elementType);
  const size_t total = elemBytes * elementCount;
  copyBytes(p, data, total);
  exporter->constants[std::string(name)] = std::move(p);
}

void agxSetTimeStepParameter(AGXExporter exporter,
    uint32_t timeStepIndex,
    const char *name,
    ANARIDataType type,
    const void *value)
{
  if (!exporter || !name)
    return;
  timeStepIndex = clampToValidIndex(
      timeStepIndex, exporter->timeSteps ? exporter->timeSteps - 1 : 0);
  if (exporter->perTimeStep.size() != exporter->timeSteps)
    exporter->perTimeStep.resize(exporter->timeSteps);

  ParamData p;
  p.isArray = false;
  p.type = type;
  const size_t nbytes = agxSizeOf(type);
  copyBytes(p, value, nbytes);
  exporter->perTimeStep[timeStepIndex][std::string(name)] = std::move(p);
}

void agxSetTimeStepParameterArray1D(AGXExporter exporter,
    uint32_t timeStepIndex,
    const char *name,
    ANARIDataType elementType,
    const void *data,
    uint64_t elementCount)
{
  if (!exporter || !name)
    return;
  timeStepIndex = clampToValidIndex(
      timeStepIndex, exporter->timeSteps ? exporter->timeSteps - 1 : 0);
  if (exporter->perTimeStep.size() != exporter->timeSteps)
    exporter->perTimeStep.resize(exporter->timeSteps);

  ParamData p;
  p.isArray = true;
  p.elementType = elementType;
  p.elementCount = elementCount;
  const size_t elemBytes = agxSizeOf(elementType);
  const size_t total = elemBytes * elementCount;
  copyBytes(p, data, total);
  exporter->perTimeStep[timeStepIndex][std::string(name)] = std::move(p);
}

int agxWrite(AGXExporter exporter, const char *filename)
{
  if (!exporter || !filename)
    return 1;

  std::FILE *f = std::fopen(filename, "wb");
  if (!f)
    return 2;

  // Count constants
  uint32_t constantCount = static_cast<uint32_t>(exporter->constants.size());

  // Header
  const char magic[4] = {'A', 'G', 'X', 'B'};
  uint32_t version = 1;
  uint32_t endianMarker = 0x01020304;
  uint32_t timeSteps = exporter->timeSteps;
  uint32_t objectType = ANARI_GEOMETRY; // reserved for future configuration

  bool ok = true;
  ok = ok && writeBytes(f, magic, sizeof(magic));
  ok = ok && writePOD(f, version);
  ok = ok && writePOD(f, endianMarker);
  ok = ok && writePOD(f, objectType);
  ok = ok && writePOD(f, timeSteps);
  ok = ok && writePOD(f, constantCount);

  // Subtype
  if (ok) {
    const std::string &subtype = exporter->subtype;
    uint32_t subtypeLen = static_cast<uint32_t>(subtype.size());
    ok = ok && writePOD(f, subtypeLen);
    if (subtypeLen > 0)
      ok = ok && writeBytes(f, subtype.data(), subtypeLen);
  }

  // Constants section
  if (ok) {
    for (const auto &kv : exporter->constants) {
      ok = writeParamRecord(f, kv.first, kv.second);
      if (!ok)
        break;
    }
  }

  // Time steps section
  if (ok) {
    for (uint32_t i = 0; i < exporter->timeSteps; ++i) {
      const auto &m = exporter->perTimeStep[i];
      uint32_t paramCount = static_cast<uint32_t>(m.size());
      ok = ok && writePOD(f, i);
      ok = ok && writePOD(f, paramCount);
      if (!ok)
        break;

      for (const auto &kv : m) {
        ok = writeParamRecord(f, kv.first, kv.second);
        if (!ok)
          break;
      }
      if (!ok)
        break;
    }
  }

  std::fclose(f);
  return ok ? 0 : 3;
}

} // extern "C"
#endif

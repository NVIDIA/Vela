// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// agx_reader.h - Reader API for AGXB (AGX binary) files.

#include <stddef.h>
#include <stdint.h>
// Use ANARI's logical type enums.
#include <anari/frontend/anari_enums.h>
#include <anari/frontend/type_utility.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque reader handle
typedef struct AGXReader_t *AGXReader;

// Header information (copied from file header)
typedef struct AGXHeader
{
  uint32_t version;
  ANARIDataType objectType;
  uint32_t timeSteps;
  uint32_t constantParamCount;

  // Endianness info
  uint32_t endianMarker; // value from file
  uint8_t hostLittleEndian; // 1 if host is little-endian
  uint8_t fileLittleEndian; // 1 if file is little-endian
  uint8_t needByteSwap; // 1 if we need to swap 32/64-bit scalar header fields
  uint8_t reserved;
} AGXHeader;

// A view into a parameter record. The 'data' and 'name' pointers are valid
// until the next Next* call on the same reader (or the reader is destroyed).
typedef struct AGXParamView
{
  const char *name; // not null-terminated guaranteed; see nameLength
  uint32_t nameLength; // length of the name string
  uint8_t isArray; // 0 = single value, 1 = array

  // For single value
  ANARIDataType type; // valid when isArray == 0

  // For arrays
  ANARIDataType elementType; // valid when isArray == 1
  uint64_t elementCount; // valid when isArray == 1

  // Raw bytes for the value or array contents
  const void *data; // pointer to internal buffer
  uint64_t dataBytes; // number of bytes pointed to by 'data'
} AGXParamView;

// Open/close
AGXReader agxNewReader(const char *filename);
void agxReleaseReader(AGXReader r);

// Header
// Returns 0 on success; nonzero on error. 'out' is filled on success.
int agxReaderGetHeader(AGXReader r, AGXHeader *out);

// Get object subtype (as set by writer, or "" if none was set).
const char *agxReaderGetSubtype(AGXReader r);

// Constant parameters iteration
// Resets iteration to the first constant parameter.
void agxReaderResetConstants(AGXReader r);

// Returns 1 and fills 'out' on success (a parameter was read).
// Returns 0 when no more constants are available.
// Returns -1 on error.
int agxReaderNextConstant(AGXReader r, AGXParamView *out);

// Time step iteration
// Positions reader to the next time step. Returns 1 on success,
// 0 if there are no more time steps, -1 on error.
// On success, fills outIndex and outParamCount.
int agxReaderBeginNextTimeStep(
    AGXReader r, uint32_t *outIndex, uint32_t *outParamCount);

// Read next parameter within the current time step.
// Returns 1 and fills 'out' when a parameter is produced.
// Returns 0 when the current time step has no more parameters.
// Returns -1 on error.
int agxReaderNextTimeStepParam(AGXReader r, AGXParamView *out);

// Optional: skip any remaining parameters in the current time step.
void agxReaderSkipRemainingTimeStep(AGXReader r);

// Reset time step iteration to the first time step.
void agxReaderResetTimeSteps(AGXReader r);

#ifdef __cplusplus
} // extern "C"
#endif

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#ifdef AGX_READ_IMPL
#include <climits>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

struct AGXReader_t
{
  std::FILE *f{nullptr};

  // Endianness
  bool hostLittle{true};
  bool fileLittle{true};
  bool needSwap{false};

  // Header info
  AGXHeader hdr{};

  // Optional subtype
  std::string subtype;

  // Offsets
  long constantsStart{-1}; // file position right after header
  long timeStepsStart{-1}; // file position at first time step header

  // Iteration state for constants
  uint32_t constantsRead{0};

  // Iteration state for time steps
  uint32_t stepsRead{0};
  bool inStep{false};
  uint32_t curStepIndex{0};
  uint32_t curStepParamCount{0};
  uint32_t curStepParamsRead{0};

  // Scratch storage for last produced parameter
  std::string lastName;
  std::vector<uint8_t> lastData;

  // Helpers to keep name pointer stable
  AGXParamView view{};
};

// Utilities
static bool hostIsLittleEndian()
{
  uint16_t x = 1;
  return *reinterpret_cast<uint8_t *>(&x) == 1;
}

static uint32_t bswap32(uint32_t v)
{
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8)
      | ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static uint64_t bswap64(uint64_t v)
{
  return ((v & 0x00000000000000FFull) << 56)
      | ((v & 0x000000000000FF00ull) << 40)
      | ((v & 0x0000000000FF0000ull) << 24) | ((v & 0x00000000FF000000ull) << 8)
      | ((v & 0x000000FF00000000ull) >> 8) | ((v & 0x0000FF0000000000ull) >> 24)
      | ((v & 0x00FF000000000000ull) >> 40)
      | ((v & 0xFF00000000000000ull) >> 56);
}

static bool readBytes(std::FILE *f, void *dst, size_t n)
{
  return std::fread(dst, 1, n, f) == n;
}

static bool readU8(std::FILE *f, uint8_t &v)
{
  return readBytes(f, &v, 1);
}

static bool readU32(std::FILE *f, uint32_t &v, bool swap)
{
  if (!readBytes(f, &v, sizeof(v)))
    return false;
  if (swap)
    v = bswap32(v);
  return true;
}

static bool readU64(std::FILE *f, uint64_t &v, bool swap)
{
  if (!readBytes(f, &v, sizeof(v)))
    return false;
  if (swap)
    v = bswap64(v);
  return true;
}

static bool skipBytes(std::FILE *f, uint64_t n)
{
  // Attempt to fseek; if that fails (very large), fall back to buffered skip.
  if (n <= static_cast<uint64_t>(LONG_MAX)) {
    if (std::fseek(f, static_cast<long>(n), SEEK_CUR) == 0)
      return true;
  }
  const size_t bufSize = 64 * 1024;
  std::vector<uint8_t> buf(bufSize);
  uint64_t remaining = n;
  while (remaining > 0) {
    size_t chunk =
        static_cast<size_t>(remaining > bufSize ? bufSize : remaining);
    size_t readCount = std::fread(buf.data(), 1, chunk, f);
    if (readCount != chunk)
      return false;
    remaining -= readCount;
  }
  return true;
}

// Read a parameter record into reader's scratch storage and produce a view
static bool readParamRecord(AGXReader_t *r, AGXParamView *out)
{
  uint32_t nameLen = 0;
  if (!readU32(r->f, nameLen, r->needSwap))
    return false;

  r->lastName.resize(nameLen);
  if (nameLen > 0 && !readBytes(r->f, r->lastName.data(), nameLen))
    return false;

  uint8_t isArray = 0;
  if (!readU8(r->f, isArray))
    return false;

  r->lastData.clear();

  if (isArray == 0) {
    uint32_t typeU32 = 0;
    uint32_t valueBytes = 0;
    if (!readU32(r->f, typeU32, r->needSwap))
      return false;
    if (!readU32(r->f, valueBytes, r->needSwap))
      return false;
    r->lastData.resize(valueBytes);
    if (valueBytes > 0 && !readBytes(r->f, r->lastData.data(), valueBytes))
      return false;

    out->name = r->lastName.c_str();
    out->nameLength = nameLen;
    out->isArray = 0;
    out->type = static_cast<ANARIDataType>(typeU32);
    out->elementType = (ANARIDataType)0;
    out->elementCount = 0;
    out->data = r->lastData.data();
    out->dataBytes = valueBytes;
    return true;
  } else {
    uint32_t elemTypeU32 = 0;
    uint64_t elemCount = 0;
    uint64_t dataBytes = 0;
    if (!readU32(r->f, elemTypeU32, r->needSwap))
      return false;
    if (!readU64(r->f, elemCount, r->needSwap))
      return false;
    if (!readU64(r->f, dataBytes, r->needSwap))
      return false;
    r->lastData.resize(static_cast<size_t>(dataBytes));
    if (dataBytes > 0
        && !readBytes(r->f, r->lastData.data(), static_cast<size_t>(dataBytes)))
      return false;

    out->name = r->lastName.c_str();
    out->nameLength = nameLen;
    out->isArray = 1;
    out->type = (ANARIDataType)0;
    out->elementType = static_cast<ANARIDataType>(elemTypeU32);
    out->elementCount = elemCount;
    out->data = r->lastData.data();
    out->dataBytes = dataBytes;
    return true;
  }
}

// Skip over a parameter record (used to locate the start of time step section)
static bool skipParamRecord(AGXReader_t *r)
{
  uint32_t nameLen = 0;
  if (!readU32(r->f, nameLen, r->needSwap))
    return false;
  if (!skipBytes(r->f, nameLen))
    return false;

  uint8_t isArray = 0;
  if (!readU8(r->f, isArray))
    return false;

  if (isArray == 0) {
    uint32_t typeU32 = 0;
    uint32_t valueBytes = 0;
    if (!readU32(r->f, typeU32, r->needSwap))
      return false;
    if (!readU32(r->f, valueBytes, r->needSwap))
      return false;
    if (!skipBytes(r->f, valueBytes))
      return false;
    return true;
  } else {
    uint32_t elemTypeU32 = 0;
    uint64_t elemCount = 0;
    uint64_t dataBytes = 0;
    if (!readU32(r->f, elemTypeU32, r->needSwap))
      return false;
    if (!readU64(r->f, elemCount, r->needSwap))
      return false;
    if (!readU64(r->f, dataBytes, r->needSwap))
      return false;
    if (!skipBytes(r->f, dataBytes))
      return false;
    return true;
  }
}

extern "C" {

// Open and prime reader: read header and compute section offsets
AGXReader agxNewReader(const char *filename)
{
  if (!filename)
    return nullptr;
  std::FILE *f = std::fopen(filename, "rb");
  if (!f)
    return nullptr;

  AGXReader_t *r = new (std::nothrow) AGXReader_t{};
  if (!r) {
    std::fclose(f);
    return nullptr;
  }
  r->f = f;
  r->hostLittle = hostIsLittleEndian();

  // Read header
  char magic[4];
  if (!readBytes(r->f, magic, sizeof(magic))
      || std::memcmp(magic, "AGXB", 4) != 0) {
    agxReleaseReader(r);
    return nullptr;
  }

  uint32_t version = 0;
  uint32_t endianMarker = 0;
  uint32_t objectType = ANARI_UNKNOWN; // default in case of early error
  uint32_t timeSteps = 0;
  uint32_t constCount = 0;

  if (!readU32(r->f, version, false) || !readU32(r->f, endianMarker, false)
      || !readU32(r->f, objectType, false) || !readU32(r->f, timeSteps, false)
      || !readU32(r->f, constCount, false)) {
    agxReleaseReader(r);
    return nullptr;
  }

  bool fileLittle = true;
  bool needSwap = false;
  if (endianMarker == 0x01020304u) {
    fileLittle = r->hostLittle; // same as host
    needSwap = false;
  } else if (bswap32(endianMarker) == 0x01020304u) {
    fileLittle = !r->hostLittle;
    needSwap = true;
  } else {
    // Invalid endian marker
    agxReleaseReader(r);
    return nullptr;
  }

  r->fileLittle = fileLittle;
  r->needSwap = needSwap;

  // If needSwap is true, swap the header integers we just read (except
  // endianMarker which we keep as file value)
  if (r->needSwap) {
    version = bswap32(version);
    timeSteps = bswap32(timeSteps);
    constCount = bswap32(constCount);
  }

  r->hdr.version = version;
  r->hdr.objectType = static_cast<ANARIDataType>(objectType);
  r->hdr.timeSteps = timeSteps;
  r->hdr.constantParamCount = constCount;
  r->hdr.endianMarker = endianMarker;
  r->hdr.hostLittleEndian = r->hostLittle ? 1 : 0;
  r->hdr.fileLittleEndian = r->fileLittle ? 1 : 0;
  r->hdr.needByteSwap = r->needSwap ? 1 : 0;
  r->hdr.reserved = 0;

  // Read optional subtype string
  uint32_t subtypeLen = 0;
  if (!readU32(r->f, subtypeLen, r->needSwap)) {
    agxReleaseReader(r);
    return nullptr;
  }
  if (subtypeLen > 0) {
    r->subtype.resize(subtypeLen);
    if (!readBytes(r->f, r->subtype.data(), subtypeLen)) {
      agxReleaseReader(r);
      return nullptr;
    }
  } else {
    r->subtype.clear();
  }

  // Mark constantsStart
  r->constantsStart = std::ftell(r->f);

  // Compute timeStepsStart by skipping constants
  for (uint32_t i = 0; i < constCount; ++i) {
    if (!skipParamRecord(r)) {
      agxReleaseReader(r);
      return nullptr;
    }
  }
  r->timeStepsStart = std::ftell(r->f);

  // Initialize iteration positions
  agxReaderResetConstants(r);
  agxReaderResetTimeSteps(r);

  return r;
}

void agxReleaseReader(AGXReader r_)
{
  if (!r_)
    return;
  if (r_->f)
    std::fclose(r_->f);
  delete r_;
}

int agxReaderGetHeader(AGXReader r_, AGXHeader *out)
{
  if (!r_ || !out)
    return 1;
  *out = r_->hdr;
  return 0;
}

const char *agxReaderGetSubtype(AGXReader r)
{
  return r->subtype.c_str();
}

// Constants iteration
void agxReaderResetConstants(AGXReader r_)
{
  if (!r_ || !r_->f)
    return;
  std::fseek(r_->f, r_->constantsStart, SEEK_SET);
  r_->constantsRead = 0;
  r_->lastName.clear();
  r_->lastData.clear();
}

int agxReaderNextConstant(AGXReader r_, AGXParamView *out)
{
  if (!r_ || !r_->f || !out)
    return -1;
  if (r_->constantsRead >= r_->hdr.constantParamCount)
    return 0;

  AGXParamView v{};
  if (!readParamRecord(r_, &v))
    return -1;

  r_->constantsRead++;
  *out = v;
  return 1;
}

// Time steps iteration
void agxReaderResetTimeSteps(AGXReader r_)
{
  if (!r_ || !r_->f)
    return;
  std::fseek(r_->f, r_->timeStepsStart, SEEK_SET);
  r_->stepsRead = 0;
  r_->inStep = false;
  r_->curStepIndex = 0;
  r_->curStepParamCount = 0;
  r_->curStepParamsRead = 0;
  r_->lastName.clear();
  r_->lastData.clear();
}

int agxReaderBeginNextTimeStep(
    AGXReader r_, uint32_t *outIndex, uint32_t *outParamCount)
{
  if (!r_ || !r_->f || !outIndex || !outParamCount)
    return -1;
  if (r_->stepsRead >= r_->hdr.timeSteps)
    return 0;

  uint32_t index = 0;
  uint32_t paramCount = 0;
  if (!readU32(r_->f, index, r_->needSwap))
    return -1;
  if (!readU32(r_->f, paramCount, r_->needSwap))
    return -1;

  r_->inStep = true;
  r_->curStepIndex = index;
  r_->curStepParamCount = paramCount;
  r_->curStepParamsRead = 0;
  r_->stepsRead++;

  *outIndex = index;
  *outParamCount = paramCount;
  return 1;
}

int agxReaderNextTimeStepParam(AGXReader r_, AGXParamView *out)
{
  if (!r_ || !r_->f || !out)
    return -1;
  if (!r_->inStep)
    return 0;
  if (r_->curStepParamsRead >= r_->curStepParamCount) {
    r_->inStep = false;
    return 0;
  }

  AGXParamView v{};
  if (!readParamRecord(r_, &v))
    return -1;

  r_->curStepParamsRead++;
  if (r_->curStepParamsRead >= r_->curStepParamCount) {
    // End of this step reached; the caller may call BeginNextTimeStep for the
    // next one.
    r_->inStep = false;
  }

  *out = v;
  return 1;
}

void agxReaderSkipRemainingTimeStep(AGXReader r_)
{
  if (!r_ || !r_->f || !r_->inStep)
    return;
  while (r_->curStepParamsRead < r_->curStepParamCount) {
    if (!skipParamRecord(r_))
      break;
    r_->curStepParamsRead++;
  }
  r_->inStep = false;
}

} // extern "C"
#endif

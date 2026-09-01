// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataStream.hpp"
// std
#include <cstdint>
#include <stdexcept>

namespace vsr::core {

// BufferWriter definitions ///////////////////////////////////////////////////

BufferWriter::BufferWriter(size_t initial_size)
{
  m_buffer.reserve(initial_size);
}

size_t BufferWriter::write(const void *ptr, size_t size, size_t count)
{
  if (ptr == nullptr || size == 0 || count == 0)
    return 0;

  const size_t total_bytes = size * count;
  const auto *data = static_cast<const std::byte *>(ptr);

  m_buffer.resize(m_buffer.size() + total_bytes);
  std::memcpy(
      m_buffer.data() + m_buffer.size() - total_bytes, data, total_bytes);

  return count;
}

const std::vector<std::byte> &BufferWriter::buffer() const
{
  return m_buffer;
}

std::vector<std::byte> BufferWriter::take()
{
  return std::move(m_buffer);
}

void BufferWriter::clear()
{
  m_buffer.clear();
}

// FileWriter definitions /////////////////////////////////////////////////////

FileWriter::FileWriter(const char *filename, const char *mode)
{
  m_file = std::fopen(filename, mode);
}

FileWriter::~FileWriter()
{
  if (m_file != nullptr)
    std::fclose(m_file);
}

size_t FileWriter::write(const void *ptr, size_t size, size_t count)
{
  if (ptr == nullptr || size == 0 || count == 0 || m_file == nullptr)
    return 0;

  return std::fwrite(ptr, size, count, m_file);
}

bool FileWriter::valid() const
{
  return m_file != nullptr;
}

FileWriter::operator bool() const
{
  return valid();
}

// BufferReader definitions ///////////////////////////////////////////////////

size_t DataReader::bytesRemaining() const
{
  return SIZE_MAX;
}

BufferReader::BufferReader(const std::vector<std::byte> &buffer, size_t offset)
    : m_buffer(buffer), m_offset(offset)
{}

size_t BufferReader::bytesRemaining() const
{
  return m_offset < m_buffer.size() ? m_buffer.size() - m_offset : 0;
}

size_t BufferReader::read(void *ptr, size_t size, size_t count)
{
  if (ptr == nullptr || size == 0 || count == 0
      || m_offset >= m_buffer.size()) {
    return 0;
  }

  size_t total_bytes = size * count;
  size_t remaining_bytes = m_buffer.size() - m_offset;

  if (total_bytes > remaining_bytes)
    total_bytes = remaining_bytes;

  std::memcpy(ptr, m_buffer.data() + m_offset, total_bytes);
  m_offset += total_bytes;

  return total_bytes / size; // Return number of elements read
}

size_t BufferReader::position() const
{
  return m_offset;
}

void BufferReader::reset(size_t offset)
{
  m_offset = offset;
}

// FileReader definitions /////////////////////////////////////////////////////

FileReader::FileReader(const char *filename, const char *mode)
{
  m_file = std::fopen(filename, mode);
  if (m_file != nullptr && std::fseek(m_file, 0, SEEK_END) == 0) {
    const long size = std::ftell(m_file);
    if (size > 0)
      m_fileSize = size_t(size);
    std::fseek(m_file, 0, SEEK_SET);
  }
}

FileReader::~FileReader()
{
  if (m_file != nullptr)
    std::fclose(m_file);
}

size_t FileReader::read(void *ptr, size_t size, size_t count)
{
  if (ptr == nullptr || size == 0 || count == 0 || m_file == nullptr) {
    return 0;
  }

  const size_t numRead = std::fread(ptr, size, count, m_file);
  m_position += numRead * size;
  return numRead;
}

size_t FileReader::bytesRemaining() const
{
  if (m_file == nullptr)
    return 0;
  // A file that could not be sized (a pipe, a device) reports unknown.
  if (m_fileSize == 0)
    return SIZE_MAX;
  return m_position < m_fileSize ? m_fileSize - m_position : 0;
}

bool FileReader::valid() const
{
  return m_file != nullptr;
}

FileReader::operator bool() const
{
  return valid();
}

} // namespace vsr::core

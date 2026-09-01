// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace vsr::core {

///////////////////////////////////////////////////////////////////////////////
// Data writers ///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/*
 * Abstract base for write targets; implementations forward data to files,
 * buffers, or any other destination via a single virtual write() method.
 *
 * Example:
 *   void serialize(DataWriter &w) {
 *     w.write(&value, sizeof(value), 1);
 *   }
 */
struct DataWriter
{
  virtual ~DataWriter() = default;

  // Write data to the writer (like std::fwrite)
  virtual size_t write(const void *ptr, size_t size, size_t count) = 0;
};

/*
 * DataWriter that accumulates written bytes into a growable in-memory buffer;
 * the buffer contents can be retrieved or moved out when writing is complete.
 *
 * Example:
 *   BufferWriter bw;
 *   bw.write(data, sizeof(int), count);
 *   auto bytes = bw.take(); // move the buffer out
 */
struct BufferWriter : public DataWriter
{
  explicit BufferWriter(size_t initial_size = 0);

  size_t write(const void *ptr, size_t size, size_t count) override;

  const std::vector<std::byte> &buffer() const;
  std::vector<std::byte> take();
  void clear();

 private:
  std::vector<std::byte> m_buffer;
};

/*
 * DataWriter that writes bytes directly to a file on disk, opened at
 * construction and closed on destruction.
 *
 * Example:
 *   FileWriter fw("output.bin");
 *   if (fw) fw.write(data, sizeof(float), n);
 */
struct FileWriter : public DataWriter
{
  explicit FileWriter(const char *filename, const char *mode = "wb");
  ~FileWriter();

  size_t write(const void *ptr, size_t size, size_t count) override;

  bool valid() const;
  operator bool() const;

 private:
  std::FILE *m_file{nullptr};
};

///////////////////////////////////////////////////////////////////////////////
// Data readers ///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/*
 * Abstract base for read sources; implementations read from files, buffers,
 * or any other source through a single virtual read() method.
 *
 * Example:
 *   void deserialize(DataReader &r) {
 *     r.read(&value, sizeof(value), 1);
 *   }
 */
struct DataReader
{
  virtual ~DataReader() = default;

  // Read data from the reader (like std::fread)
  virtual size_t read(void *ptr, size_t size, size_t count) = 0;

  // Upper bound on the bytes still readable, so a caller can reject a
  // corrupt length before allocating for it. SIZE_MAX when unknown.
  virtual size_t bytesRemaining() const;
};

/*
 * DataReader that reads bytes sequentially from a caller-owned in-memory
 * buffer, tracking the current position and supporting reset.
 *
 * Example:
 *   BufferReader br(myBuffer);
 *   br.read(&value, sizeof(int), 1);
 *   br.reset(); // seek back to start
 */
struct BufferReader : public DataReader
{
  explicit BufferReader(
      const std::vector<std::byte> &buffer, size_t offset = 0);

  size_t read(void *ptr, size_t size, size_t count) override;
  size_t bytesRemaining() const override;

  size_t position() const;
  void reset(size_t offset = 0);

 private:
  const std::vector<std::byte> &m_buffer;
  size_t m_offset{0};
};

/*
 * DataReader that reads bytes sequentially from a file on disk, opened at
 * construction and closed on destruction.
 *
 * Example:
 *   FileReader fr("input.bin");
 *   if (fr) fr.read(&value, sizeof(float), n);
 */
struct FileReader : public DataReader
{
  explicit FileReader(const char *filename, const char *mode = "rb");
  ~FileReader();

  size_t read(void *ptr, size_t size, size_t count) override;
  size_t bytesRemaining() const override;

  bool valid() const;
  operator bool() const;

 private:
  std::FILE *m_file{nullptr};
  size_t m_fileSize{0};
  size_t m_position{0};
};

} // namespace vsr::core

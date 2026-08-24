// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// mpi
#include <mpi.h>
// std
#include <type_traits>

namespace vsr::mpi {

/*
 * MPI-replicated POD value that the main rank (rank 0) can write and all ranks
 * can read; calling sync() broadcasts any pending update to worker ranks via
 * MPI_Bcast, returning true if the value changed.
 *
 * Example:
 *   ReplicatedObject<Config> cfg;
 *   if (rank == 0) *cfg.write() = newConfig;
 *   bool changed = cfg.sync();
 *   const Config *c = cfg.read();
 */
template <typename T>
struct ReplicatedObject
{
  static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>,
      "ReplicatedObject<T> requires that 'T' be a POD type.");

  ReplicatedObject();
  ~ReplicatedObject();

  bool sync(); // sync main rank data to workers, return true if update occured
  const T *read() const; // anyone can read
  T *write(); // main rank only, causes an update to occur on next sync

 private:
  void markUpdated();
  bool needsUpdate() const;
  bool isMain() const;
  T m_data;
  int m_rank{-1};
  int m_mainVersion{0};
  int m_workerVersion{-1};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline ReplicatedObject<T>::ReplicatedObject()
{
  MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);
}

template <typename T>
inline ReplicatedObject<T>::~ReplicatedObject() = default;

template <typename T>
inline bool ReplicatedObject<T>::sync()
{
  MPI_Bcast(&m_mainVersion, 1, MPI_INT, /*root=*/0, MPI_COMM_WORLD);
  bool updated = needsUpdate();
  if (updated) {
    MPI_Bcast(&m_data, sizeof(T), MPI_INT8_T, /*root=*/0, MPI_COMM_WORLD);
    m_workerVersion = m_mainVersion;
  }
  return updated;
}

template <typename T>
inline const T *ReplicatedObject<T>::read() const
{
  return &m_data;
}

template <typename T>
inline T *ReplicatedObject<T>::write()
{
  markUpdated();
  return isMain() ? &m_data : nullptr;
}

template <typename T>
inline void ReplicatedObject<T>::markUpdated()
{
  if (isMain() && !needsUpdate())
    m_mainVersion++;
}

template <typename T>
inline bool ReplicatedObject<T>::needsUpdate() const
{
  return m_workerVersion < m_mainVersion;
}

template <typename T>
inline bool ReplicatedObject<T>::isMain() const
{
  return m_rank == 0;
}

} // namespace vsr::mpi

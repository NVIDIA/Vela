// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RenderRuntime.h"
// MPI
#ifdef VSR_USE_MPI
#include <mpi.h>
#endif
// std
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace vsr::render_cli {

#ifdef VSR_USE_MPI
namespace {

void checkMPI(int status)
{
  if (status == MPI_SUCCESS)
    return;
  std::fprintf(stderr, "vsrRender: fatal MPI error (%d)\n", status);
  MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  std::abort();
}

} // namespace
#endif

RenderRuntime::RenderRuntime(int &argc, char **&argv)
{
#ifdef VSR_USE_MPI
  int provided = 0;
  const int status =
      MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
  if (status != MPI_SUCCESS) {
    std::fprintf(stderr, "vsrRender: cannot initialize MPI (%d)\n", status);
    std::abort();
  }
  checkMPI(MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN));
  if (provided < MPI_THREAD_FUNNELED)
    checkMPI(MPI_ERR_OTHER);
  checkMPI(MPI_Comm_rank(MPI_COMM_WORLD, &m_rank));
  checkMPI(MPI_Comm_size(MPI_COMM_WORLD, &m_size));
#else
  (void)argc;
  (void)argv;
#endif
}

RenderRuntime::~RenderRuntime()
{
#ifdef VSR_USE_MPI
  // No exceptions escape cleanup; failed MPI operations are not recoverable.
  checkMPI(MPI_Finalize());
#endif
}

int RenderRuntime::rank() const
{
  return m_rank;
}

int RenderRuntime::size() const
{
  return m_size;
}

void RenderRuntime::runPhase(
    const char *name, const std::function<void()> &work) const
{
  // Bounded diagnostics avoid allocation between the status and message
  // collectives. The lowest failing rank supplies a deterministic diagnostic.
  char diagnostic[4096] = {};
  int failedRank = std::numeric_limits<int>::max();
  try {
    work();
  } catch (const std::bad_alloc &) {
    abort("Resource exhaustion");
  } catch (const std::exception &error) {
    const int length =
        std::snprintf(diagnostic, sizeof(diagnostic), "%s", error.what());
    if (length >= int(sizeof(diagnostic))) {
      constexpr char TRUNCATION_MARKER[] = " [truncated]";
      std::memcpy(diagnostic + sizeof(diagnostic) - sizeof(TRUNCATION_MARKER),
          TRUNCATION_MARKER,
          sizeof(TRUNCATION_MARKER));
    }
    failedRank = m_rank;
  } catch (...) {
    std::snprintf(
        diagnostic, sizeof(diagnostic), "Unknown application exception");
    failedRank = m_rank;
  }
#ifdef VSR_USE_MPI
  int globalFailedRank = std::numeric_limits<int>::max();
  checkMPI(MPI_Allreduce(
      &failedRank, &globalFailedRank, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD));
  failedRank = globalFailedRank;
  if (failedRank != std::numeric_limits<int>::max())
    checkMPI(MPI_Bcast(
        diagnostic, sizeof(diagnostic), MPI_CHAR, failedRank, MPI_COMM_WORLD));
#endif
  if (failedRank != std::numeric_limits<int>::max()) {
    if (m_size == 1)
      throw std::runtime_error(diagnostic);
    throw std::runtime_error(std::string(name) + " failed on rank "
        + std::to_string(failedRank) + ": " + diagnostic);
  }
}

void RenderRuntime::abort(const char *diagnostic) const
{
  std::fprintf(stderr, "vsrRender: fatal rank %d: %s\n", m_rank, diagnostic);
#ifdef VSR_USE_MPI
  MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  std::abort();
#else
  std::exit(EXIT_FAILURE);
#endif
}

} // namespace vsr::render_cli

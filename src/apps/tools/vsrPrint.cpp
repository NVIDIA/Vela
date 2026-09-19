// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include <vsr/core/DataTree.hpp>
#include <vsr/core/Logging.hpp>

// Print a Data Tree file, in whichever encoding it is in, as the Text
// Encoding. Conversion between encodings is vsrConvert's job.

int main(int argc, const char *argv[])
{
  if (argc != 2) {
    printf("usage: %s <input.vsr>\n", argv[0]);
    printf("  Prints the input as the Data Tree Text Encoding.\n");
    return 1;
  }

  vsr::core::setLogToStderr();

  vsr::core::DataTree tree;
  if (!tree.load(argv[1])) {
    fprintf(stderr, "failed to load '%s'\n", argv[1]);
    return 1;
  }

  tree.print();
  return 0;
}

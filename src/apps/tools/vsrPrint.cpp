// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include <vsr/core/DataTree.hpp>

int main(int argc, const char *argv[])
{
  if (argc < 2) {
    printf("usage: ./%s <file.vsr>\n", argv[0]);
    return 1;
  }

  vsr::core::DataTree tree;
  tree.load(argv[1]);
  tree.print();

  return 0;
}

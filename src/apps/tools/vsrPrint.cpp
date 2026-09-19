// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include <vsr/core/DataTree.hpp>
#include <vsr/core/DataTreeText.hpp>
#include <vsr/core/Logging.hpp>

int main(int argc, const char *argv[])
{
  if (argc < 2 || argc > 3) {
    printf("usage: %s <input.vsr> [output.vsr]\n", argv[0]);
    printf("  With one argument, prints the input as the Text Encoding.\n");
    printf("  With two, converts the input to the other encoding: a binary\n");
    printf("  input is written as text, a text input is written as binary.\n");
    return 1;
  }

  vsr::core::setLogToStderr();

  const char *input = argv[1];
  const bool inputIsText = vsr::core::isDataTreeTextFile(input);

  vsr::core::DataTree tree;
  if (!tree.load(input)) {
    fprintf(stderr, "failed to load '%s'\n", input);
    return 1;
  }

  if (argc == 2) {
    tree.print();
    return 0;
  }

  const char *output = argv[2];
  const auto encoding =
      inputIsText ? vsr::core::Encoding::Binary : vsr::core::Encoding::Text;
  if (!tree.save(output, encoding)) {
    fprintf(stderr, "failed to save '%s'\n", output);
    return 1;
  }

  return 0;
}

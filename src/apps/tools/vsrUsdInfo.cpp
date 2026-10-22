// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include <vsr/core/Logging.hpp>
// vsr_io
#include <vsr/io/UsdImport.hpp>
// std
#include <cstdio>
#include <filesystem>

// Print the Variant Sets a USD Stage offers to Variant Selections, with the
// variants each one authors -- the names `-usd_variant <set>=<variant>`
// accepts. The Stage opens with no payload loaded, so this is cheap even for
// a large Stage, but a Variant Set only a payload authors is not listed.

int main(int argc, const char *argv[])
{
  if (argc != 2) {
    printf("usage: %s <stage.usd>\n", argv[0]);
    printf("  Prints the Variant Sets the Stage offers and their variants.\n");
    return 1;
  }

  vsr::core::setLogToStderr();

  if (!std::filesystem::exists(argv[1])) {
    fprintf(stderr, "no such file '%s'\n", argv[1]);
    return 1;
  }

  const auto catalog = vsr::io::usdVariantSets(argv[1]);
  if (catalog.empty()) {
    printf("no Variant Sets\n");
    return 0;
  }

  for (const auto &[set, variants] : catalog) {
    printf("%s:", set.c_str());
    for (const auto &variant : variants)
      printf(" %s", variant.c_str());
    printf("\n");
  }
  return 0;
}

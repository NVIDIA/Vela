// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include <vsr/core/DataTree.hpp>
#include <vsr/core/DataTreeText.hpp>
#include <vsr/core/Logging.hpp>
// std
#include <cstdio>
#include <cstring>

// Convert a Data Tree file between its Binary and Text Encodings. The input's
// encoding is detected from its contents; the output defaults to the other
// one, so a plain two-argument call always flips. Neither side consults the
// file extension (ADR 0039), so any naming convention works.

namespace {

void usage(const char *program)
{
  printf("usage: %s [--text|--binary] <input.vsr> <output.vsr>\n", program);
  printf(
      "  Converts a Data Tree file between the Binary and Text Encodings.\n");
  printf("  The input's encoding is detected from its contents. By default\n");
  printf("  the output is written in the other encoding; --text or --binary\n");
  printf("  forces one, which also lets a file be re-saved in canonical\n");
  printf("  form in its own encoding.\n");
}

} // namespace

int main(int argc, const char *argv[])
{
  const char *input = nullptr;
  const char *output = nullptr;
  bool forceText = false;
  bool forceBinary = false;

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--text") == 0)
      forceText = true;
    else if (std::strcmp(arg, "--binary") == 0)
      forceBinary = true;
    else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else if (arg[0] == '-' && arg[1] != '\0') {
      fprintf(stderr, "unknown option '%s'\n", arg);
      usage(argv[0]);
      return 1;
    } else if (!input)
      input = arg;
    else if (!output)
      output = arg;
    else {
      fprintf(stderr, "unexpected argument '%s'\n", arg);
      usage(argv[0]);
      return 1;
    }
  }

  if (!input || !output || (forceText && forceBinary)) {
    usage(argv[0]);
    return 1;
  }

  vsr::core::setLogToStderr();

  const bool inputIsText = vsr::core::isDataTreeTextFile(input);

  vsr::core::DataTree tree;
  if (!tree.load(input)) {
    fprintf(stderr, "failed to load '%s'\n", input);
    return 1;
  }

  vsr::core::Encoding encoding =
      inputIsText ? vsr::core::Encoding::Binary : vsr::core::Encoding::Text;
  if (forceText)
    encoding = vsr::core::Encoding::Text;
  else if (forceBinary)
    encoding = vsr::core::Encoding::Binary;

  if (!tree.save(output, encoding)) {
    fprintf(stderr, "failed to save '%s'\n", output);
    return 1;
  }

  printf("%s (%s) -> %s (%s)\n",
      input,
      inputIsText ? "text" : "binary",
      output,
      encoding == vsr::core::Encoding::Text ? "text" : "binary");
  return 0;
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Application.h"
// vsr_core
#include "vsr/core/Logging.hpp"

int main(int argc, const char *argv[])
{
  {
    vsr::core::setLogToStdout();
    vsr::scivis_studio::client::Application app(argc, argv);
    app.run(1920, 1080, "SciVis Studio Client");
  }

  return 0;
}

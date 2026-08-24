// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RenderServer.hpp"

int main(int argc, const char *argv[])
{
  vsr::network::RenderServer server(argc, argv);
  server.run(12345);
  return 0;
}

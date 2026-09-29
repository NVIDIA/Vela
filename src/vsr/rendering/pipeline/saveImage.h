// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>

namespace vsr::rendering {

struct ImagePipeline;

/*
 * Writes the finished color buffer of 'pipeline' to 'filename' as an RGBA8
 * PNG, top row first. Call after render(); returns false (and logs) if
 * nothing has been rendered yet or the file cannot be written.
 *
 * Example:
 *   for (int s = 0; s < samples; s++)
 *     pipeline.render();
 *   saveImage(pipeline, "frame_0001.png");
 */
bool saveImage(const ImagePipeline &pipeline, const std::string &filename);

} // namespace vsr::rendering

// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Object.hpp"

namespace vsr::scene {

/*
 * ANARI Sampler object that provides texture or data lookup for material
 * parameters; subtype selects the sampling source (1D/2D/3D image, primitive, etc.).
 *
 * Example:
 *   auto samp = scene.createObject<Sampler>(tokens::sampler::image2D);
 *   samp->setParameter("image", textureArray);
 */
struct Sampler : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Sampler);

  Sampler(Token subtype = tokens::unknown);
  virtual ~Sampler() = default;

  ObjectPoolRef<Sampler> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;
};

using SamplerRef = ObjectPoolRef<Sampler>;

namespace tokens::sampler {

extern const Token compressedImage2D;
extern const Token image1D;
extern const Token image2D;
extern const Token image3D;
extern const Token primitive;
extern const Token transform;

} // namespace tokens::sampler

} // namespace vsr::scene

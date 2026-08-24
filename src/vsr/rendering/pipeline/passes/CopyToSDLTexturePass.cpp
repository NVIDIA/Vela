// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#if ENABLE_SDL

#include "CopyToSDLTexturePass.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// cuda + SDL
#ifdef ENABLE_CUDA
#include <SDL3/SDL_opengl.h>
#include <cuda_gl_interop.h>
#include <cuda_runtime_api.h>
#endif
// std
#include <optional>

namespace vsr::rendering {

struct CopyToSDLTexturePass::CopyToSDLTexturePassImpl
{
  SDL_Renderer *renderer{nullptr};
  SDL_Texture *texture{nullptr};
  std::optional<bool> glInteropAvailable;
#ifdef ENABLE_CUDA
  cudaGraphicsResource_t graphicsResource{nullptr};
#endif
};

CopyToSDLTexturePass::CopyToSDLTexturePass(SDL_Renderer *renderer)
{
  m_impl = new CopyToSDLTexturePassImpl;
  m_impl->renderer = renderer;
}

CopyToSDLTexturePass::~CopyToSDLTexturePass()
{
#ifdef ENABLE_CUDA
  if (m_impl->graphicsResource)
    cudaGraphicsUnregisterResource(m_impl->graphicsResource);
#endif
  SDL_DestroyTexture(m_impl->texture);
  delete m_impl;
  m_impl = nullptr;
}

const char *CopyToSDLTexturePass::name() const
{
  return "Copy-To-SDL";
}

SDL_Texture *CopyToSDLTexturePass::getTexture() const
{
  return m_impl->texture;
}

void CopyToSDLTexturePass::checkGLInterop() const
{
#ifdef ENABLE_CUDA
  unsigned int numDevices = 0;
  int cudaDevices[8]; // Assuming max 8 devices for simplicity

  cudaError_t err =
      cudaGLGetDevices(&numDevices, cudaDevices, 8, cudaGLDeviceListAll);
  if (err != cudaSuccess) {
    vsr::core::logWarning(
        "[ImagePipeline] failed to get CUDA GL devices -- reason: %s",
        cudaGetErrorString(err));
    cudaGetLastError(); // Clear the error so it is not captured by subsequent
                        // calls.
    m_impl->glInteropAvailable = false;
    return;
  }

  if (numDevices > 0) {
    int currentDevice = 0;
    cudaGetDevice(&currentDevice);
    for (unsigned int i = 0; i < numDevices; ++i) {
      if (currentDevice == cudaDevices[i]) {
        vsr::core::logStatus("[ImagePipeline] using CUDA-GL interop via SDL3");
        m_impl->glInteropAvailable = true;
        return;
      }
    }
  }
#endif

  vsr::core::logWarning(
      "[ImagePipeline] unable to use CUDA-GL interop via SDL3");
  m_impl->glInteropAvailable = false;
}

void CopyToSDLTexturePass::render(ImageBuffers &b, int /*stageId*/)
{
  if (!m_impl->glInteropAvailable) {
    checkGLInterop();
    if (m_impl->glInteropAvailable && *m_impl->glInteropAvailable)
      updateSize();
  }

  const auto size = getDimensions();

#ifdef ENABLE_CUDA
  if (m_impl->graphicsResource) {
    cudaGraphicsMapResources(1, &m_impl->graphicsResource);
    cudaArray_t array;
    cudaGraphicsSubResourceGetMappedArray(
        &array, m_impl->graphicsResource, 0, 0);
    cudaMemcpy2DToArray(array,
        0,
        0,
        b.color,
        size.x * sizeof(b.color[0]),
        size.x * sizeof(b.color[0]),
        size.y,
        cudaMemcpyDeviceToDevice);
    cudaGraphicsUnmapResources(1, &m_impl->graphicsResource);
  } else {
#endif
    SDL_UpdateTexture(m_impl->texture,
        nullptr,
        b.color,
        getDimensions().x * sizeof(b.color[0]));
#ifdef ENABLE_CUDA
  }
#endif
}

void CopyToSDLTexturePass::updateSize()
{
#ifdef ENABLE_CUDA
  if (m_impl->graphicsResource) {
    cudaGraphicsUnregisterResource(m_impl->graphicsResource);
    m_impl->graphicsResource = nullptr;
  }
#endif

  if (m_impl->texture)
    SDL_DestroyTexture(m_impl->texture);
  auto newSize = getDimensions();
  m_impl->texture = SDL_CreateTexture(m_impl->renderer,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_STREAMING,
      newSize.x,
      newSize.y);

#ifdef ENABLE_CUDA
  if (m_impl->glInteropAvailable && *m_impl->glInteropAvailable) {
    SDL_PropertiesID propID = SDL_GetTextureProperties(m_impl->texture);
    Sint64 texID = SDL_GetNumberProperty(
        propID, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, -1);

    if (texID > 0) {
      cudaGraphicsGLRegisterImage(&m_impl->graphicsResource,
          static_cast<GLuint>(texID),
          GL_TEXTURE_2D,
          cudaGraphicsRegisterFlagsWriteDiscard);
    } else {
      vsr::core::logWarning(
          "[ImagePipeline] could not get SDL texture number!");
    }
  }
#endif
}

} // namespace vsr::rendering

#endif

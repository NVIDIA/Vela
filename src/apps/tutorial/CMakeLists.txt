## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

macro(define_tutorial TUTORIAL_NAME)
  project(${TUTORIAL_NAME})
  project_add_executable(${TUTORIAL_NAME}.cpp)
  project_link_libraries(
    vsr_core
    vsr_scene
    vsr_io
    vsr_rendering
    vela_ext_stb_image
    vela_ext_flip
  )
endmacro()

define_tutorial(vsrTutorialDataTreeDemo)
define_tutorial(vsrTutorialFlipDiff)
define_tutorial(vsrTutorialForestDemo)
define_tutorial(vsrTutorialMultiRender)
define_tutorial(vsrTutorialPipeline)

find_package(OpenMP)
if (TARGET OpenMP::OpenMP_CXX)
  target_link_libraries(vsrTutorialFlipDiff OpenMP::OpenMP_CXX)
endif()

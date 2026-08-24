# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if (TARGET vela::silo)
  return()
endif()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
  pkg_check_modules(SILO QUIET silo siloh5)
endif()

if (NOT SILO_FOUND)
  # Try to find Silo manually
  find_path(SILO_INCLUDE_DIR silo.h)
  find_library(SILO_LIBRARY NAMES siloh5 silo)
  if (SILO_INCLUDE_DIR AND SILO_LIBRARY)
    set(SILO_FOUND TRUE)
    set(SILO_INCLUDE_DIRS ${SILO_INCLUDE_DIR})
    set(SILO_LIBRARIES ${SILO_LIBRARY})
  endif()
endif()

if (SILO_FOUND)
  add_library(vela::silo INTERFACE IMPORTED)
  target_include_directories(vela::silo INTERFACE ${SILO_INCLUDE_DIRS})
  target_link_libraries(vela::silo INTERFACE ${SILO_LIBRARIES})
  message(STATUS "Found Silo: ${SILO_LIBRARIES}")
else()
  if(vela_silo_FIND_REQUIRED)
    message(FATAL_ERROR "Silo not found")
  else()
    message(WARNING "Silo requested but not found")
  endif()
endif()

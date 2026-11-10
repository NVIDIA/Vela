## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_lua LANGUAGES C)

find_package(Lua)
if(LUA_FOUND)
  if(LUA_VERSION_STRING VERSION_LESS "5.5")
    project_add_library(INTERFACE)
    set_target_properties(${PROJECT_NAME}
      PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${LUA_LIBRARIES}"
    )
    return()
  else()
    message(WARNING "Found Lua >= 5.5, which is not supported yet. Falling back to embedding Lua 5.4")
  endif()
endif()

# Fetched from the lua/lua GitHub mirror (tag v5.4.8) rather than lua.org/ftp,
# whose server frequently times out on CI runners. Same sources as the release
# tarball, but laid out at the archive root instead of under src/.
anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/lua/lua/archive/refs/tags/v5.4.8.tar.gz
  MD5 af2c3cf59f247cab6c2f23c383e2f44c
)
set(VELA_LUA_SRC ${vela_ext_lua_LOCATION})

project_add_library(STATIC
  # Lua source files (core library, no standalone interpreter)
  ${VELA_LUA_SRC}/lapi.c
  ${VELA_LUA_SRC}/lcode.c
  ${VELA_LUA_SRC}/lctype.c
  ${VELA_LUA_SRC}/ldebug.c
  ${VELA_LUA_SRC}/ldo.c
  ${VELA_LUA_SRC}/ldump.c
  ${VELA_LUA_SRC}/lfunc.c
  ${VELA_LUA_SRC}/lgc.c
  ${VELA_LUA_SRC}/llex.c
  ${VELA_LUA_SRC}/lmem.c
  ${VELA_LUA_SRC}/lobject.c
  ${VELA_LUA_SRC}/lopcodes.c
  ${VELA_LUA_SRC}/lparser.c
  ${VELA_LUA_SRC}/lstate.c
  ${VELA_LUA_SRC}/lstring.c
  ${VELA_LUA_SRC}/ltable.c
  ${VELA_LUA_SRC}/ltm.c
  ${VELA_LUA_SRC}/lundump.c
  ${VELA_LUA_SRC}/lvm.c
  ${VELA_LUA_SRC}/lzio.c
  # Lua auxiliary library
  ${VELA_LUA_SRC}/lauxlib.c
  ${VELA_LUA_SRC}/lbaselib.c
  ${VELA_LUA_SRC}/lcorolib.c
  ${VELA_LUA_SRC}/ldblib.c
  ${VELA_LUA_SRC}/liolib.c
  ${VELA_LUA_SRC}/lmathlib.c
  ${VELA_LUA_SRC}/loadlib.c
  ${VELA_LUA_SRC}/loslib.c
  ${VELA_LUA_SRC}/lstrlib.c
  ${VELA_LUA_SRC}/ltablib.c
  ${VELA_LUA_SRC}/lutf8lib.c
  ${VELA_LUA_SRC}/linit.c
)

project_include_directories(PUBLIC ${VELA_LUA_SRC})

# Platform-specific definitions
if(UNIX AND NOT APPLE)
  project_compile_definitions(PRIVATE LUA_USE_LINUX)
  project_link_libraries(PRIVATE m dl)
elseif(APPLE)
  project_compile_definitions(PRIVATE LUA_USE_MACOSX)
  project_link_libraries(PRIVATE m)
elseif(WIN32)
  project_compile_definitions(PRIVATE LUA_USE_WINDOWS)
endif()

# Suppress warnings from Lua code
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  project_compile_options(PRIVATE -w)
elseif(MSVC)
  project_compile_options(PRIVATE /W0)
endif()

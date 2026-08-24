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

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
  MD5 81cf5265b8634967d8a7480d238168ce
)

project_add_library(STATIC
  # Lua source files (core library, no standalone interpreter)
  ${vela_ext_lua_LOCATION}/src/lapi.c
  ${vela_ext_lua_LOCATION}/src/lcode.c
  ${vela_ext_lua_LOCATION}/src/lctype.c
  ${vela_ext_lua_LOCATION}/src/ldebug.c
  ${vela_ext_lua_LOCATION}/src/ldo.c
  ${vela_ext_lua_LOCATION}/src/ldump.c
  ${vela_ext_lua_LOCATION}/src/lfunc.c
  ${vela_ext_lua_LOCATION}/src/lgc.c
  ${vela_ext_lua_LOCATION}/src/llex.c
  ${vela_ext_lua_LOCATION}/src/lmem.c
  ${vela_ext_lua_LOCATION}/src/lobject.c
  ${vela_ext_lua_LOCATION}/src/lopcodes.c
  ${vela_ext_lua_LOCATION}/src/lparser.c
  ${vela_ext_lua_LOCATION}/src/lstate.c
  ${vela_ext_lua_LOCATION}/src/lstring.c
  ${vela_ext_lua_LOCATION}/src/ltable.c
  ${vela_ext_lua_LOCATION}/src/ltm.c
  ${vela_ext_lua_LOCATION}/src/lundump.c
  ${vela_ext_lua_LOCATION}/src/lvm.c
  ${vela_ext_lua_LOCATION}/src/lzio.c
  # Lua auxiliary library
  ${vela_ext_lua_LOCATION}/src/lauxlib.c
  ${vela_ext_lua_LOCATION}/src/lbaselib.c
  ${vela_ext_lua_LOCATION}/src/lcorolib.c
  ${vela_ext_lua_LOCATION}/src/ldblib.c
  ${vela_ext_lua_LOCATION}/src/liolib.c
  ${vela_ext_lua_LOCATION}/src/lmathlib.c
  ${vela_ext_lua_LOCATION}/src/loadlib.c
  ${vela_ext_lua_LOCATION}/src/loslib.c
  ${vela_ext_lua_LOCATION}/src/lstrlib.c
  ${vela_ext_lua_LOCATION}/src/ltablib.c
  ${vela_ext_lua_LOCATION}/src/lutf8lib.c
  ${vela_ext_lua_LOCATION}/src/linit.c
)

project_include_directories(PUBLIC ${vela_ext_lua_LOCATION}/src)

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

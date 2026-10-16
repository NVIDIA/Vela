// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_app
#include <vsr/app/Context.h>
// vsr_core
#include <vsr/core/Logging.hpp>
#include <vsr/core/Timer.hpp>
// vsr_io
#include <vsr/io/archives.hpp>
// std
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Assemble a scene using the viewers' scene-loading flags (-vsr, -gltf, -obj,
// -usd, -l, ...) and save it as a Scene Archive instead of rendering it.
// No ANARI device is loaded.

namespace {

const char *DEFAULT_OUTPUT = "scene.vsr";

void printUsage(const char *programName)
{
  printf("Usage: %s [options] <scene inputs...>\n\n", programName);
  printf("Options:\n");
  printf("  -o, --output <file>        Scene Archive to write (default: %s)\n",
      DEFAULT_OUTPUT);
  printf("  -h, --help                 Show this help message\n");
  printf("\n");
  printf("Scene Archive Options:\n");
  printf("  -vsr <file>                Load a VSR Scene Archive\n");
  printf("\n");
  printf("Importer Options:\n");
  printf("  -gltf <file...>            Import GLTF/GLB files\n");
  printf("  -obj <file...>             Import Wavefront OBJ files\n");
  printf("  -ply <file...>             Import PLY files\n");
  printf(
      "  -volume <file...>          Import volume data (RAW, NVDB, VTI, etc.)\n");
  printf("  -hdri <file>               Set HDRI environment map\n");
  printf("  -silo <file...>            Import Silo files\n");
  printf("  -usd <file...>             Import USD files\n");
  printf("  -usd_mtlx <file...>        Import USD files as native MaterialX\n");
  printf("  -usd_mdl <file...>         Import USD files as native MDL\n");
  printf("  -usd_variant <set>=<var>   Select a USD variant for the USD files\n");
  printf("                             that follow (repeatable)\n");
  printf("  -l, --layer <name>         Specify layer name for following imports\n");
  printf("  -assimp <file...>          Import via ASSIMP (supports many formats)\n");
  printf("  -axyz <file...>            Import AXYZ point cloud files\n");
  printf("  -e57xyz <file...>          Import E57 point cloud files\n");
  printf("  -pdb <file...>             Import PDB molecule files\n");
  printf("  -swc <file...>             Import SWC neuron files\n");
  printf("  -trk <file...>             Import TRK track files\n");
  printf("  -nbody <file...>           Import N-body simulation files\n");
  printf("\n");
  printf("A Scene Archive holds no animations; an animated scene is saved at\n");
  printf("its current frame.\n");
  printf("\n");
  printf("Example:\n");
  printf("  %s -l geometry -gltf model.glb -hdri env.hdr -o scene.vsr\n",
      programName);
}

// Pull this tool's own options out of argv, leaving the scene-loading ones for
// Context. Returns false when the program should exit, with 'exitCode' set.
bool parseOptions(int argc,
    const char *argv[],
    std::string &outputFile,
    std::vector<std::string> &sceneArgs,
    int &exitCode)
{
  sceneArgs.push_back(argv[0]);
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      printUsage(argv[0]);
      exitCode = 0;
      return false;
    } else if (arg == "-o" || arg == "--output") {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires an argument\n", arg.c_str());
        exitCode = 1;
        return false;
      }
      outputFile = argv[++i];
    } else
      sceneArgs.push_back(arg);
  }
  return true;
}

// Report every scene input that names a file which does not exist. The
// importers only log a missing file, so without this check a typo would save
// an archive silently lacking that content.
bool allInputFilesExist(const vsr::app::CommandLineOptions &commandLine)
{
  std::vector<std::string> files;
  for (const auto &input : commandLine.sceneInputs) {
    if (const auto *archive = std::get_if<vsr::app::SceneArchiveLoad>(&input))
      files.push_back(archive->filename);
    else {
      // "<file>;<layer>[;<importer option>]"
      const auto &importFile =
          std::get<vsr::app::ForeignSceneImport>(input).file.second;
      files.push_back(importFile.substr(0, importFile.find(';')));
    }
  }
  for (const auto &sequence : commandLine.animationFilenames) {
    // "<file>[;<importer option>]"
    for (const auto &file : sequence.second)
      files.push_back(file.substr(0, file.find(';')));
  }

  bool allExist = true;
  for (const auto &file : files) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
      fprintf(stderr, "Error: input file '%s' does not exist\n", file.c_str());
      allExist = false;
    }
  }
  return allExist;
}

} // namespace

int main(int argc, const char *argv[])
{
  vsr::core::setLogToStdout();

  std::string outputFile = DEFAULT_OUTPUT;
  std::vector<std::string> sceneArgs;
  int exitCode = 0;
  if (!parseOptions(argc, argv, outputFile, sceneArgs, exitCode))
    return exitCode;

  auto ctx = std::make_unique<vsr::app::Context>();
  try {
    ctx->parseCommandLine(sceneArgs);
  } catch (const std::exception &e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }

  const auto &commandLine = ctx->commandLine;
  if (commandLine.loadedFromStateFile) {
    fprintf(stderr,
        "Error: '%s' is not preceded by an importer flag (use -vsr to load a "
        "Scene Archive)\n",
        commandLine.stateFile.c_str());
    return 1;
  }
  if (commandLine.sceneInputs.empty()
      && commandLine.animationFilenames.empty()) {
    fprintf(stderr, "Error: no scene inputs given\n\n");
    printUsage(argv[0]);
    return 1;
  }
  if (!allInputFilesExist(commandLine))
    return 1;

  vsr::core::Timer timer;

  printf("Loading scene data...\n");
  fflush(stdout);
  timer.start();
  bool loaded = ctx->loadCommandLineSceneInputs();
  vsr::io::import_animations(
      ctx->vsr.scene, ctx->vsr.animationMgr, commandLine.animationFilenames);
  timer.end();
  printf("...done (%.2f ms)\n", timer.milliseconds());

  if (!loaded) {
    fprintf(stderr, "Error: failed to load the scene inputs\n");
    return 1;
  }

  if (!ctx->vsr.animationMgr.animations().empty())
    printf("Scene is animated; saving its current frame without animations\n");

  printf("Saving Scene Archive '%s'...\n", outputFile.c_str());
  fflush(stdout);
  timer.start();
  const bool saved =
      vsr::io::save_SceneArchive(ctx->vsr.scene, outputFile.c_str());
  timer.end();

  if (!saved) {
    fprintf(stderr, "Error: failed to save '%s'\n", outputFile.c_str());
    return 1;
  }
  printf("...done (%.2f ms)\n", timer.milliseconds());

  return 0;
}

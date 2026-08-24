// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr
#include <vsr/core/Logging.hpp>
#include <vsr/io/exporters.hpp>
#include <vsr/io/importers.hpp>
#include <vsr/scene/Scene.hpp>
#include <vsr/scene/objects/SpatialField.hpp>
// fmt
#include <fmt/format.h>
// std
#include <optional>
#include <string>
#include <string_view>

static void printUsage(std::string_view progName)
{
  fmt::print("usage: {} [options] <input_volume> <output.vdb>\n", progName);
  fmt::print("\n");
  fmt::print("Options:\n");
  fmt::print("  --help, -h              Show this help message\n");
  fmt::print(
      "  --undefined <value>     Skip voxels with this undefined value\n");
  fmt::print("  -u <value>              Short form of --undefined\n");
  fmt::print(
      "  --precision <type>      Quantization precision (fp4|fp8|fp16|fpn|half|float32)\n");
  fmt::print("                          Default: fp16\n");
  fmt::print("  -p <type>               Short form of --precision\n");
  fmt::print("  --dither                Enable dithering for quantization\n");
  fmt::print("  -d                      Short form of --dither\n");
  fmt::print("\n");
  fmt::print("Examples:\n");
  fmt::print("  {} input.raw output.vdb\n", progName);
  fmt::print("  {} --undefined 0.0 input.vti output.vdb\n", progName);
  fmt::print("  {} --undefined 0.5 input.mhd output.vdb\n", progName);
  fmt::print("  {} --precision fp8 --dither input.mhd output.vdb\n", progName);
  fmt::print("\n");
  fmt::print("Supported input formats: .raw, .vti, .vtu, .mhd, .hdf5, .nvdb\n");
}

int main(int argc, const char *argv[])
{
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  // Check for help flag
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
  }

  std::optional<float> undefinedValue;
  std::optional<std::string> inputFile;
  std::optional<std::string> outputFile;
  vsr::io::VDBPrecision precision = vsr::io::VDBPrecision::Fp16;
  bool enableDithering = false;

  vsr::core::setLogToStderr();

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};

    if (arg == "--undefined" || arg == "-u") {
      if (i + 1 >= argc) {
        vsr::core::logError(
            "Option %s requires a value", std::string(arg).c_str());
        printUsage(argv[0]);
        return 1;
      }
      try {
        undefinedValue = std::stof(std::string(argv[++i]));
      } catch (const std::exception &e) {
        vsr::core::logError(
            "Invalid undefined value: %s", std::string(argv[i]).c_str());
        printUsage(argv[0]);
        return 1;
      }
    } else if (arg == "--precision" || arg == "-p") {
      if (i + 1 >= argc) {
        vsr::core::logError("Option --precision requires a value");
        printUsage(argv[0]);
        return 1;
      }
      std::string_view precStr = argv[++i];
      if (precStr == "fp4") {
        precision = vsr::io::VDBPrecision::Fp4;
      } else if (precStr == "fp8") {
        precision = vsr::io::VDBPrecision::Fp8;
      } else if (precStr == "fp16") {
        precision = vsr::io::VDBPrecision::Fp16;
      } else if (precStr == "fpn") {
        precision = vsr::io::VDBPrecision::FpN;
      } else if (precStr == "half") {
        precision = vsr::io::VDBPrecision::Half;
      } else if (precStr == "float32") {
        precision = vsr::io::VDBPrecision::Float32;
      } else {
        vsr::core::logError(
            "Unknown precision type: %s", std::string(precStr).c_str());
        printUsage(argv[0]);
        return 1;
      }
    } else if (arg == "--dither" || arg == "-d") {
      enableDithering = true;
    } else if (argv[i][0] != '-') {
      if (!inputFile) {
        inputFile = std::string(arg);
      } else if (!outputFile) {
        outputFile = std::string(arg);
      } else {
        vsr::core::logError(
            "Unexpected positional argument: %s", std::string(arg).c_str());
        printUsage(argv[0]);
        return 1;
      }
    } else {
      vsr::core::logError("Unknown option: %s", std::string(arg).c_str());
      printUsage(argv[0]);
      return 1;
    }
  }

  if (!inputFile || !outputFile) {
    vsr::core::logError("Missing required arguments");
    printUsage(argv[0]);
    return 1;
  }

  vsr::core::logStatus("Loading volume from: %s", inputFile->c_str());

  vsr::scene::Scene scene;
  auto volume = vsr::io::import_volume(scene, inputFile->c_str());

  if (!volume) {
    vsr::core::logError("Failed to load volume");
    return 1;
  }

  const auto *spatialField =
      volume->parameterValueAsObject<vsr::scene::SpatialField>("value");

  if (!spatialField) {
    vsr::core::logError("Volume does not have a spatial field");
    return 1;
  }

  const auto subtype = spatialField->subtype();
  const bool isRectilinear =
      subtype == vsr::scene::tokens::spatial_field::structuredRectilinear;

  if (isRectilinear) {
    vsr::core::logStatus(
        "Detected rectilinear grid; writing NanoVDB sidecar with axis coordinates.");
  }

  vsr::io::export_StructuredVolumeToNanoVDB(spatialField,
      outputFile->c_str(),
      undefinedValue.has_value(),
      undefinedValue.value_or(0.0f),
      precision,
      enableDithering);

  vsr::core::logStatus("Export complete");
  return 0;
}

// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RenderDeviceConfig.h"
#include "RenderRuntime.h"
// vsr_app
#include <vsr/app/ApplicationDump.h>
#include <vsr/app/Context.h>
#include "vsr/app/renderAnimationSequence.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/io/procedural.hpp"
// std
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Input
{
  std::string path;
  std::string importer;
  std::string layer;
  std::string property;
  size_t sequence{0};
};

struct DirectionalLight
{
  vsr::math::float3 direction;
  vsr::math::float3 color;
  float irradiance;
};

struct Options
{
  bool help{false};
  std::vector<Input> inputs;
  std::optional<std::string> output;
  bool animation{false};
  bool overwrite{false};
  std::optional<int> frame;
  std::optional<std::string> frames;
  std::optional<std::string> camera;
  std::optional<size_t> cameraIndex;
  std::optional<std::string> device;
  std::optional<std::string> renderer;
  vsr::render_cli::RendererAssignments rendererParameters;
  bool defaultCamera{false};
  bool defaultLights{false};
  std::vector<DirectionalLight> directionalLights;
  std::vector<vsr::app::OfflineCameraParameter> cameraParameters;
  std::optional<vsr::math::float3> lookAt;
  std::optional<uint32_t> width;
  std::optional<uint32_t> height;
  std::optional<uint32_t> samples;
  std::optional<vsr::rendering::AOVType> aov;
  std::optional<float> depthMin;
  std::optional<float> depthMax;
  std::optional<bool> edgeInvert;
};

const char *aovName(vsr::rendering::AOVType type)
{
  using vsr::rendering::AOVType;
  switch (type) {
  case AOVType::NONE:
    return "none";
  case AOVType::DEPTH:
    return "depth";
  case AOVType::ALBEDO:
    return "albedo";
  case AOVType::NORMAL:
    return "normal";
  case AOVType::EDGES:
    return "edges";
  case AOVType::OBJECT_ID:
    return "object-id";
  case AOVType::PRIMITIVE_ID:
    return "primitive-id";
  case AOVType::INSTANCE_ID:
    return "instance-id";
  }
  throw std::runtime_error("Invalid AOV selection");
}

void require(bool valid, const std::string &message)
{
  if (!valid)
    throw std::runtime_error(message);
}

bool isDigits(const std::string &text)
{
  return !text.empty()
      && text.find_first_not_of("0123456789") == std::string::npos;
}

uint32_t positiveNumber(const std::string &text, const std::string &option)
{
  uint32_t value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  require(isDigits(text) && result.ec == std::errc()
          && result.ptr == text.data() + text.size() && value > 0,
      option + " requires a positive integer in range");
  return value;
}

size_t cameraIndexNumber(const std::string &text)
{
  size_t value = vsr::core::INVALID_INDEX;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  require(isDigits(text) && result.ec == std::errc()
          && result.ptr == text.data() + text.size(),
      "Invalid Camera index for --camera-index: " + text
          + " (requires an unsigned decimal object index in range)");
  return value;
}

int frameNumber(const std::string &text, const std::string &option)
{
  int value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  require(isDigits(text) && result.ec == std::errc()
          && result.ptr == text.data() + text.size(),
      option + " requires a nonnegative decimal integer in range");
  return value;
}

float finiteNumber(const std::string &text, const std::string &option)
{
  char *end = nullptr;
  errno = 0;
  const float value = std::strtof(text.c_str(), &end);
  require(!text.empty() && text.find_first_of(" \t\n\r") == std::string::npos
          && end == text.c_str() + text.size() && errno != ERANGE
          && std::isfinite(value),
      option + " requires a finite float in range");
  return value;
}

vsr::io::ImporterType importerType(const Input &input);

Options parseOptions(int argc, char **argv)
{
  Options options;
  bool positionalOnly = false;
  Input routing;
  routing.layer = "default";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (!positionalOnly && arg == "--") {
      positionalOnly = true;
    } else if (!positionalOnly && arg == "--help") {
      options.help = true;
    } else if (!positionalOnly && arg == "--overwrite") {
      options.overwrite = true;
    } else if (!positionalOnly && arg == "--animation") {
      options.animation = true;
    } else if (!positionalOnly && arg == "--default-camera") {
      options.defaultCamera = true;
    } else if (!positionalOnly && arg == "--default-lights") {
      options.defaultLights = true;
    } else if (!positionalOnly && arg == "--directional-light") {
      require(i + 7 < argc, "Missing operands for " + arg);
      float values[7];
      for (auto &value : values)
        value = finiteNumber(argv[++i], arg);
      const double length = std::sqrt(double(values[0]) * values[0]
          + double(values[1]) * values[1] + double(values[2]) * values[2]);
      require(length > 0., arg + " requires a nonzero direction");
      require(values[3] >= 0.f && values[4] >= 0.f && values[5] >= 0.f
              && values[6] >= 0.f,
          arg + " requires nonnegative color and irradiance");
      options.directionalLights.push_back({{float(values[0] / length),
                                               float(values[1] / length),
                                               float(values[2] / length)},
          {values[3], values[4], values[5]},
          values[6]});
    } else if (!positionalOnly
        && (arg == "--background" || arg == "--ambient-color")) {
      const int count = arg == "--background" ? 4 : 3;
      require(i + count < argc, "Missing vector operands for " + arg);
      std::string value;
      for (int component = 0; component < count; ++component) {
        if (component)
          value += ',';
        value += argv[++i];
      }
      options.rendererParameters.emplace_back(
          arg == "--background" ? "background" : "ambientColor", value);
    } else if (!positionalOnly
        && (arg == "--camera-position" || arg == "--camera-look-at"
            || arg == "--camera-up")) {
      require(i + 3 < argc, "Missing vector operands for " + arg);
      const float x = finiteNumber(argv[++i], arg);
      const float y = finiteNumber(argv[++i], arg);
      const float z = finiteNumber(argv[++i], arg);
      const vsr::math::float3 value(x, y, z);
      if (arg == "--camera-look-at")
        options.lookAt = value;
      else
        options.cameraParameters.push_back(
            {arg == "--camera-position" ? "position" : "up", value});
    } else if (!positionalOnly
        && (arg == "--camera-fovy" || arg == "--camera-height"
            || arg == "--camera-aperture" || arg == "--camera-focus")) {
      require(i + 1 < argc, "Missing operand for " + arg);
      float value = finiteNumber(argv[++i], arg);
      const bool aperture = arg == "--camera-aperture";
      require(
          aperture ? value >= 0.f : value > 0.f, "Invalid value for " + arg);
      const char *name = aperture    ? "apertureRadius"
          : arg == "--camera-focus"  ? "focusDistance"
          : arg == "--camera-height" ? "height"
                                     : "fovy";
      if (arg == "--camera-fovy") {
        require(
            value < 180.f, "--camera-fovy must be between 0 and 180 degrees");
        value = vsr::math::radians(value);
        require(value > 0.f, "--camera-fovy conversion underflows");
      }
      options.cameraParameters.push_back({name, value});
    } else if (!positionalOnly && !arg.empty() && arg[0] == '-') {
      require(arg == "--output" || arg == "--camera" || arg == "--camera-index"
              || arg == "--width" || arg == "--height" || arg == "--samples"
              || arg == "--importer" || arg == "--layer"
              || arg == "--vtu-property" || arg == "--device"
              || arg == "--renderer" || arg == "--renderer-param"
              || arg == "--ambient" || arg == "--frame" || arg == "--frames"
              || arg == "--aov" || arg == "--depth-min" || arg == "--depth-max"
              || arg == "--edge-invert",
          "Unknown option: " + arg);
      require(i + 1 < argc, "Missing operand for " + arg);
      const std::string value = argv[++i];
      require(!value.empty(), "Missing or invalid operand for " + arg);
      if (arg == "--importer") {
        routing.importer = value == "auto" ? "" : value;
        if (!routing.importer.empty())
          importerType(routing);
        ++routing.sequence;
      } else if (arg == "--layer") {
        require(value.find(';') == std::string::npos,
            "--layer cannot contain the import delimiter ';'");
        routing.layer = value;
        ++routing.sequence;
      } else if (arg == "--vtu-property") {
        require(value.find(';') == std::string::npos,
            "--vtu-property cannot contain the import delimiter ';'");
        routing.property = value;
        ++routing.sequence;
      } else if (arg == "--output")
        options.output = value;
      else if (arg == "--frame")
        options.frame = frameNumber(value, arg);
      else if (arg == "--frames")
        options.frames = value;
      else if (arg == "--camera")
        options.camera = value;
      else if (arg == "--camera-index")
        options.cameraIndex = cameraIndexNumber(value);
      else if (arg == "--device")
        options.device = value;
      else if (arg == "--renderer")
        options.renderer = value;
      else if (arg == "--renderer-param") {
        const auto equals = value.find('=');
        require(equals != std::string::npos && equals > 0,
            "--renderer-param requires name=value");
        options.rendererParameters.emplace_back(
            value.substr(0, equals), value.substr(equals + 1));
      } else if (arg == "--ambient")
        options.rendererParameters.emplace_back("ambientRadiance", value);
      else if (arg == "--aov") {
        using vsr::rendering::AOVType;
        bool found = false;
        for (const auto type : {AOVType::NONE,
                 AOVType::DEPTH,
                 AOVType::ALBEDO,
                 AOVType::NORMAL,
                 AOVType::EDGES,
                 AOVType::OBJECT_ID,
                 AOVType::PRIMITIVE_ID,
                 AOVType::INSTANCE_ID}) {
          if (value == aovName(type)) {
            options.aov = type;
            found = true;
          }
        }
        require(found, "Unknown AOV visualization: " + value + " (see --help)");
      } else if (arg == "--depth-min")
        options.depthMin = finiteNumber(value, arg);
      else if (arg == "--depth-max")
        options.depthMax = finiteNumber(value, arg);
      else if (arg == "--edge-invert") {
        require(
            value == "true" || value == "false" || value == "1" || value == "0",
            "--edge-invert requires true, false, 1 or 0");
        options.edgeInvert = value == "true" || value == "1";
      } else if (arg == "--width")
        options.width = positiveNumber(value, arg);
      else if (arg == "--height")
        options.height = positiveNumber(value, arg);
      else
        options.samples = positiveNumber(value, arg);
    } else {
      routing.path = arg;
      options.inputs.push_back(routing);
    }
  }
  require(!options.frame || !options.animation,
      "--frame cannot be combined with --animation");
  require(
      !options.frames || options.animation, "--frames requires --animation");
  require(!options.camera || !options.cameraIndex,
      "--camera cannot be combined with --camera-index");
  require(!options.defaultCamera || (!options.camera && !options.cameraIndex),
      "--default-camera cannot be combined with --camera or --camera-index");
  return options;
}

void printHelp()
{
  std::puts(
      "Usage: vsrRender [options] INPUT...\n"
      "Render a still (default) or an explicit animation from imports/native state.\n"
      "CLI reference: src/apps/tools/README.md (canonical detailed policy).\n"
      "Native inputs are Application Dumps or Scene Archives.\n"
      "Binary and Text Encodings and legacy scenes are accepted regardless\n"
      "of filename extension. Dumps use their saved current frame and supplied\n"
      "offline settings; scenes/imports use frame 0. The native base loads first;\n"
      "foreign imports retain argument order. No prompts, implicit cameras,\n"
      "saved poses, implicit sequences, demo scenes or implicit lights.\n\n"
      "  --importer NAME Select importer for subsequent foreign inputs; auto resets\n"
      "                  to extension detection. Native content always loads.\n"
      "                  agx, assimp, assimp-flat, axyz, dlaf, e57xyz, ensight,\n"
      "                  gltf, hdri, hsmesh, nbody, obj, pdb, pbrt, ply, pointsbin,\n"
      "                  pt, silo, smesh, smesh-animation, swc, swc-sdf, trk,\n"
      "                  usd, usd-mtlx, vtp, vtu, xyzdp, volume, volume-animation, xf\n"
      "                  pointsbin/volume-animation group subsequent files into\n"
      "                  one sequence until routing changes; bindings are retained.\n"
      "  --layer NAME    Assign subsequent imports to this layer (default: default)\n"
      "  --vtu-property NAME Select a VTU volume property for subsequent inputs\n"
      "  --help          Show this help without an input\n"
      "  --frame N       Select one existing zero-based timeline frame\n"
      "                  Default still: saved current frame, otherwise frame 0.\n"
      "  --animation     Render saved subset/stride or full existing timeline\n"
      "                  Requires animated content; conflicts with --frame.\n"
      "  --frames START:END[:STEP] Requires --animation; overrides saved selection\n"
      "                  Inclusive end, positive step (default 1); no retiming.\n"
      "  --output PATH   Exact still PNG (all text literal) or sequence pattern\n"
      "                  Sequence: exactly one {frame:04}, no other braces.\n"
      "                  Four-digit minimum padding of the actual timeline index.\n"
      "                  Explicit output overrides saved directory/prefix.\n"
      "                  Saved: directory/prefix.png or directory/prefix0000.png.\n"
      "                  Stills omit one trailing underscore from the saved prefix.\n"
      "                  Otherwise: vsrRender.png or vsrRender_{frame:04}.png.\n"
      "                  Missing destination directories are created.\n"
      "  --overwrite     Permit replacing existing selected output files\n"
      "                  The complete job is preflighted before any image writes.\n"
      "                  Existing outputs otherwise refuse the entire job; invalid\n"
      "                  paths/types and collisions fail even with --overwrite.\n"
      "                  Write failures fail the command; partial files may remain.\n"
      "  --camera NAME   Exact Camera name, including numeric-only names\n"
      "  --camera-index N Unsigned decimal Camera object index\n"
      "                  Selectors conflict; invalid explicit requests never fall back.\n"
      "                  Without either, prefer saved selection, then the sole Camera.\n"
      "  --default-camera Generate/select a perspective Camera, even if others exist\n"
      "                  Conflicts with both selectors; fit once at the\n"
      "                  global first selected frame, not each rank's first frame.\n"
      "  --camera-position X Y Z World-space position\n"
      "  --camera-look-at X Y Z  World-space target (recomputed after animation)\n"
      "  --camera-up X Y Z       Nonzero up vector, not parallel to view direction\n"
      "  --camera-fovy DEGREES   Perspective vertical FOV, strictly between 0 and 180\n"
      "  --camera-height VALUE   Orthographic image-plane height, positive\n"
      "  --camera-aperture VALUE Aperture radius, nonnegative\n"
      "  --camera-focus VALUE    Focus distance, positive\n"
      "                  Only supported subtype/device controls are accepted.\n"
      "                  Saved values precede explicit values; repeats are last-wins.\n"
      "  --directional-light DX DY DZ R G B IRRADIANCE Repeatable scene Light\n"
      "                  Finite floats; nonzero world-space emission direction\n"
      "                  is normalized; color and irradiance are nonnegative.\n"
      "                  Every occurrence adds a Light, preserving authored lights.\n"
      "  --default-lights Add the existing default-light setup once, even if lit\n"
      "                  Adds to authored/requested lights; never enables itself.\n"
      "                  Neither light option changes background/ambient settings.\n"
      "                  Unlit, emissive and HDRI scenes receive no implicit lights.\n"
      "  --width N       Positive output width (default: 1024)\n"
      "  --height N      Positive output height (default: 768)\n"
      "  --samples N     Positive samples per pixel (default: 128)\n"
      "                  Defaults apply only to absent saved/explicit fields.\n"
      "  --aov MODE      none, depth, albedo, normal, edges, object-id,\n"
      "                  primitive-id, instance-id (default: none/color image).\n"
      "                  Explicit none disables saved AOV visualization.\n"
      "                  Requires device channel support; missing buffers fail.\n"
      "                  Writes existing visualization PNGs, not raw/HDR channels.\n"
      "  --depth-min VALUE Depth visualization minimum (default: 0)\n"
      "  --depth-max VALUE Depth visualization maximum (default: 1)\n"
      "                  Finite increasing range with representable span.\n"
      "  --edge-invert BOOL Invert edges: true/false/1/0 (default: false)\n"
      "                  Absent controls preserve saved fields; repeats last-win.\n"
      "  --device ID     [Device Subtype@]ANARI Library; default@helide = helide\n"
      "                  Precedence: explicit, saved, first VSR_ANARI_LIBRARIES,\n"
      "                  environment if ANARI_LIBRARY is set, otherwise helide.\n"
      "                  Selected library/subtype failures never try a fallback.\n"
      "  --renderer NAME Select renderer subtype (saved compatible or default)\n"
      "                  Device changes use the new device's default renderer;\n"
      "                  subtype/device changes reset saved parameters to defaults.\n"
      "  --renderer-param name=value Repeatable metadata-validated assignment\n"
      "                  Decimal scalars, true/false/0/1 booleans, literal strings,\n"
      "                  comma-separated numeric vectors (e.g. background=0,0,1,1).\n"
      "                  Numeric whitespace, invalid/ambiguous types or values,\n"
      "                  unknown names and object/array constructions fail.\n"
      "  --background R G B A Shorthand for renderer background\n"
      "  --ambient VALUE     Shorthand for renderer ambientRadiance\n"
      "  --ambient-color R G B Shorthand for renderer ambientColor\n"
      "                  All assignments are validated; later assignments win,\n"
      "                  including shorthand. Absent flags never override saved values.\n"
      "  --              End options\n\n"
      "Defaults: selected device/default renderer, or compatible saved renderer.\n"
      "Only explicit quality flags override saved values. Authored subtype/lens\n"
      "parameters are preserved; the engine supplies the output aspect.\n"
      "Example: vsrRender scene.data --camera 'Main Camera' --width 320 \\\n"
      "           --height 240 --samples 4 --output preview.png\n"
      "By index: vsrRender scene.data --camera-index 12 --samples 4\n"
      "By numeric name: vsrRender scene.data --camera 12 --samples 4\n"
      "Imports: vsrRender authored-camera.pbrt --width 320 --height 240 --samples 4\n"
      "Augment: vsrRender base.vsr --layer detail --importer obj detail.obj\n"
      "Camera-less input: vsrRender model.obj --default-camera --samples 4\n"
      "Opt-in lighting: vsrRender model.obj --default-camera --default-lights\n"
      "Explicit light: vsrRender scene.vsr --directional-light 0 0 -1 1 1 1 3\n"
      "Renderer: vsrRender scene.vsr --device helide --renderer default \\\n"
      "            --renderer-param mode=hitSurface --background 0 0 1 1 --samples 4\n"
      "Sequence: vsrRender saved.vsr --animation --frames 2:10:3 \\\n"
      "            --output 'renders/shot_{frame:04}.png'  # frames 2, 5, 8\n"
      "Depth: vsrRender scene.vsr --aov depth --depth-min 0 --depth-max 10\n"
      "Edges: vsrRender saved.vsr --aov edges --edge-invert false\n"
      "Color: vsrRender saved.vsr --aov none --width 320 --height 240 --samples 4\n"
      "Replace: add --overwrite to explicitly permit replacing selected images.\n"
      "Generated framing stays fixed; later animated motion may leave its view.\n"
      "MPI (VSR_USE_MPI=ON):\n"
      "  mpiexec -n 2 vsrRender saved.vsr --animation --frames 20:27:2 \\\n"
      "    --default-camera --output 'parallel/shot_{frame:04}.png'\n"
      "Selected sequence positions are round-robin: 20,22,24,26 on two ranks\n"
      "go to rank 0: 20,24 and rank 1: 22,26. Names retain timeline indices.\n"
      "Stills render only on rank 0; samples within a frame are not distributed.\n"
      "Use identical inputs/options and a shared output filesystem on all ranks.\n"
      "Global preflight precedes writes; any rank failure fails the invocation.\n"
      "Partial runtime outputs may remain; ordinary errors complete collectively,\n"
      "fatal MPI/resource errors abort. Generation uses the global first frame.\n"
      "Importer warnings are advisory; errors/no-content imports fail.\n"
      "Warnings may describe lossy or incomplete conversions; review diagnostics.\n"
      "Optional features require build dependencies. Foreign paths/layer/property\n"
      "names cannot contain ';'.");
}

std::string applicationEvidence(vsr::core::DataNode &root)
{
  // Presence, not successful decoding, prevents malformed envelopes from
  // falling through to the scene compatibility reader.
  for (const auto *name : {"archives",
           "ANARIDeviceManager",
           "offlineRendering",
           "settings",
           "cameraPoses",
           "animations"}) {
    if (root.child(name))
      return name;
    if (const auto *context = root.child("context");
        context && context->child(name))
      return std::string("context/") + name;
  }
  // I/O also accepts scene-only context wrappers, including legacy payloads.
  // Invalid scene content alone is not evidence of application state.
  return {};
}

// Persistence readers are deliberately permissive and may create absent nodes.
// Validate and retain presence before invoking them, so absence stays absence.
vsr::core::DataNode *section(vsr::core::DataNode &node, const char *name)
{
  auto *child = node.child(name);
  require(!child || !child->getValue().valid(),
      std::string("Malformed Application Dump section: ") + name);
  return child;
}

template <typename T>
bool savedField(
    vsr::core::DataNode &node, const char *name, anari::DataType type, T &value)
{
  const auto *child = node.child(name);
  require(!child || (child->isLeaf() && child->getValue(type, &value)),
      std::string("Invalid Application Dump field: ") + name);
  return child != nullptr;
}

void validateQuality(uint32_t width, uint32_t height, uint32_t samples)
{
  const uint64_t pixels = uint64_t(width) * height;
  require(width > 0 && height > 0 && samples > 0
          && width <= uint32_t(std::numeric_limits<int>::max() / 4)
          && height <= uint32_t(std::numeric_limits<int>::max())
          && pixels <= uint64_t(std::numeric_limits<int>::max()) / 16,
      "Invalid dimensions or image resource-size arithmetic");
  require(samples <= uint32_t(std::numeric_limits<int>::max()),
      "Sample count exceeds supported range");
}

struct SavedState
{
  vsr::app::OfflineRenderSequenceConfig config;
  bool cameraIndex{false};
  bool outputDirectory{false};
  bool filePrefix{false};
  int frame{0};
};

SavedState validateDump(vsr::core::DataNode &root)
{
  SavedState saved;
  auto *context = section(root, "context");
  auto &payload = context ? *context : root;
  for (auto *node : {&root, &payload}) {
    if (auto *settings = section(*node, "settings")) {
      bool value = false;
      savedField(*settings, "logVerbose", ANARI_BOOL, value);
      savedField(*settings, "logEchoOutput", ANARI_BOOL, value);
    }
    if (auto *devices = section(*node, "ANARIDeviceManager")) {
      int kind = 0;
      savedField(*devices, "renderIndexKind", ANARI_INT32, kind);
    }
    // Poses are inert presentation state, not render settings or scene Cameras.
    // Check the envelope, but leave their internals to the persistence reader.
    section(*node, "cameraPoses");
  }
  auto *archives = section(root, "archives");
  vsr::core::DataNode *timeline = archives
      ? section(*archives, "animationManager")
      : section(payload, "animations");
  if (timeline) {
    float time = 0.f, fps = 30.f, increment = 0.01f;
    int frames = 100;
    savedField(*timeline, "time", ANARI_FLOAT32, time);
    savedField(*timeline, "fps", ANARI_FLOAT32, fps);
    savedField(*timeline, "increment", ANARI_FLOAT32, increment);
    savedField(*timeline, "totalFrames", ANARI_INT32, frames);
    require(std::isfinite(time) && time >= 0.f && time <= 1.f
            && std::isfinite(fps) && fps > 0.f && std::isfinite(increment)
            && frames >= 2,
        "Invalid Application Dump saved animation frame/timeline");
    // Match the manager's float time-to-frame mapping, but check its rounded
    // result before the persistence reader or an integer cast can clamp it.
    const double frame = std::round(time * (frames - 1));
    require(frame >= 0. && frame <= double(frames - 1),
        "Invalid Application Dump saved animation frame arithmetic");
    saved.frame = int(frame);
  }
  auto *offline = section(root, "offlineRendering");
  if (!offline && context)
    offline = section(payload, "offlineRendering");
  if (!offline)
    return saved;
  auto &cfg = saved.config;
  if (auto *frame = section(*offline, "frame")) {
    savedField(*frame, "width", ANARI_UINT32, cfg.frame.width);
    savedField(*frame, "height", ANARI_UINT32, cfg.frame.height);
    savedField(*frame, "samples", ANARI_UINT32, cfg.frame.samples);
    int format = int(cfg.frame.colorFormat);
    savedField(*frame, "colorFormat", ANARI_INT32, format);
    require(format == ANARI_UFIXED8_RGBA_SRGB || format == ANARI_UFIXED8_VEC4
            || format == ANARI_FLOAT32_VEC4,
        "Invalid Application Dump colorFormat");
    savedField(*frame, "numFrames", ANARI_INT32, cfg.frame.numFrames);
    savedField(*frame, "renderSubset", ANARI_BOOL, cfg.frame.renderSubset);
    savedField(*frame, "startFrame", ANARI_INT32, cfg.frame.startFrame);
    savedField(*frame, "endFrame", ANARI_INT32, cfg.frame.endFrame);
    savedField(*frame, "frameIncrement", ANARI_INT32, cfg.frame.frameIncrement);
    require(cfg.frame.width > 0 && cfg.frame.height > 0 && cfg.frame.samples > 0
            && cfg.frame.numFrames > 0 && cfg.frame.startFrame >= 0
            && cfg.frame.endFrame >= cfg.frame.startFrame
            && cfg.frame.frameIncrement > 0,
        "Invalid Application Dump frame settings");
  }
  if (auto *camera = section(*offline, "camera")) {
    saved.cameraIndex = savedField(
        *camera, "cameraIndex", ANARI_UINT64, cfg.camera.cameraIndex);
    if (camera->child("apertureRadius") || camera->child("focusDistance"))
      vsr::core::logWarning(
          "Ignoring legacy offline lens settings; use Scene Camera parameters "
          "or explicit --camera-aperture/--camera-focus overrides");
  }
  if (auto *renderer = section(*offline, "renderer")) {
    const bool active = savedField(
        *renderer, "activeRenderer", ANARI_INT32, cfg.renderer.activeRenderer);
    savedField(
        *renderer, "libraryName", ANARI_STRING, cfg.renderer.libraryName);
    auto *objects = section(*renderer, "rendererObjects");
    if (objects) {
      objects->foreach_child([&](auto &object) {
        std::string subtype = object.name(), name;
        savedField(object, "subtype", ANARI_STRING, subtype);
        savedField(object, "name", ANARI_STRING, name);
        require(!subtype.empty(), "Invalid saved renderer subtype");
        require(!object.getValue().valid(), "Malformed saved renderer object");
        if (auto *parameters = section(object, "parameters")) {
          parameters->foreach_child([&](auto &parameter) {
            bool enabled = true;
            std::string description;
            int usage = 0;
            savedField(parameter, "enabled", ANARI_BOOL, enabled);
            savedField(parameter, "description", ANARI_STRING, description);
            savedField(parameter, "usage", ANARI_INT32, usage);
            const auto *value = parameter.child("value");
            require(!parameter.name().empty() && value && value->isLeaf()
                    && value->getValue().valid(),
                "Invalid Application Dump renderer parameter value");
          });
        }
      });
    }
    require(!active
            || (objects && cfg.renderer.activeRenderer >= 0
                && size_t(cfg.renderer.activeRenderer) < objects->numChildren())
            || (cfg.renderer.activeRenderer == -1
                && (!objects || objects->numChildren() == 0)),
        "Invalid Application Dump active renderer reference");
    require(!objects || objects->numChildren() == 0 || active,
        "Saved renderer collection requires an active renderer reference");
  }
  if (auto *aov = section(*offline, "aov")) {
    int type = int(cfg.aov.aovType);
    savedField(*aov, "aovType", ANARI_INT32, type);
    savedField(*aov, "depthMin", ANARI_FLOAT32, cfg.aov.depthMin);
    savedField(*aov, "depthMax", ANARI_FLOAT32, cfg.aov.depthMax);
    savedField(*aov, "edgeInvert", ANARI_BOOL, cfg.aov.edgeInvert);
    require(type >= int(vsr::rendering::AOVType::NONE)
            && type <= int(vsr::rendering::AOVType::INSTANCE_ID)
            && vsr::rendering::isValidDepthRange(
                cfg.aov.depthMin, cfg.aov.depthMax),
        "Invalid Application Dump AOV settings");
  }
  if (auto *output = section(*offline, "output")) {
    saved.outputDirectory = savedField(
        *output, "outputDirectory", ANARI_STRING, cfg.output.outputDirectory);
    saved.filePrefix =
        savedField(*output, "filePrefix", ANARI_STRING, cfg.output.filePrefix);
  }
  validateQuality(cfg.frame.width, cfg.frame.height, cfg.frame.samples);
  cfg.loadSettings(*offline);
  return saved;
}

std::vector<int> selectFrames(const Options &options,
    const SavedState &saved,
    const vsr::animation::AnimationManager &manager)
{
  const int total = manager.getAnimationTotalFrames();
  if (!options.animation) {
    const int frame = options.frame.value_or(saved.frame);
    require(frame >= 0 && frame < total,
        "Selected frame is outside the animation timeline");
    return {frame};
  }
  bool animated = false;
  for (const auto &animation : manager.animations()) {
    animated |= !animation.objectParameterBindings().empty()
        || !animation.transformBindings().empty()
        || !animation.callbackBindings().empty()
        || !animation.fileBindings().empty();
  }
  require(animated, "No animation to render; use a still without --animation");
  const auto &config = saved.config.frame;
  int start = config.renderSubset ? config.startFrame : 0;
  int end = config.renderSubset ? config.endFrame : total - 1;
  int step = config.frameIncrement;
  if (options.frames) {
    const auto &range = *options.frames;
    const auto first = range.find(':');
    const auto second =
        range.find(':', first == std::string::npos ? 0 : first + 1);
    require(first != std::string::npos
            && (second == std::string::npos
                || range.find(':', second + 1) == std::string::npos),
        "--frames requires START:END[:STEP]");
    start = frameNumber(range.substr(0, first), "--frames start");
    end = frameNumber(range.substr(first + 1,
                          second == std::string::npos ? std::string::npos
                                                      : second - first - 1),
        "--frames end");
    step = second == std::string::npos
        ? 1
        : frameNumber(range.substr(second + 1), "--frames step");
  }
  require(start >= 0 && end >= start && end < total && step > 0,
      "Invalid frame range: require 0 <= START <= END < timeline length and STEP > 0");
  std::vector<int> frames;
  for (int frame = start;;) {
    frames.push_back(frame);
    // Subtract first so even an INT_MAX step cannot overflow the next index.
    if (step > end - frame)
      break;
    frame += step;
  }
  return frames;
}

std::vector<vsr::app::OfflineRenderFrame> resolveDestinations(
    const Options &options,
    const SavedState &saved,
    const std::vector<int> &frames)
{
  auto prefix = saved.filePrefix ? saved.config.output.filePrefix
      : options.animation        ? "vsrRender_"
                                 : "vsrRender";
  if (!options.animation && !prefix.empty() && prefix.back() == '_')
    prefix.pop_back();
  const auto directory =
      saved.outputDirectory ? saved.config.output.outputDirectory : ".";
  const auto filename =
      prefix + (options.animation ? "{frame:04}" : "") + ".png";
  const std::string pattern = options.output.value_or(
      (std::filesystem::path(directory) / filename).string());
  require(pattern.size() >= 4
          && pattern.compare(pattern.size() - 4, 4, ".png") == 0,
      "--output must end in .png");
  const std::string token = "{frame:04}";
  const auto position = pattern.find(token);
  if (options.animation) {
    require(position != std::string::npos,
        "Sequence --output requires exactly one {frame:04} placeholder");
    auto literal = pattern;
    literal.erase(position, token.size());
    require(literal.find_first_of("{}") == std::string::npos,
        "Malformed sequence pattern: only one {frame:04} placeholder is supported");
  }
  std::set<std::string> destinations;
  std::vector<vsr::app::OfflineRenderFrame> resolved;
  for (int frame : frames) {
    auto destination = pattern;
    if (options.animation) {
      std::ostringstream number;
      number << std::setfill('0') << std::setw(4) << frame;
      destination.replace(position, token.size(), number.str());
    }
    require(
        destinations
            .insert(
                std::filesystem::path(destination).lexically_normal().string())
            .second,
        "Duplicate selected output destination: " + destination);
    resolved.push_back({frame, destination});
  }
  return resolved;
}

void preflightDestinations(
    const std::vector<vsr::app::OfflineRenderFrame> &frames, bool overwrite)
{
  namespace fs = std::filesystem;
  std::set<fs::path> destinations;
  std::vector<fs::path> hardlinked;
  for (const auto &frame : frames) {
    const fs::path path(frame.destination);
    std::error_code ec;
    const auto linkStatus = fs::symlink_status(path, ec);
    require(!ec || ec == std::errc::no_such_file_or_directory,
        "Cannot inspect output destination '" + frame.destination
            + "': " + ec.message());
    if (fs::exists(linkStatus)) {
      // Follow links for regular-file checks, but do not mistake a dangling
      // link for an unused destination. Special files must never be opened.
      require(fs::is_regular_file(path),
          "Output destination is not a regular file: " + frame.destination);
      require(overwrite,
          "Output already exists: " + frame.destination
              + "; use --overwrite to replace selected files");
      if (fs::hard_link_count(path) > 1) {
        for (const auto &previous : hardlinked)
          require(!fs::equivalent(previous, path),
              "Duplicate selected output destination: " + frame.destination);
        hardlinked.push_back(path);
      }
    }
    const auto identity = fs::weakly_canonical(fs::absolute(path));
    require(destinations.insert(identity).second,
        "Duplicate selected output destination: " + frame.destination);
  }
  // Check the whole selection first: refusal must not write a nonexisting
  // subset. Directory creation is fallible even for a valid formatted name.
  for (const auto &frame : frames) {
    const auto parent = fs::path(frame.destination).parent_path();
    if (parent.empty())
      continue;
    std::error_code ec;
    fs::create_directories(parent, ec);
    require(!ec,
        "Cannot create destination directory for '" + frame.destination
            + "': " + ec.message());
  }
}

size_t selectCamera(vsr::scene::Scene &scene,
    const std::optional<std::string> &selection,
    std::optional<size_t> indexSelection,
    std::optional<size_t> savedSelection = {})
{
  std::vector<const vsr::scene::Camera *> cameras;
  std::string available;
  vsr::core::foreach_item_const(scene.objectDB().camera, [&](auto *camera) {
    if (camera) {
      cameras.push_back(camera);
      available += "\n  " + std::to_string(camera->index()) + ": '"
          + camera->name() + "' (" + camera->subtype().str() + ")";
    }
  });
  if (indexSelection) {
    const auto camera = scene.getObject<vsr::scene::Camera>(*indexSelection);
    require(bool(camera),
        "Camera index not found: " + std::to_string(*indexSelection)
            + available);
    return camera->index();
  }
  if (selection) {
    const vsr::scene::Camera *match = nullptr;
    for (auto *camera : cameras) {
      if (camera->name() == *selection) {
        require(!match, "Ambiguous Camera name: " + *selection + available);
        match = camera;
      }
    }
    require(match, "Camera name not found: " + *selection + available);
    return match->index();
  }
  if (savedSelection && scene.getObject<vsr::scene::Camera>(*savedSelection))
    return *savedSelection;
  require(!cameras.empty(),
      "No scene Camera: provide an authored Camera or use --default-camera. "
      "Saved poses are not Cameras.");
  require(cameras.size() == 1,
      "Multiple scene Cameras; select one with --camera or --camera-index. "
      "Available Cameras:"
          + available);
  return cameras.front()->index();
}

vsr::io::ImporterType importerType(const Input &input)
{
  using T = vsr::io::ImporterType;
  const std::pair<const char *, T> names[] = {{"agx", T::AGX},
      {"assimp", T::ASSIMP},
      {"assimp-flat", T::ASSIMP_FLAT},
      {"axyz", T::AXYZ},
      {"dlaf", T::DLAF},
      {"e57xyz", T::E57XYZ},
      {"ensight", T::ENSIGHT},
      {"gltf", T::GLTF},
      {"hdri", T::HDRI},
      {"hsmesh", T::HSMESH},
      {"nbody", T::NBODY},
      {"obj", T::OBJ},
      {"pdb", T::PDB},
      {"pbrt", T::PBRT},
      {"ply", T::PLY},
      {"pointsbin", T::POINTSBIN_MULTIFILE},
      {"pt", T::PT},
      {"silo", T::SILO},
      {"smesh", T::SMESH},
      {"smesh-animation", T::SMESH_ANIMATION},
      {"swc", T::SWC},
      {"swc-sdf", T::SWC_SDF},
      {"trk", T::TRK},
      {"usd", T::USD},
      {"usd-mtlx", T::USD_MTLX},
      {"vtp", T::VTP},
      {"vtu", T::VTU},
      {"xyzdp", T::XYZDP},
      {"volume", T::VOLUME},
      {"volume-animation", T::VOLUME_ANIMATION},
      {"xf", T::XF}};
  std::string name = input.importer;
  if (name.empty()) {
    name = std::filesystem::path(input.path).extension().string();
    if (!name.empty())
      name.erase(0, 1);
    if (name == "glb")
      name = "gltf";
    else if (name == "usda" || name == "usdc" || name == "usdz")
      name = "usd";
    else if (name == "raw" || name == "mhd" || name == "nvdb" || name == "vdb"
        || name == "vti" || name == "flash" || name == "hdf5")
      name = "volume";
    else if (name == "hdr" || name == "exr")
      name = "hdri";
    else if (name == "sil")
      name = "silo";
  }
  for (const auto &[key, type] : names)
    if (name == key)
      return type;
  throw std::runtime_error("Unsupported importer '" + name + "' for "
      + input.path + "; select a supported --importer (see --help)");
}

size_t importedContentCount(const vsr::scene::Scene &scene)
{
  const auto &db = scene.objectDB();
  return db.surface.size() + db.volume.size() + db.light.size()
      + db.camera.size();
}

struct ImportDiagnostics
{
  ImportDiagnostics();
  ~ImportDiagnostics();
  std::string failure;
};

ImportDiagnostics::ImportDiagnostics()
{
  // Importers predate result-returning APIs. Capture their public diagnostics
  // at this executable boundary rather than redesigning each importer.
  vsr::core::setLoggingCallback([this](auto level, std::string message) {
    if (level != vsr::core::DEBUG)
      std::fputs(message.c_str(), stderr);
    if (level == vsr::core::ERROR)
      failure += message;
  });
}

ImportDiagnostics::~ImportDiagnostics()
{
  vsr::core::setLogToStderr();
}

void prepareImports(vsr::app::Context &ctx, const std::vector<Input> &inputs)
{
  using T = vsr::io::ImporterType;
  vsr::core::TransferFunction tf;
  for (size_t i = 0; i < inputs.size();) {
    const auto &input = inputs[i];
    require(input.path.find(';') == std::string::npos,
        "Foreign input path cannot contain the import delimiter ';': "
            + input.path);
    const auto type = importerType(input);
    const auto before = importedContentCount(ctx.vsr.scene);
    const auto animationsBefore = ctx.vsr.animationMgr.animations().size();
    ImportDiagnostics diagnostics;
    std::printf(
        "import=%s layer=%s\n", input.path.c_str(), input.layer.c_str());
    const auto root = ctx.vsr.scene.addLayer(input.layer.c_str())->root();
    if (type == T::POINTSBIN_MULTIFILE || type == T::VOLUME_ANIMATION) {
      std::vector<std::string> files;
      const auto sequence = input.sequence;
      const auto layer = input.layer;
      do {
        auto path = inputs[i].path;
        require(path.find(';') == std::string::npos,
            "Foreign input path cannot contain the import delimiter ';': "
                + path);
        if (type == T::VOLUME_ANIMATION && !inputs[i].property.empty())
          path += ';' + inputs[i].property;
        files.push_back(std::move(path));
        ++i;
      } while (i < inputs.size() && inputs[i].sequence == sequence
          && inputs[i].layer == layer && importerType(inputs[i]) == type);
      // Use the default-transfer-function overload when no xf was supplied.
      if (tf.colorPoints.empty() && tf.opacityPoints.empty())
        vsr::io::import_animations(
            ctx.vsr.scene, ctx.vsr.animationMgr, {{type, files}}, root);
      else
        vsr::io::import_animations(
            ctx.vsr.scene, ctx.vsr.animationMgr, {{type, files}}, tf, root);
    } else if (type == T::PBRT) {
      // PBRT supplies a default Camera even without a Camera directive. That
      // is useful to generic callers, but not an authored CLI viewpoint.
      vsr::io::import_PBRT(
          ctx.vsr.scene, ctx.vsr.animationMgr, input.path.c_str(), root, false);
      ++i;
    } else {
      auto file = input.path + ';' + input.layer;
      if (type == T::VTU && !input.property.empty())
        file += ';' + input.property;
      vsr::io::import_file(
          ctx.vsr.scene, ctx.vsr.animationMgr, {type, file}, tf, root);
      ++i;
    }
    require(diagnostics.failure.empty(),
        "Import failed for " + input.path + ": " + diagnostics.failure);
    if (type == T::XF)
      require(!tf.colorPoints.empty() || !tf.opacityPoints.empty(),
          "Transfer function import produced no points: " + input.path);
    else
      require(importedContentCount(ctx.vsr.scene) > before
              || ctx.vsr.animationMgr.animations().size() > animationsBefore,
          "Import produced no scene content: " + input.path);
  }
}

void prepareLights(vsr::scene::Scene &scene, const Options &options)
{
  if (options.defaultLights)
    vsr::io::generate_default_lights(scene);
  for (const auto &request : options.directionalLights) {
    auto light = scene.createObject<vsr::scene::Light>(
        vsr::scene::tokens::light::directional);
    light->setParameter("direction", request.direction);
    light->setParameter("color", request.color);
    light->setParameter("irradiance", request.irradiance);
    scene.insertChildObjectNode(scene.defaultLayer()->root(), light);
  }
}

void logPreparedScene(const vsr::app::Context &ctx)
{
  std::printf(
      "prepared surfaces=%zu volumes=%zu lights=%zu cameras=%zu animations=%zu\n",
      ctx.vsr.scene.objectDB().surface.size(),
      ctx.vsr.scene.objectDB().volume.size(),
      ctx.vsr.scene.objectDB().light.size(),
      ctx.vsr.scene.objectDB().camera.size(),
      ctx.vsr.animationMgr.animations().size());
}

int render(
    const Options &options, const vsr::render_cli::RenderRuntime &runtime)
{
  std::optional<vsr::app::Context> context;
  vsr::app::OfflineRenderJob job;
  std::optional<vsr::scene::Object> renderer;
  runtime.runPhase("Job preparation", [&] {
    context.emplace();
    auto &ctx = *context;
    require(!options.inputs.empty(), "Scene input is required (see --help)");
    vsr::core::DataTree tree;
    std::optional<std::string> native;
    std::vector<Input> imports;
    for (const auto &input : options.inputs) {
      require(std::filesystem::is_regular_file(input.path),
          "Input file not found: " + input.path);
      vsr::core::DataTree candidate;
      if (candidate.load(input.path.c_str())) {
        require(!native, "Multiple native bases are not supported");
        native = input.path;
      } else {
        imports.push_back(input);
      }
    }
    SavedState saved;
    // Scene construction seeds a convenience Camera; foreign-only jobs must not
    // acquire that unauthored viewpoint. Native reconstruction replaces state.
    ctx.vsr.scene.removeAllObjects();
    if (native) {
      require(tree.load(native->c_str()),
          "Cannot load native Data Tree: " + *native);
      const auto evidence = applicationEvidence(tree.root());
      if (!evidence.empty()) {
        saved = validateDump(tree.root());
        // Legacy scene validation traverses object references in the whole
        // payload. Offline renderer identities belong to application settings,
        // not the scene registry. They are already resolved above; keep them
        // out of scene loading.
        if (!tree.root().child("archives")) {
          auto *context = tree.root().child("context");
          auto &payload = context ? *context : tree.root();
          for (const auto *name : {"offlineRendering",
                   "ANARIDeviceManager",
                   "settings",
                   "cameraPoses"})
            payload.remove(name);
        }
        require(vsr::app::deserialize_ApplicationDump(ctx, tree.root()),
            "Invalid Application Dump at root/" + evidence);
        job.width = saved.config.frame.width;
        job.height = saved.config.frame.height;
        job.samples = saved.config.frame.samples;
        job.aovType = saved.config.aov.aovType;
        job.depthMin = saved.config.aov.depthMin;
        job.depthMax = saved.config.aov.depthMax;
        job.edgeInvert = saved.config.aov.edgeInvert;
      } else {
        const auto validation = vsr::io::validate_SceneArchive(tree.root());
        require(validation.accepted(),
            "Invalid Scene Archive: " + validation.message);
        require(vsr::io::deserialize_SceneArchive(ctx.vsr.scene, tree.root()),
            "Cannot reconstruct Scene Archive: " + *native);
      }
    }
    prepareImports(ctx, imports);
    prepareLights(ctx.vsr.scene, options);
    logPreparedScene(ctx);
    job.width = options.width.value_or(job.width);
    job.height = options.height.value_or(job.height);
    job.samples = options.samples.value_or(job.samples);
    validateQuality(job.width, job.height, job.samples);
    job.aovType = options.aov.value_or(job.aovType);
    job.depthMin = options.depthMin.value_or(job.depthMin);
    job.depthMax = options.depthMax.value_or(job.depthMax);
    job.edgeInvert = options.edgeInvert.value_or(job.edgeInvert);
    require(vsr::rendering::isValidDepthRange(job.depthMin, job.depthMax),
        "Invalid AOV depth range: require finite increasing bounds and span");
    const auto frames = selectFrames(options, saved, ctx.vsr.animationMgr);
    job.frames = resolveDestinations(options, saved, frames);

    job.generateCamera = options.defaultCamera;
    job.globalFirstSelectedFrame = frames.front();
    if (!job.generateCamera)
      job.cameraIndex = selectCamera(ctx.vsr.scene,
          options.camera,
          options.cameraIndex,
          saved.cameraIndex
              ? std::optional<size_t>(saved.config.camera.cameraIndex)
              : std::nullopt);
    job.deviceIdentifier = vsr::render_cli::resolveDevice(
        options.device, saved.config.renderer.libraryName);
    const auto *savedRenderer = saved.config.renderer.activeRenderer >= 0
        ? &saved.config.renderer.rendererObjects.at(
              saved.config.renderer.activeRenderer)
        : nullptr;
    renderer = vsr::render_cli::resolveRenderer(job.deviceIdentifier,
        saved.config.renderer.libraryName,
        savedRenderer,
        options.renderer,
        options.rendererParameters);
    job.renderer = &*renderer;
    job.cameraParameters.insert(job.cameraParameters.end(),
        options.cameraParameters.begin(),
        options.cameraParameters.end());
    job.cameraLookAt = options.lookAt;
  });
  auto &ctx = *context;
  // Rank 0 prepares the shared filesystem only after all ranks have resolved
  // the complete global mapping. No rank can write before this phase succeeds.
  runtime.runPhase("Output preflight", [&] {
    if (runtime.rank() == 0)
      preflightDestinations(job.frames, options.overwrite);
  });
  const auto &destination = job.frames.front().destination;
  auto camera = ctx.vsr.scene.getObject<vsr::scene::Camera>(job.cameraIndex);
  const auto cameraSelection =
      job.generateCamera ? "generated" : std::to_string(job.cameraIndex);
  std::printf(
      "device=%s renderer=%s camera=%s '%s' (%s) frame=%d "
      "dimensions=%ux%u samples=%u destination=%s mode=%s selected=%zu last=%d "
      "aov=%s depth=%g:%g edge-invert=%s\n",
      job.deviceIdentifier.c_str(),
      job.renderer->subtype().c_str(),
      cameraSelection.c_str(),
      camera ? camera->name().c_str() : "generated default Camera",
      camera ? camera->subtype().c_str() : "perspective",
      job.globalFirstSelectedFrame,
      job.width,
      job.height,
      job.samples,
      destination.c_str(),
      options.animation ? "animation" : "still",
      job.frames.size(),
      job.frames.back().frameIndex,
      aovName(job.aovType),
      job.depthMin,
      job.depthMax,
      job.edgeInvert ? "true" : "false");
  std::fflush(stdout);
  const size_t globalCount = job.frames.size();
  runtime.runPhase("Rendering", [&] {
    std::vector<vsr::app::OfflineRenderFrame> localFrames;
    for (size_t i = 0; i < globalCount; ++i) {
      if (i % runtime.size() == size_t(runtime.rank()))
        localFrames.push_back(job.frames[i]);
    }
    job.frames = std::move(localFrames);
    std::printf("rank=%d/%d assigned=%zu global=%zu first=%d\n",
        runtime.rank(),
        runtime.size(),
        job.frames.size(),
        globalCount,
        job.globalFirstSelectedFrame);
    std::fflush(stdout);
    if (job.frames.empty())
      return;
    const auto result = vsr::app::renderOfflineJob(
        ctx, job, [&](const vsr::app::OfflineRenderProgress &progress) {
          std::printf("rank=%d frame=%d local=%zu/%zu\n",
              runtime.rank(),
              progress.frameIndex,
              progress.position + 1,
              progress.totalFrames);
          std::fflush(stdout);
          return true;
        });
    require(result.status == vsr::app::OfflineRenderStatus::Completed
            && result.completedFrames == job.frames.size(),
        "Render failed for " + result.destination + ": " + result.diagnostic);
  });
  if (runtime.rank() == 0)
    std::printf(
        "Completed global frames=%zu ranks=%d\n", globalCount, runtime.size());
  return 0;
}

} // namespace

int main(int argc, char **argv)
{
  vsr::render_cli::RenderRuntime runtime(argc, argv);
  try {
    vsr::core::setLogToStderr();
    Options options;
    runtime.runPhase("Arguments", [&] { options = parseOptions(argc, argv); });
    if (options.help) {
      if (runtime.rank() == 0)
        printHelp();
      return 0;
    }
    return render(options, runtime);
  } catch (const std::bad_alloc &) {
    // This also covers allocation while forming a phase callback, before its
    // collective boundary is entered. Continuing could strand other ranks.
    runtime.abort("Resource exhaustion");
  } catch (const std::exception &error) {
    if (runtime.rank() == 0)
      std::fprintf(stderr, "vsrRender: %s\n", error.what());
    return 1;
  }
}

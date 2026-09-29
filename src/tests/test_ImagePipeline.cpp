// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// std
#include <cstring>
#include <string>
#include <vector>

namespace rendering = vsr::rendering;

namespace {

using Log = std::vector<std::string>;

struct FakeSource : public rendering::ImageSource
{
  FakeSource(Log *log, uint32_t value = 1u) : m_log(log), m_value(value) {}
  const char *name() const override
  {
    return "Fake Source";
  }

 private:
  void render(rendering::ImageBuffers &b) override
  {
    m_log->push_back("source");
    const auto size = dimensions();
    for (size_t i = 0; i < size_t(size.x) * size_t(size.y); i++)
      b.color[i] = m_value;
  }

  Log *m_log{nullptr};
  uint32_t m_value{1u};
};

struct AddPass : public rendering::ImagePass
{
  AddPass(Log *log, std::string tag) : m_log(log), m_tag(std::move(tag)) {}
  const char *name() const override
  {
    return m_tag.c_str();
  }

 private:
  void render(rendering::ImageBuffers &b, rendering::FrameState &) override
  {
    m_log->push_back(m_tag);
    const auto size = dimensions();
    for (size_t i = 0; i < size_t(size.x) * size_t(size.y); i++)
      b.color[i] += 10u;
  }

  Log *m_log{nullptr};
  std::string m_tag;
};

struct CaptureSink : public rendering::ImageSink
{
  CaptureSink(Log *log) : m_log(log) {}
  const char *name() const override
  {
    return "Capture";
  }

  std::vector<uint32_t> captured;

 private:
  void render(const rendering::ImageBuffers &b) override
  {
    m_log->push_back("sink");
    const auto size = dimensions();
    captured.assign(b.color, b.color + size_t(size.x) * size_t(size.y));
  }

  Log *m_log{nullptr};
};

} // namespace

TEST_CASE(
    "Image Pipeline runs source, then passes, then sinks", "[ImagePipeline]")
{
  Log log;
  rendering::ImagePipeline pipeline(2, 2);

  // Added out of role order on purpose: role decides execution order.
  auto *sink = pipeline.addSink<CaptureSink>(&log);
  pipeline.addPass<AddPass>(&log, "passA");
  pipeline.setSource<FakeSource>(&log);
  pipeline.addPass<AddPass>(&log, "passB");

  pipeline.render();

  REQUIRE(log == Log{"source", "passA", "passB", "sink"});
  REQUIRE(sink->captured == std::vector<uint32_t>(4, 21u));
  REQUIRE(pipeline.getColorBuffer()[0] == 21u);
}

TEST_CASE(
    "Image Pipeline without a source produces no frame", "[ImagePipeline]")
{
  Log log;
  rendering::ImagePipeline pipeline(2, 2);
  pipeline.addPass<AddPass>(&log, "pass");
  pipeline.addSink<CaptureSink>(&log);

  pipeline.render();

  REQUIRE(log.empty());
  REQUIRE(pipeline.getPassTimings().empty());
}

TEST_CASE("Image Pipeline skips disabled stages", "[ImagePipeline]")
{
  Log log;
  rendering::ImagePipeline pipeline(1, 1);
  auto *source = pipeline.setSource<FakeSource>(&log);
  auto *pass = pipeline.addPass<AddPass>(&log, "pass");
  auto *sink = pipeline.addSink<CaptureSink>(&log);

  SECTION("disabled pass and sink")
  {
    pass->setEnabled(false);
    sink->setEnabled(false);
    pipeline.render();
    REQUIRE(log == Log{"source"});
  }

  SECTION("disabled source skips the whole frame, keeping the last image")
  {
    pipeline.render();
    log.clear();
    source->setEnabled(false);
    pipeline.render();
    REQUIRE(log.empty());
    REQUIRE(pipeline.getColorBuffer()[0] == 11u);
    REQUIRE(sink->captured == std::vector<uint32_t>(1, 11u));
  }
}

TEST_CASE(
    "Image Pipeline setSource replaces the previous source", "[ImagePipeline]")
{
  Log log;
  rendering::ImagePipeline pipeline(1, 1);
  pipeline.setSource<FakeSource>(&log, 1u);
  auto *second = pipeline.setSource<FakeSource>(&log, 5u);

  REQUIRE(pipeline.source() == second);
  pipeline.render();

  REQUIRE(log == Log{"source"});
  REQUIRE(pipeline.getColorBuffer()[0] == 5u);
}

TEST_CASE("Image Pipeline sizes every stage", "[ImagePipeline]")
{
  Log log;
  rendering::ImagePipeline pipeline;
  auto *early = pipeline.addPass<AddPass>(&log, "early");
  pipeline.setDimensions(3, 2);
  auto *late = pipeline.addSink<CaptureSink>(&log);
  auto *source = pipeline.setSource<FakeSource>(&log);

  REQUIRE(early->dimensions() == vsr::math::uint2(3, 2));
  REQUIRE(late->dimensions() == vsr::math::uint2(3, 2));
  REQUIRE(source->dimensions() == vsr::math::uint2(3, 2));

  pipeline.render();
  REQUIRE(late->captured.size() == 6);
}

TEST_CASE(
    "Image Pipeline reports per-stage timings with roles", "[ImagePipeline]")
{
  Log log;
  rendering::ImagePipeline pipeline(1, 1);
  pipeline.setSource<FakeSource>(&log);
  pipeline.addPass<AddPass>(&log, "pass");
  pipeline.addSink<CaptureSink>(&log);

  pipeline.render();

  const auto &timings = pipeline.getPassTimings();
  REQUIRE(timings.size() == 3);
  REQUIRE(std::string(timings[0].name) == "Fake Source");
  REQUIRE(timings[0].role == rendering::ImageStageRole::SOURCE);
  REQUIRE(std::string(timings[1].name) == "pass");
  REQUIRE(timings[1].role == rendering::ImageStageRole::PASS);
  REQUIRE(std::string(timings[2].name) == "Capture");
  REQUIRE(timings[2].role == rendering::ImageStageRole::SINK);
}

TEST_CASE("Image Pipeline clear removes every stage", "[ImagePipeline]")
{
  Log log;
  rendering::ImagePipeline pipeline(1, 1);
  pipeline.setSource<FakeSource>(&log);
  pipeline.addPass<AddPass>(&log, "pass");
  pipeline.addSink<CaptureSink>(&log);
  REQUIRE(!pipeline.empty());

  pipeline.clear();

  REQUIRE(pipeline.empty());
  REQUIRE(pipeline.source() == nullptr);
  pipeline.render();
  REQUIRE(log.empty());
}

TEST_CASE("External Frame Source copies a matching frame or falls back",
    "[ImagePipeline]")
{
  rendering::ImagePipeline pipeline(2, 1);
  auto *source = pipeline.setSource<rendering::ExternalFrameSource>();
  source->setFallbackColor(vsr::math::float4(1.f, 0.f, 0.f, 1.f));
  const uint8_t redBytes[4] = {255, 0, 0, 255};
  uint32_t red = 0;
  std::memcpy(&red, redBytes, sizeof(red));

  SECTION("no frame yet: fallback color")
  {
    pipeline.render();
    REQUIRE(pipeline.getColorBuffer()[0] == red);
    REQUIRE(pipeline.getColorBuffer()[1] == red);
  }

  SECTION("mismatched frame: fallback color")
  {
    std::vector<uint8_t> frame(4 * 3, 7);
    source->setFrame(&frame);
    pipeline.render();
    REQUIRE(pipeline.getColorBuffer()[0] == red);
  }

  SECTION("matching frame: copied verbatim")
  {
    std::vector<uint8_t> frame = {1, 2, 3, 4, 5, 6, 7, 8};
    source->setFrame(&frame);
    pipeline.render();
    uint32_t expected[2];
    std::memcpy(expected, frame.data(), sizeof(expected));
    REQUIRE(pipeline.getColorBuffer()[0] == expected[0]);
    REQUIRE(pipeline.getColorBuffer()[1] == expected[1]);
  }
}

// Channel demand /////////////////////////////////////////////////////////////

namespace {

using rendering::ImageChannels;

struct ChannelSource : public rendering::ImageSource
{
  ChannelSource(ImageChannels supported) : m_supported(supported) {}
  const char *name() const override
  {
    return "Channel Source";
  }
  ImageChannels supportedChannels() const override
  {
    return m_supported;
  }

  int channelUpdates{0};
  float depthValue{1.f};
  bool writeDepth{true};

 private:
  void updateChannels() override
  {
    channelUpdates++;
  }
  void render(rendering::ImageBuffers &b) override
  {
    const auto size = dimensions();
    const size_t n = size_t(size.x) * size_t(size.y);
    std::fill(b.color, b.color + n, 1u);
    if (b.depth && writeDepth)
      std::fill(b.depth, b.depth + n, depthValue);
  }

  ImageChannels m_supported{ImageChannels::NONE};
};

// Records which buffers it was handed, and the depth it saw.
struct ProbePass : public rendering::ImagePass
{
  ProbePass(ImageChannels required) : required(required) {}
  const char *name() const override
  {
    return "Probe";
  }
  ImageChannels requiredChannels() const override
  {
    return required;
  }

  ImageChannels required{ImageChannels::NONE};
  bool sawColor{false};
  bool sawHdr{false};
  bool sawDepth{false};
  bool sawObjectId{false};
  bool sawPrimitiveId{false};
  bool sawInstanceId{false};
  bool sawAlbedo{false};
  bool sawNormal{false};
  float depth{0.f};

 private:
  void render(rendering::ImageBuffers &b, rendering::FrameState &) override
  {
    sawColor = b.color != nullptr;
    sawHdr = b.hdrColor != nullptr;
    sawDepth = b.depth != nullptr;
    sawObjectId = b.objectId != nullptr;
    sawPrimitiveId = b.primitiveId != nullptr;
    sawInstanceId = b.instanceId != nullptr;
    sawAlbedo = b.albedo != nullptr;
    sawNormal = b.normal != nullptr;
    depth = b.depth ? b.depth[0] : 0.f;
  }
};

constexpr ImageChannels ALL_CHANNELS = ImageChannels::DEPTH
    | ImageChannels::OBJECT_ID | ImageChannels::PRIMITIVE_ID
    | ImageChannels::INSTANCE_ID | ImageChannels::ALBEDO | ImageChannels::NORMAL
    | ImageChannels::HDR_COLOR;

} // namespace

TEST_CASE("Image Pipeline hands the source the channels enabled passes need",
    "[ImagePipeline]")
{
  rendering::ImagePipeline pipeline(1, 1);
  auto *source = pipeline.setSource<ChannelSource>(ALL_CHANNELS);
  pipeline.addPass<ProbePass>(ImageChannels::DEPTH);
  auto *ids = pipeline.addPass<ProbePass>(ImageChannels::OBJECT_ID);
  ids->setEnabled(false);

  pipeline.render();
  REQUIRE(source->channels() == ImageChannels::DEPTH);

  ids->setEnabled(true);
  pipeline.render();
  REQUIRE(
      source->channels() == (ImageChannels::DEPTH | ImageChannels::OBJECT_ID));
}

TEST_CASE("Image Pipeline channel demand follows pass state", "[ImagePipeline]")
{
  rendering::ImagePipeline pipeline(1, 1);
  auto *source = pipeline.setSource<ChannelSource>(ALL_CHANNELS);
  auto *probe = pipeline.addPass<ProbePass>(ImageChannels::NONE);

  pipeline.render();
  REQUIRE(source->channels() == ImageChannels::NONE);
  const int updates = source->channelUpdates;

  probe->required = ImageChannels::NORMAL;
  pipeline.render();
  REQUIRE(source->channels() == ImageChannels::NORMAL);
  REQUIRE(source->channelUpdates == updates + 1);

  pipeline.render();
  REQUIRE(source->channelUpdates == updates + 1);
}

TEST_CASE("Image Pipeline clips demand to what the source supports",
    "[ImagePipeline]")
{
  rendering::ImagePipeline pipeline(1, 1);
  auto *source = pipeline.setSource<ChannelSource>(ImageChannels::DEPTH);
  auto *probe =
      pipeline.addPass<ProbePass>(ImageChannels::DEPTH | ImageChannels::NORMAL);

  pipeline.render();

  REQUIRE(source->channels() == ImageChannels::DEPTH);
  REQUIRE(probe->sawDepth);
  REQUIRE(!probe->sawNormal);
}

TEST_CASE("Image Pipeline allocates only requested channels", "[ImagePipeline]")
{
  rendering::ImagePipeline pipeline(1, 1);
  pipeline.setSource<ChannelSource>(ALL_CHANNELS);
  auto *probe = pipeline.addPass<ProbePass>(ImageChannels::NONE);

  SECTION("nothing requested: color only")
  {
    pipeline.render();
    REQUIRE(probe->sawColor);
    REQUIRE(!probe->sawHdr);
    REQUIRE(!probe->sawDepth);
    REQUIRE(!probe->sawObjectId);
    REQUIRE(!probe->sawPrimitiveId);
    REQUIRE(!probe->sawInstanceId);
    REQUIRE(!probe->sawAlbedo);
    REQUIRE(!probe->sawNormal);
  }

  SECTION("everything requested")
  {
    probe->required = ALL_CHANNELS;
    pipeline.render();
    REQUIRE(probe->sawHdr);
    REQUIRE(probe->sawDepth);
    REQUIRE(probe->sawObjectId);
    REQUIRE(probe->sawPrimitiveId);
    REQUIRE(probe->sawInstanceId);
    REQUIRE(probe->sawAlbedo);
    REQUIRE(probe->sawNormal);
  }
}

TEST_CASE("Image Pipeline keeps a channel's buffer while it stays requested",
    "[ImagePipeline]")
{
  rendering::ImagePipeline pipeline(1, 1);
  auto *source = pipeline.setSource<ChannelSource>(ALL_CHANNELS);
  auto *probe = pipeline.addPass<ProbePass>(ImageChannels::DEPTH);

  source->depthValue = 7.f;
  pipeline.render();
  REQUIRE(probe->depth == 7.f);

  // The source stops writing depth; the buffer must still hold the last
  // value, even when an unrelated channel is added.
  source->writeDepth = false;
  probe->required = ImageChannels::DEPTH | ImageChannels::NORMAL;
  pipeline.render();
  REQUIRE(probe->depth == 7.f);
}

TEST_CASE("Image Pipeline resets frame state every frame", "[ImagePipeline]")
{
  struct WriterPass : public rendering::ImagePass
  {
    void render(rendering::ImageBuffers &, rendering::FrameState &f) override
    {
      f.exposure = 3.f;
    }
  };
  struct ReaderPass : public rendering::ImagePass
  {
    float seen{-1.f};
    void render(rendering::ImageBuffers &, rendering::FrameState &f) override
    {
      seen = f.exposure;
    }
  };

  rendering::ImagePipeline pipeline(1, 1);
  pipeline.setSource<ChannelSource>(ImageChannels::NONE);
  auto *writer = pipeline.addPass<WriterPass>();
  auto *reader = pipeline.addPass<ReaderPass>();

  pipeline.render();
  REQUIRE(reader->seen == 3.f);

  writer->setEnabled(false);
  pipeline.render();
  REQUIRE(reader->seen == 0.f);
}

TEST_CASE(
    "External Frame Source produces no auxiliary channels", "[ImagePipeline]")
{
  rendering::ImagePipeline pipeline(1, 1);
  pipeline.setSource<rendering::ExternalFrameSource>();
  auto *probe = pipeline.addPass<ProbePass>(ALL_CHANNELS);

  pipeline.render();

  REQUIRE(probe->sawColor);
  REQUIRE(!probe->sawDepth);
  REQUIRE(!probe->sawObjectId);
}

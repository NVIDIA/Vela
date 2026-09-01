// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StudioViewport.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// vsr_rendering
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Renderer.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// imgui
#include <imgui.h>

namespace vsr::scivis_studio::client {

using namespace std::chrono_literals;

namespace {

constexpr vsr::math::float4 CLEAR_WITH_FRAME{0.f, 0.f, 0.f, 1.f};
constexpr vsr::math::float4 CLEAR_WITHOUT_FRAME{0.12f, 0.12f, 0.12f, 1.f};

const char *hintFor(ConnectionState state)
{
  switch (state) {
  case ConnectionState::Connected:
    return "Connected -- waiting for the first frame";
  case ConnectionState::Lost:
    return "Server connection lost";
  case ConnectionState::NeverConnected:
  case ConnectionState::Disconnected:
    break;
  }
  return "Not connected -- Client > Connect";
}

} // namespace

// Construction ///////////////////////////////////////////////////////////////

StudioViewport::StudioViewport(vsr::ui::imgui::Application *app,
    vsr::rendering::Manipulator *manipulator,
    ServerConnection *connection,
    const char *name)
    : BaseViewport(app, name), m_connection(connection)
{
  setManipulator(manipulator);
  BaseViewport::imagePipeline_setup();
  reset();
}

StudioViewport::~StudioViewport()
{
  BaseViewport::imagePipeline_teardown();
}

// UI /////////////////////////////////////////////////////////////////////////

void StudioViewport::buildUI()
{
  const auto state =
      m_connection ? m_connection->state() : ConnectionState::NeverConnected;
  const bool connected = state == ConnectionState::Connected;
  BaseViewport::viewport_setActive(connected);

  if (state != m_shownState) {
    if (state == ConnectionState::Disconnected
        || state == ConnectionState::NeverConnected)
      reset();
    m_shownState = state;
  }

  BaseViewport::buildUI(); // settled resizes -> viewport_reshape()

  takeLatestFrame();
  const auto now = Clock::now();
  while (!m_frameArrivals.empty() && now - m_frameArrivals.front() > 1s)
    m_frameArrivals.pop_front();

  syncPipelineToFrame();
  if (connected)
    updateCamera();

  m_framePass->setEnabled(m_hasFrame);
  m_clearPass->setClearColor(
      m_hasFrame ? CLEAR_WITH_FRAME : CLEAR_WITHOUT_FRAME);
  BaseViewport::imagePipeline_render();

  ui_menubar(connected);

  if (BaseViewport::imagePipeline_isSetup()) {
    ImGui::Image((ImTextureID)m_outputPass->getTexture(),
        ImGui::GetContentRegionAvail(),
        ImVec2(0, 1),
        ImVec2(1, 0));
  }

  if (!m_hasFrame)
    ui_notConnectedHint();

  // Only a connected viewport with a camera to drive takes input; while Lost
  // the frozen frame must not drift from the mirror camera.
  if (BaseViewport::viewport_isActive() && m_camera.current) {
    const bool widgetActive = BaseViewport::ui_orientationWidget();
    if (!widgetActive)
      BaseViewport::ui_handleInput();
  }

  if (m_showOverlay)
    ui_overlay();
}

void StudioViewport::ui_menubar(bool connected)
{
  if (!ImGui::BeginMenuBar())
    return;

  if (ImGui::BeginMenu("Viewport")) {
    ImGui::Checkbox("show info overlay", &m_showOverlay);
    ImGui::Checkbox("show orientation widget", &m_showOrientationWidget);
    ImGui::EndMenu();
  }

  ImGui::BeginDisabled(!connected);
  if (!m_renderers.objects.empty())
    BaseViewport::ui_menubar_Renderer();
  if (m_camera.current)
    BaseViewport::ui_menubar_Camera();
  ImGui::EndDisabled();

  ImGui::EndMenuBar();
}

void StudioViewport::ui_notConnectedHint()
{
  const char *hint = hintFor(m_shownState);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const ImVec2 textSize = ImGui::CalcTextSize(hint);
  const ImVec2 at(
      (min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f);
  ImGui::GetWindowDrawList()->AddText(
      at, ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
}

void StudioViewport::ui_overlay()
{
  const ImVec2 contentStart = ImGui::GetCursorStartPos();
  ImGui::SetCursorPos(ImVec2(contentStart.x + 2.f, contentStart.y + 2.f));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.7f));

  const ImGuiChildFlags childFlags = ImGuiChildFlags_Border
      | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY;
  const ImGuiWindowFlags windowFlags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

  // A child window so other windows occlude it correctly.
  if (ImGui::BeginChild(
          "##viewportOverlay", ImVec2(0, 0), childFlags, windowFlags)) {
    ImGui::Text("connection: %s", toString(m_shownState));
    if (m_connection && !m_connection->statusText().empty())
      ImGui::TextUnformatted(m_connection->statusText().c_str());
    ImGui::Text("viewport: %i x %i", m_viewport.size.x, m_viewport.size.y);
    if (m_hasFrame) {
      // Everything below comes from the last frame's header, nothing else.
      ImGui::Text("last frame: %u x %u  %s  %s",
          m_lastHeader.width,
          m_lastHeader.height,
          protocol::toString(m_lastHeader.encoding),
          protocol::toString(m_lastHeader.pixelFormat));
      if (!m_lastHeader.shotId.empty()) {
        ImGui::Text("shot: %s  frame %d",
            m_lastHeader.shotId.c_str(),
            m_lastHeader.frame);
      }
      ImGui::Text("received: %.0f fps", receivedFps());
    } else {
      ImGui::TextUnformatted("no frame received");
    }
  }
  ImGui::EndChild();

  ImGui::PopStyleColor();
}

// Connection-driven state ////////////////////////////////////////////////////

void StudioViewport::sendFrameConfig()
{
  if (!m_connection || m_viewport.size.x <= 0 || m_viewport.size.y <= 0)
    return;
  m_sentFrameConfig = vsr::math::uint2(m_viewport.size.x, m_viewport.size.y);
  m_connection->setFrameConfig(m_sentFrameConfig.x, m_sentFrameConfig.y);
}

void StudioViewport::adoptCamera(vsr::scene::CameraAppRef camera)
{
  camera_setCurrent(camera);
  m_camera.arcballToken = {};
  m_manipulatorSynchronized = false;
}

void StudioViewport::adoptRenderer(size_t rendererIndex)
{
  auto &scene = appContext()->vsr.scene;
  m_renderers.objects.clear();
  m_renderers.current = {};
  const size_t count = scene.numberOfObjects(ANARI_RENDERER);
  for (size_t i = 0; i < count; ++i) {
    auto renderer = scene.getObject<vsr::scene::Renderer>(i);
    if (!renderer)
      continue;
    m_renderers.objects.push_back(renderer);
    if (i == rendererIndex)
      m_renderers.current = renderer;
  }
  if (!m_renderers.current && !m_renderers.objects.empty())
    m_renderers.current = m_renderers.objects.front();
}

void StudioViewport::reset()
{
  m_camera.current = {};
  m_camera.arcballToken = {};
  m_manipulatorSynchronized = false;
  m_renderers.objects.clear();
  m_renderers.current = {};
  m_hasFrame = false;
  m_lastHeader = {};
  m_sentFrameConfig = vsr::math::uint2(0, 0);
  m_frameArrivals.clear();
}

// Frames /////////////////////////////////////////////////////////////////////

void StudioViewport::takeLatestFrame()
{
  if (!m_connection)
    return;

  vsr::network::Message msg;
  if (!m_connection->takeLatestFrame(msg))
    return;

  const auto view = protocol::decodeFrame(msg);
  if (!view) {
    vsr::core::logWarning(
        "[StudioViewport] dropped a Frame message that failed to decode");
    return;
  }
  if (!protocol::decodeFramePixels(*view, m_decodeScratch)) {
    vsr::core::logWarning(
        "[StudioViewport] dropped a %s frame (%u x %u) whose pixels failed"
        " to decode",
        protocol::toString(view->header.encoding),
        view->header.width,
        view->header.height);
    return;
  }

  // The copy pass holds a reference to m_pixels itself, so swapping contents
  // keeps it bound.
  m_pixels.swap(m_decodeScratch);
  m_lastHeader = view->header;
  m_hasFrame = true;
  m_frameArrivals.push_back(Clock::now());
}

void StudioViewport::syncPipelineToFrame()
{
  const auto desired = m_hasFrame
      ? vsr::math::int2(int(m_lastHeader.width), int(m_lastHeader.height))
      : m_viewport.renderSize;
  if (desired.x <= 0 || desired.y <= 0 || desired == m_pipelineSize)
    return;
  BaseViewport::imagePipeline_setDimensions(desired.x, desired.y);
  m_pipelineSize = desired;
}

float StudioViewport::receivedFps() const
{
  return float(m_frameArrivals.size());
}

// Camera /////////////////////////////////////////////////////////////////////

void StudioViewport::updateCamera()
{
  if (!m_camera.current)
    return;

  if (!m_manipulatorSynchronized) {
    // Adopt the server's view rather than imposing ours. Without manipulator
    // metadata there is nothing to adopt; the first interaction then
    // re-frames the camera from the manipulator.
    if (m_camera.current->numMetadata() > 0) {
      vsr::rendering::updateManipulatorFromCamera(
          *m_camera.arcball, *m_camera.current);
    } else {
      vsr::core::logDebug(
          "[StudioViewport] camera carries no manipulator metadata");
    }
    m_manipulatorSynchronized = true;
    m_camera.arcball->hasChanged(m_camera.arcballToken); // consume
    return;
  }

  if (m_camera.arcball->hasChanged(m_camera.arcballToken))
    vsr::rendering::updateCameraObject(*m_camera.current, *m_camera.arcball);
}

// BaseViewport hooks /////////////////////////////////////////////////////////

void StudioViewport::imagePipeline_populate(vsr::rendering::ImagePipeline &p)
{
  m_clearPass = p.emplace_back<vsr::rendering::ClearBuffersPass>();
  m_clearPass->setClearColor(CLEAR_WITHOUT_FRAME);
  m_framePass = p.emplace_back<vsr::rendering::CopyToColorBufferPass>();
  m_framePass->setExternalBuffer(m_pixels);
  m_framePass->setEnabled(false);
  m_outputPass = p.emplace_back<vsr::rendering::CopyToSDLTexturePass>(
      m_app->sdlRenderer());
}

void StudioViewport::viewport_reshape(vsr::math::int2 newWindowSize)
{
  if (newWindowSize.x <= 0 || newWindowSize.y <= 0)
    return;

  BaseViewport::viewport_reshape(newWindowSize);
  m_pipelineSize = m_viewport.renderSize; // what the base just set

  // The base only reshapes on a settled size change, so this is at most one
  // SetFrameConfig per UI frame and none for a size the server already has.
  if (!m_connection || m_connection->state() != ConnectionState::Connected)
    return;
  const auto size = vsr::math::uint2(newWindowSize.x, newWindowSize.y);
  if (size == m_sentFrameConfig)
    return;
  m_sentFrameConfig = size;
  m_connection->setFrameConfig(size.x, size.y);
}

void StudioViewport::camera_resetView(bool /*resetAzEl*/)
{
  vsr::core::logWarning(
      "[StudioViewport] camera view reset is not available in the client yet");
}

void StudioViewport::camera_centerView()
{
  vsr::core::logWarning(
      "[StudioViewport] camera centering is not available in the client yet");
}

void StudioViewport::renderer_clone()
{
  vsr::core::logWarning(
      "[StudioViewport] renderer cloning is not available in the client yet");
}

void StudioViewport::renderer_resetParameterDefaults()
{
  vsr::core::logWarning(
      "[StudioViewport] renderer parameter reset is not available in the"
      " client yet");
}

} // namespace vsr::scivis_studio::client

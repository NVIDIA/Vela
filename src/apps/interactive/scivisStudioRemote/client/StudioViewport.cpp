// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StudioViewport.h"
#include "ProjectOps.h"
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
// std
#include <algorithm>
#include <cmath>

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

bool sameIdentity(const std::optional<SceneObjectRef> &a,
    const std::optional<SceneObjectRef> &b)
{
  if (a.has_value() != b.has_value())
    return false;
  return !a || (a->type == b->type && a->objectIndex == b->objectIndex);
}

constexpr int AOV_TYPE_COUNT = int(vsr::rendering::AOVType::INSTANCE_ID) + 1;

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
    if (connected && m_hasFrame)
      ui_picking();
  }

  if (!m_hasFrame)
    ui_notConnectedHint();

  if (connected)
    syncOutline();

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

  if (ImGui::BeginMenu("View")) {
    ui_menubar_View();
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

// The monolith Viewport's id-driven pass toggles; every change ships the
// whole struct. PRIMITIVE_ID is offered without knowing the server device:
// the server turns it off silently when unsupported.
void StudioViewport::ui_menubar_View()
{
  using vsr::rendering::AOVType;
  auto &s = m_settings;
  bool changed = false;

  ImGui::Text("AOV Visualization:");
  ImGui::Indent(vsr::ui::imgui::INDENT_AMOUNT);
  if (ImGui::BeginCombo("AOV", protocol::toString(s.visualizeAOV))) {
    for (int i = 0; i < AOV_TYPE_COUNT; ++i) {
      const auto type = AOVType(i);
      const bool selected = type == s.visualizeAOV;
      if (ImGui::Selectable(protocol::toString(type), selected) && !selected) {
        s.visualizeAOV = type;
        changed = true;
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::BeginDisabled(s.visualizeAOV != AOVType::DEPTH);
  changed |= ImGui::DragFloat("Depth Minimum",
      &s.depthVisualMinimum,
      0.1f,
      0.f,
      s.depthVisualMaximum);
  changed |= ImGui::DragFloat("Depth Maximum",
      &s.depthVisualMaximum,
      0.1f,
      s.depthVisualMinimum,
      1e20f);
  ImGui::EndDisabled();
  ImGui::BeginDisabled(s.visualizeAOV != AOVType::EDGES);
  changed |= ImGui::Checkbox("Invert Edges", &s.edgeInvert);
  ImGui::EndDisabled();
  ImGui::Unindent(vsr::ui::imgui::INDENT_AMOUNT);

  ImGui::Separator();

  ImGui::Text("Display:");
  ImGui::Indent(vsr::ui::imgui::INDENT_AMOUNT);
  changed |= ImGui::Checkbox("Highlight Selected", &s.highlightSelection);
  changed |= ImGui::Checkbox("Outline Primitives", &s.outlinePrimitives);
  vsr::ui::tooltipForPreviousItem(
      "Needs a server device with primitive ids; silently off otherwise");
  changed |= ImGui::Checkbox("World Bounds", &s.showWorldBounds);
  ImGui::BeginDisabled(!s.showWorldBounds);
  changed |= ImGui::ColorEdit4("Bounds Color",
      &s.worldBoundsColor.x,
      ImGuiColorEditFlags_NoInputs);
  if (ImGui::InputInt("Bounds Width", &s.worldBoundsWidth)) {
    s.worldBoundsWidth = std::max(1, s.worldBoundsWidth);
    changed = true;
  }
  ImGui::EndDisabled();
  ImGui::Unindent(vsr::ui::imgui::INDENT_AMOUNT);

  if (changed)
    sendViewportSettings();
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

// Picking and outline ////////////////////////////////////////////////////////

void StudioViewport::ui_picking()
{
  if (!ImGui::IsItemHovered()
      || !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
      || !m_camera.current || m_lastHeader.width == 0
      || m_lastHeader.height == 0)
    return;

  // Frame-header pixels, y down from the top-left, however the image is
  // scaled on screen.
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const ImVec2 mouse = ImGui::GetMousePos();
  const float w = std::max(max.x - min.x, 1.f);
  const float h = std::max(max.y - min.y, 1.f);
  const int x = std::clamp(int((mouse.x - min.x) / w * m_lastHeader.width),
      0,
      int(m_lastHeader.width) - 1);
  const int y = std::clamp(int((mouse.y - min.y) / h * m_lastHeader.height),
      0,
      int(m_lastHeader.height) - 1);

  // Shift: re-centre the view (perspective only, as the monolith); plain:
  // select what is under the mouse.
  const bool focus = ImGui::IsKeyDown(ImGuiKey_LeftShift)
      || ImGui::IsKeyDown(ImGuiKey_RightShift);
  if (focus
      && m_camera.current->subtype()
          != vsr::scene::tokens::camera::perspective)
    return;
  pick(x, y, !focus);
}

void StudioViewport::pick(int x, int y, bool selectObject)
{
  if (!m_connection || m_connection->state() != ConnectionState::Connected)
    return;
  auto &ops = m_connection->projectOps();
  // One pick in flight: the server keeps only the latest anyway.
  if (m_pendingPick.valid() && ops.pending(m_pendingPick))
    ops.forget(m_pendingPick);
  m_pendingPick = ops.pick(x, y, [this, selectObject](const auto &reply) {
    if (reply)
      onPickReply(*reply, selectObject);
  });
}

void StudioViewport::onPickReply(
    const protocol::PickReply &reply, bool selectObject)
{
  auto *ctx = appContext();
  if (selectObject) {
    // Identity -> mirror object -> its layer node; unknown clears, as the
    // monolith does.
    const vsr::scene::Object *object = nullptr;
    if (reply.objectIdentity) {
      object = ctx->vsr.scene.getObject(
          reply.objectIdentity->type, reply.objectIdentity->objectIndex);
    }
    ctx->setSelected(object);
    return;
  }
  if (!reply.hit || !m_camera.arcball)
    return;
  m_camera.arcball->setCenter(reply.worldPosition);
}

std::optional<SceneObjectRef> StudioViewport::selectedIdentity() const
{
  const auto node = appContext()->getFirstSelected();
  if (!node.valid())
    return {};
  const auto *object = (*node)->getObject();
  if (!object
      || (object->type() != ANARI_SURFACE && object->type() != ANARI_VOLUME))
    return {};
  return SceneObjectRef{object->type(), object->index()};
}

void StudioViewport::syncOutline()
{
  if (!m_serverReady || !m_connection)
    return;
  const auto identity = selectedIdentity();
  if (sameIdentity(identity, m_sentOutline))
    return;
  m_connection->setOutline(identity);
  m_sentOutline = identity;
}

void StudioViewport::sendViewportSettings()
{
  if (!m_serverReady || !m_connection)
    return;
  m_connection->setViewportSettings(m_settings);
}

void StudioViewport::onServerReady()
{
  m_serverReady = true;
  m_sentOutline.reset(); // the server starts with none; resend on change
  sendViewportSettings();
}

// Window settings ////////////////////////////////////////////////////////////

void StudioViewport::saveSettings(vsr::core::DataNode &root)
{
  root["showOverlay"] = m_showOverlay;
  root["highlightSelection"] = m_settings.highlightSelection;
  root["outlinePrimitives"] = m_settings.outlinePrimitives;
  root["showWorldBounds"] = m_settings.showWorldBounds;
  root["worldBoundsColor"] = m_settings.worldBoundsColor;
  root["worldBoundsWidth"] = m_settings.worldBoundsWidth;
  root["visualizeAOV"] = static_cast<int>(m_settings.visualizeAOV);
  root["depthVisualMinimum"] = m_settings.depthVisualMinimum;
  root["depthVisualMaximum"] = m_settings.depthVisualMaximum;
  root["edgeInvert"] = m_settings.edgeInvert;
  BaseViewport::saveSettings(root);
}

void StudioViewport::loadSettings(vsr::core::DataNode &root)
{
  BaseViewport::loadSettings(root);
  auto &s = m_settings;
  root["showOverlay"].getValue(ANARI_BOOL, &m_showOverlay);
  root["highlightSelection"].getValue(ANARI_BOOL, &s.highlightSelection);
  root["outlinePrimitives"].getValue(ANARI_BOOL, &s.outlinePrimitives);
  root["showWorldBounds"].getValue(ANARI_BOOL, &s.showWorldBounds);
  root["worldBoundsColor"].getValue(ANARI_FLOAT32_VEC4, &s.worldBoundsColor);
  root["worldBoundsWidth"].getValue(ANARI_INT32, &s.worldBoundsWidth);
  int aov = static_cast<int>(s.visualizeAOV);
  root["visualizeAOV"].getValue(ANARI_INT32, &aov);
  s.visualizeAOV = static_cast<vsr::rendering::AOVType>(
      std::clamp(aov, 0, AOV_TYPE_COUNT - 1));
  root["depthVisualMinimum"].getValue(ANARI_FLOAT32, &s.depthVisualMinimum);
  root["depthVisualMaximum"].getValue(ANARI_FLOAT32, &s.depthVisualMaximum);
  root["edgeInvert"].getValue(ANARI_BOOL, &s.edgeInvert);
  s.worldBoundsWidth = std::max(1, s.worldBoundsWidth);
  sendViewportSettings(); // no-op until the server is ready
}

// Connection-driven state ////////////////////////////////////////////////////

void StudioViewport::sendFrameConfig()
{
  if (!m_connection || m_viewport.size.x <= 0 || m_viewport.size.y <= 0)
    return;
  m_sentFrameConfig = vsr::math::uint2(m_viewport.size.x, m_viewport.size.y);
  m_connection->setFrameConfig(m_sentFrameConfig.x, m_sentFrameConfig.y);
  m_reportResizes = true;
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

void StudioViewport::dropMirrorReferences()
{
  m_camera.current = {};
  m_camera.arcballToken = {};
  m_manipulatorSynchronized = false;
  m_renderers.objects.clear();
  m_renderers.current = {};
  m_reportResizes = false;
  m_sentFrameConfig = vsr::math::uint2(0, 0);
  // The server this state was sent to is gone or being re-bootstrapped.
  m_serverReady = false;
  m_sentOutline.reset();
  if (m_connection && m_pendingPick.valid())
    m_connection->projectOps().forget(m_pendingPick);
  m_pendingPick = {};
}

void StudioViewport::reset()
{
  dropMirrorReferences();
  m_hasFrame = false;
  m_lastHeader = {};
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
  if (!m_reportResizes || !m_connection
      || m_connection->state() != ConnectionState::Connected)
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

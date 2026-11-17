#include "snap.hpp"

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/supplementary/executor/Executor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "barDeco.hpp"
#include "globals.hpp"

namespace {

enum class eDrop { NONE, AREA, MAXIMIZE, MINIMIZE };

struct SDrop {
  eDrop kind = eDrop::NONE;
  // The whole window, titlebar and border included.
  CBox area;
  PHLMONITORREF monitor;

  bool operator==(const SDrop &other) const { return kind == other.kind && area == other.area && monitor == other.monitor; }
};

// The drop under the pointer during the current drag. Release applies this
// rather than recomputing, so the window lands where the preview showed.
PHLWINDOWREF g_dragged;
SDrop g_drop;

bool isSnappable(const PHLWINDOW &window) {
  if (!g_pOmarchyFloatingGlobalState->config.edgeSnap->value() || !validMapped(window) || !window->m_isFloating)
    return false;

  // Snapping belongs to the same windows the titlebars do.
  const auto workspaceTag = g_pOmarchyFloatingGlobalState->config.workspaceTag->value();
  return workspaceTag.empty() || window->m_ruleApplicator->m_tagKeeper.isTagged(workspaceTag);
}

// A window dropped near one edge of the work area fills that half of it, and
// near two that quarter, spaced the way tiled windows are: gaps_out from the
// work area's edges and gaps_in from the other half.
SDrop dropAt(const PHLWINDOW &window, const Vector2D &cursor) {
  PHLMONITOR monitor = window->m_monitor.lock();
  for (const auto &candidate : State::monitorState()->monitors()) {
    if (candidate->logicalBox().containsPoint(cursor)) {
      monitor = candidate;
      break;
    }
  }
  if (!monitor)
    return {};

  constexpr double SNAP_DISTANCE = 24;
  const auto work = monitor->logicalBoxMinusReserved();
  const bool left = cursor.x <= work.x + SNAP_DISTANCE;
  const bool right = !left && cursor.x >= work.x + work.w - SNAP_DISTANCE;
  const bool top = cursor.y <= work.y + SNAP_DISTANCE;
  const bool bottom = !top && cursor.y >= work.y + work.h - SNAP_DISTANCE;
  if (!left && !right && !top && !bottom)
    return {};

  static auto PGAPSIN = CConfigValue<Config::IComplexConfigValue>("general:gaps_in");
  static auto PGAPSOUT = CConfigValue<Config::IComplexConfigValue>("general:gaps_out");
  const auto *gapsIn = sc<Config::CCssGapData *>(PGAPSIN.ptr());
  const auto *gapsOut = sc<Config::CCssGapData *>(PGAPSOUT.ptr());

  // A window alone on its workspace has no other window to share a half with.
  const bool alone = window->m_workspace && window->m_workspace->getWindowCount(std::nullopt, std::nullopt, true) == 1;
  const bool edgeOnly = !left && !right;
  if (alone && edgeOnly && top) {
    const CBox full = {work.x + gapsOut->m_left, work.y + gapsOut->m_top, work.w - gapsOut->m_left - gapsOut->m_right,
                       work.h - gapsOut->m_top - gapsOut->m_bottom};
    return {eDrop::MAXIMIZE, full, monitor};
  }
  if (alone && edgeOnly && bottom && !g_pOmarchyFloatingGlobalState->config.onDropBottom->value().empty()) {
    // A strip about the height of Omarchy's dock, which is where a minimized
    // window goes.
    constexpr double DOCK_HEIGHT = 60;
    const CBox dock = {work.x + gapsOut->m_left, work.y + work.h - gapsOut->m_bottom - DOCK_HEIGHT, work.w - gapsOut->m_left - gapsOut->m_right, DOCK_HEIGHT};
    return {eDrop::MINIMIZE, dock, monitor};
  }

  const double midX = work.x + work.w / 2;
  const double midY = work.y + work.h / 2;
  const double x1 = right ? midX + gapsIn->m_left : work.x + gapsOut->m_left;
  const double x2 = left ? midX - gapsIn->m_right : work.x + work.w - gapsOut->m_right;
  const double y1 = bottom ? midY + gapsIn->m_top : work.y + gapsOut->m_top;
  const double y2 = top ? midY - gapsIn->m_bottom : work.y + work.h - gapsOut->m_bottom;
  return {eDrop::AREA, {x1, y1, x2 - x1, y2 - y1}, monitor};
}

// The preview is drawn where these are, moving from the dragged window to the
// drop like the window itself would.
PHLANIMVAR<Vector2D> g_previewPosition;
PHLANIMVAR<Vector2D> g_previewSize;
CBox g_previewDrawn;

CBox previewBox() { return {g_previewPosition->value(), g_previewSize->value()}; }

void damagePreview() {
  g_pHyprRenderer->damageBox(g_previewDrawn);
  g_previewDrawn = previewBox();
  g_pHyprRenderer->damageBox(g_previewDrawn);
}

void showDrop(const PHLWINDOW &window, const SDrop &drop) {
  if (g_dragged.lock() == window && g_drop == drop)
    return;

  if (!g_previewPosition) {
    const auto config = Config::animationTree()->getAnimationPropertyConfig("windowsMove");
    Animation::mgr()->createAnimation(Vector2D{}, g_previewPosition, config, AVARDAMAGE_NONE);
    Animation::mgr()->createAnimation(Vector2D{}, g_previewSize, config, AVARDAMAGE_NONE);
    g_previewPosition->setUpdateCallback([](auto) { damagePreview(); });
    g_previewSize->setUpdateCallback([](auto) { damagePreview(); });
  }

  const bool wasShown = !g_drop.area.empty();
  g_dragged = window;
  g_drop = drop;
  if (g_drop.area.empty()) {
    g_pHyprRenderer->damageBox(g_previewDrawn);
    g_previewDrawn = {};
    return;
  }

  if (!wasShown) {
    const auto reserved = window->getFullWindowReservedArea();
    g_previewPosition->setValueAndWarp(window->positionAnimation()->value() - reserved.topLeft);
    g_previewSize->setValueAndWarp(window->sizeAnimation()->value() + reserved.topLeft + reserved.bottomRight);
  }
  *g_previewPosition = g_drop.area.pos();
  *g_previewSize = g_drop.area.size();
  damagePreview();
}

} // namespace

void omarchyFloatingUpdateSnapPreview(const Vector2D &cursor) {
  const auto &drag = g_layoutManager->dragController();
  const PHLWINDOW window = drag->mode() == MBIND_MOVE && drag->target() ? drag->target()->window() : nullptr;
  showDrop(window, window && isSnappable(window) ? dropAt(window, cursor) : SDrop{});
}

void omarchyFloatingDrawSnapPreview(eRenderStage stage) {
  if (stage != RENDER_PRE_WINDOW || g_drop.area.empty())
    return;

  // Drawn just before the dragged window, so it covers the others but not it.
  const auto &renderData = g_pHyprRenderer->m_renderData;
  const auto window = g_dragged.lock();
  const auto monitor = renderData.pMonitor.lock();
  if (!window || renderData.currentWindow.lock() != window || !monitor || monitor != g_drop.monitor.lock())
    return;

  CBox box = previewBox();
  box.translate(-monitor->m_position).scale(monitor->m_scale).round();
  CRectPassElement::SRectData data;
  data.box = box;
  data.color = CHyprColor{static_cast<uint64_t>(g_pOmarchyFloatingGlobalState->config.snapPreviewColor->value())};
  data.round = window->rounding() * monitor->m_scale;
  data.roundingPower = window->roundingPower();
  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
}

void omarchyFloatingSnapToEdge(PHLWINDOW window) {
  if (!window || g_dragged.lock() != window)
    return;

  const auto drop = g_drop;
  showDrop(nullptr, {});
  if (!isSnappable(window))
    return;

  switch (drop.kind) {
  case eDrop::NONE:
    return;
  case eDrop::MAXIMIZE:
    Fullscreen::controller()->setFullscreenMode(window, Fullscreen::FSMODE_MAXIMIZED);
    return;
  case eDrop::MINIMIZE:
    Config::Supplementary::executor()->spawn(windowCommand(g_pOmarchyFloatingGlobalState->config.onDropBottom->value(), window));
    return;
  case eDrop::AREA: {
    const auto reserved = window->getFullWindowReservedArea();
    const CBox box = {drop.area.x + reserved.topLeft.x, drop.area.y + reserved.topLeft.y, drop.area.w - reserved.topLeft.x - reserved.bottomRight.x,
                      drop.area.h - reserved.topLeft.y - reserved.bottomRight.y};
    g_layoutManager->setTargetGeom(box, window->layoutTarget());
    Log::logger->log(Log::DEBUG, "[hyprbars] Snapped {:x} to {} {} {}x{}", (uintptr_t)window.get(), box.x, box.y, box.w, box.h);
  }
  }
}

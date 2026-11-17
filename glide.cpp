#include "glide.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/supplementary/DragController.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <cmath>

namespace {

PHLWINDOWREF g_window;
CBox g_from;
Time::steady_tp g_start;

} // namespace

void omarchyFloatingGlideFrom(PHLWINDOW window, const CBox &from) {
  g_window = window;
  g_from = from;
  g_start = Time::steadyNow();
}

void omarchyFloatingNoticeDrag() {
  // Hyprland starts the drags it runs itself, for a client's move request or a
  // mouse bind, by putting the window under the pointer, and the next step
  // warps it there.
  static PHLWINDOWREF noticed;
  const auto &drag = g_layoutManager->dragController();
  const PHLWINDOW window = drag->mode() == MBIND_MOVE && drag->target() ? drag->target()->window() : nullptr;
  if (!window) {
    noticed.reset();
    return;
  }
  if (noticed.lock() == window)
    return;

  noticed = window;
  const CBox drawn = {window->positionAnimation()->value(), window->sizeAnimation()->value()};
  const CBox placed = {window->positionAnimation()->goal(), window->sizeAnimation()->goal()};
  if (g_window.lock() != window && drawn != placed)
    omarchyFloatingGlideFrom(window, drawn);
}

void omarchyFloatingUpdateGlide(eRenderStage stage) {
  const auto window = g_window.lock();
  if (stage != RENDER_PRE || !window)
    return;

  // Lasts as long as the window's own move animation, whose speed is in tenths
  // of a second.
  const auto config = window->positionAnimation()->getConfig().lock();
  const auto values = config ? config->pValues.lock() : nullptr;
  const double duration = values && values->internalEnabled ? values->internalSpeed * 100.0 : 0.0;
  const double elapsed = std::chrono::duration<double, std::milli>(Time::steadyNow() - g_start).count();
  const auto &drag = g_layoutManager->dragController();
  const bool dragging = drag->mode() == MBIND_MOVE && drag->target() == window->layoutTarget();
  if (!dragging || elapsed >= duration) {
    g_window.reset();
    return;
  }

  // Each frame redraws the window part of the way from where it started to
  // where the drag has put it, easing out like Omarchy's window animations.
  const double eased = 1.0 - std::pow(1.0 - elapsed / duration, 5);
  const auto position = g_from.pos() + (window->positionAnimation()->goal() - g_from.pos()) * eased;
  const auto size = g_from.size() + (window->sizeAnimation()->goal() - g_from.size()) * eased;
  g_pHyprRenderer->damageWindow(window);
  window->positionAnimation()->setValue(position);
  window->sizeAnimation()->setValue(size);
  g_pHyprRenderer->damageWindow(window);
}

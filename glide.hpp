#pragma once
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

// Animates a window being dragged from the box it was drawn in towards the
// pointer. A move warps the window to the pointer on every step, so a drag
// that restores a maximized window would otherwise jump.
void omarchyFloatingGlideFrom(PHLWINDOW window, const CBox &from);
void omarchyFloatingNoticeDrag();
void omarchyFloatingUpdateGlide(eRenderStage stage);

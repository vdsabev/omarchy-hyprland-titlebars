#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

// Follows the pointer through a window move, for the preview and for the drop.
void omarchyFloatingUpdateSnapPreview(const Vector2D &cursor);
void omarchyFloatingDrawSnapPreview(eRenderStage stage);
// Applies the drop the preview showed, once the move has ended.
void omarchyFloatingSnapToEdge(PHLWINDOW window);

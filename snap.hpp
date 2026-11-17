#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <functional>

// Follows the pointer through a window move, for the preview and for the drop.
void omarchyFloatingUpdateSnapPreview(const Vector2D &cursor);
void omarchyFloatingDrawSnapPreview(eRenderStage stage);
// Ends the preview of window's drag and returns a call that applies the drop it
// showed. Taken at the release, since the next pointer motion clears the drop,
// and called once the move has ended.
std::function<void()> omarchyFloatingTakeDrop(PHLWINDOW window);

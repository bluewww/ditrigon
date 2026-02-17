/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 channel list backend
 *
 * The GTK2 frontend uses a custom GtkTreeModel implementation in this file.
 * GTK4 no longer uses GtkTreeView for channel lists; `chanlist.c` now uses a
 * GtkListBox-based model directly, so no standalone model type is required.
 */
#include "fe-gtk4.h"

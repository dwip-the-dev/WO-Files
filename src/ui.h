#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>

GtkWidget* ui_create_grid(void);
void ui_load_directory(GtkIconView *view, const char *path);
void ui_filter_search(GtkIconView *view, const char *path, const char *q);
GdkPixbuf* ui_load_thumbnail(const char *path);

#endif

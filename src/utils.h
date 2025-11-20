#ifndef UTILS_H
#define UTILS_H

#include <gtk/gtk.h>

extern gboolean sudo_mode;

typedef struct {
    char *name;
    char *path;
    gboolean is_dir;
} UtilsEntry;

GList* utils_list_dir(const char *path);
void utils_entry_free(UtilsEntry *e);

void utils_read_directory(GtkListStore *store, const char *dir);
void utils_filter_local(GtkListStore *store, const char *path, const char *query);

GdkPixbuf* utils_get_icon(const char *name, gboolean is_dir);
GdkPixbuf* utils_load_scaled(const char *file, int size);

#endif

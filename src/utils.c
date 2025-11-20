#include "utils.h"
#include <gtk/gtk.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static GdkPixbuf *icon_folder = NULL;
static GdkPixbuf *icon_fallback = NULL;

GdkPixbuf* utils_load_scaled(const char *file, int size)
{
    GError *err = NULL;
    GdkPixbuf *raw = gdk_pixbuf_new_from_file(file, &err);

    if (!raw)
    {
        GdkPixbuf *block = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size, size);
        gdk_pixbuf_fill(block, 0x777777ff);
        return block;
    }

    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(raw, size, size, GDK_INTERP_BILINEAR);
    g_object_unref(raw);
    return scaled;
}

GdkPixbuf* utils_get_icon(const char *name, gboolean is_dir)
{
    if (!icon_folder)
        icon_folder = utils_load_scaled("assets/folder.png", 48);

    if (!icon_fallback)
        icon_fallback = utils_load_scaled("assets/file.png", 48);

    if (is_dir)
        return icon_folder;

    const char *ext = strrchr(name, '.');

    if (ext && ext[1] != 0)
    {
        ext++;

        char p[256];
        snprintf(p, sizeof(p), "assets/%s.png", ext);

        if (g_file_test(p, G_FILE_TEST_EXISTS))
        {
            GdkPixbuf *icon = utils_load_scaled(p, 48);
            if (icon) return icon;
        }
    }

    return icon_fallback;
}


void utils_entry_free(UtilsEntry *e)
{
    if (!e) return;
    g_free(e->name);
    g_free(e->path);
    g_free(e);
}

static UtilsEntry* make_entry(const char *dir, const char *name, gboolean is_dir)
{
    UtilsEntry *e = g_malloc(sizeof(UtilsEntry));
    e->name = g_strdup(name);

    char full[4096];
    snprintf(full, sizeof(full), "%s/%s", dir, name);
    e->path = g_strdup(full);

    e->is_dir = is_dir;
    return e;
}

GList* utils_list_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return NULL;

    struct dirent *ent;
    GList *list = NULL;

    while ((ent = readdir(d)) != NULL)
    {
        if (!strcmp(ent->d_name, ".")) continue;

        /* hide dotfiles unless sudo enabled */
        if (!sudo_mode && ent->d_name[0] == '.')
            continue;

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);

        struct stat st;
        if (lstat(full, &st) != 0) continue;

        gboolean is_dir = S_ISDIR(st.st_mode);

        list = g_list_prepend(list, make_entry(path, ent->d_name, is_dir));
    }

    closedir(d);
    return g_list_reverse(list);
}


void utils_read_directory(GtkListStore *store, const char *dir)
{
    GtkTreeIter it;
    GList *list = utils_list_dir(dir);

    for (GList *l = list; l; l = l->next)
    {
        UtilsEntry *e = l->data;

        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it,
            0, utils_get_icon(e->name, e->is_dir),
            1, e->name,
            2, e->path,
            3, e->is_dir,
            -1
        );

        utils_entry_free(e);
    }

    g_list_free(list);
}


void utils_filter_local(GtkListStore *store, const char *path, const char *query)
{
    GtkTreeIter it;
    GList *list = utils_list_dir(path);

    for (GList *l = list; l; l = l->next)
    {
        UtilsEntry *e = l->data;

        if (strcasestr(e->name, query))
        {
            gtk_list_store_append(store, &it);
            gtk_list_store_set(store, &it,
                0, utils_get_icon(e->name, e->is_dir),
                1, e->name,
                2, e->path,
                3, e->is_dir,
                -1
            );
        }

        utils_entry_free(e);
    }

    g_list_free(list);
}

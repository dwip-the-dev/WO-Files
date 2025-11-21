#include "ui.h"
#include "utils.h"
#include <gtk/gtk.h>
#include <string.h>

GtkWidget* ui_create_grid(void)
{
    GtkListStore *st = gtk_list_store_new(
        4,
        GDK_TYPE_PIXBUF,  
        G_TYPE_STRING,   
        G_TYPE_STRING,  
        G_TYPE_BOOLEAN 
    );

    GtkWidget *v = gtk_icon_view_new_with_model(GTK_TREE_MODEL(st));
    g_object_unref(st);

    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(v), 0);
    gtk_icon_view_set_text_column(GTK_ICON_VIEW(v), 1);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(v), 80);
    gtk_icon_view_set_spacing(GTK_ICON_VIEW(v), 6);
    gtk_icon_view_set_margin(GTK_ICON_VIEW(v), 10);

    return v;
}

void ui_load_directory(GtkIconView *v, const char *path)
{
    GtkListStore *s = GTK_LIST_STORE(gtk_icon_view_get_model(v));
    gtk_list_store_clear(s);
    utils_read_directory(s, path);
}

GdkPixbuf* ui_load_thumbnail(const char *path)
{
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) return NULL;
    return gdk_pixbuf_new_from_file_at_size(path, 48, 48, NULL);
}

static void deep(GtkListStore *st, const char *dir, const char *q)
{
    GList *list = utils_list_dir(dir);
    GtkTreeIter it;

    for (GList *l = list; l; l = l->next)
    {
        UtilsEntry *e = l->data;

        if (strcasestr(e->name, q))
        {
            gtk_list_store_append(st, &it);
            gtk_list_store_set(st, &it,
                0, utils_get_icon(e->name, e->is_dir), 
                1, e->name,
                2, e->path,
                3, e->is_dir,
                -1
            );
        }

        if (e->is_dir)
            deep(st, e->path, q);

        utils_entry_free(e);
    }

    g_list_free(list);
}

void ui_filter_search(GtkIconView *v, const char *path, const char *q)
{
    GtkListStore *s = GTK_LIST_STORE(gtk_icon_view_get_model(v));
    gtk_list_store_clear(s);

    if (strlen(q) < 2)
    {
        utils_filter_local(s, path, q);
        return;
    }

    deep(s, path, q);
}

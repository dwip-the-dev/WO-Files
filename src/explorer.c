#include "explorer.h"
#include "utils.h"
#include "ui.h"
#include <gtk/gtk.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

char current_path[4096];
char clipboard_path[4096] = "";
gboolean clipboard_cut = FALSE;
gboolean sudo_mode = FALSE;

static GPtrArray *history_back;
static GPtrArray *history_forward;

static GtkWidget *status_label;
static GtkWidget *path_entry;
static GtkWidget *search_entry;
static GtkWidget *grid_view;
static GtkWidget *sidebar_top;
static GtkWidget *main_window;

static void add_shortcut(GtkButton *b);
static void load_path(const char *path, gboolean hist);

static gchar* get_free_space_str(const char *path)
{
    struct statvfs s;
    if (statvfs(path, &s) != 0)
        return g_strdup("Unknown");

    unsigned long long free_bytes =
        (unsigned long long)s.f_bsize *
        (unsigned long long)s.f_bavail;

    double gb = (double)free_bytes / (1024.0*1024.0*1024.0);

    return g_strdup_printf("%.1f GB", gb);
}

static gchar* get_human_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return g_strdup("Unknown");

    double s = st.st_size;

    if (s < 1024) return g_strdup_printf("%.0f B", s);
    s /= 1024.0;
    if (s < 1024) return g_strdup_printf("%.1f KB", s);
    s /= 1024.0;
    if (s < 1024) return g_strdup_printf("%.1f MB", s);
    s /= 1024.0;
    return g_strdup_printf("%.2f GB", s);
}

static void update_status(void)
{
    GtkTreeModel *m = gtk_icon_view_get_model(GTK_ICON_VIEW(grid_view));
    GtkTreeIter it;
    int count = 0;

    gboolean ok = gtk_tree_model_get_iter_first(m, &it);
    while (ok) {
        count++;
        ok = gtk_tree_model_iter_next(m, &it);
    }

    gchar *free = get_free_space_str(current_path);

    GList *sel = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(grid_view));
    if (!sel) {
        gchar *txt = g_strdup_printf("Items: %d  |  Free: %s", count, free);
        gtk_label_set_text(GTK_LABEL(status_label), txt);
        g_free(txt);
        g_free(free);
        return;
    }

    GtkTreePath *p = sel->data;
    GtkTreeIter it2;
    gchar *path = NULL;

    gtk_tree_model_get_iter(m, &it2, p);
    gtk_tree_model_get(m, &it2, 2, &path, -1);

    gchar *size = get_human_size(path);

    gchar *txt = g_strdup_printf(
        "Items: %d  |  Free: %s  |  Selected: %s — %s",
        count, free, g_path_get_basename(path), size
    );

    gtk_label_set_text(GTK_LABEL(status_label), txt);

    g_free(txt);
    g_free(path);
    g_free(size);
    g_list_free_full(sel, (GDestroyNotify)gtk_tree_path_free);
    g_free(free);
}

static void push_back(const char *p){ g_ptr_array_add(history_back, g_strdup(p)); }
static void push_forward(const char *p){ g_ptr_array_add(history_forward, g_strdup(p)); }

static char* pop_back(void){
    if (!history_back->len) return NULL;
    char *v = history_back->pdata[history_back->len-1];
    g_ptr_array_remove_index_fast(history_back, history_back->len-1);
    return v;
}

static char* pop_forward(void){
    if (!history_forward->len) return NULL;
    char *v = history_forward->pdata[history_forward->len-1];
    g_ptr_array_remove_index_fast(history_forward, history_forward->len-1);
    return v;
}

static void clear_forward(void)
{
    for (guint i = 0; i < history_forward->len; i++)
        g_free(history_forward->pdata[i]);
    g_ptr_array_set_size(history_forward, 0);
}

static void load_path(const char *path, gboolean hist)
{
    if (hist && current_path[0])
        push_back(current_path);

    g_strlcpy(current_path, path, sizeof(current_path));
    gtk_entry_set_text(GTK_ENTRY(path_entry), current_path);

    ui_load_directory(GTK_ICON_VIEW(grid_view), current_path);
    update_status();

    if (hist) clear_forward();
}

static void on_back(GtkButton *b){
    char *p = pop_back();
    if (!p) return;
    push_forward(current_path);
    load_path(p, FALSE);
    g_free(p);
}

static void on_forward(GtkButton *b){
    char *p = pop_forward();
    if (!p) return;
    push_back(current_path);
    load_path(p, FALSE);
    g_free(p);
}

static void on_up(GtkButton *b){
    if (!strcmp(current_path, "/")) return;

    char p[4096];
    g_strlcpy(p, current_path, sizeof(p));

    char *s = g_strrstr(p, "/");
    if (s == p) p[1] = 0;
    else *s = 0;

    load_path(p, TRUE);
}

static void on_path_enter(GtkEntry *e){
    const char *t = gtk_entry_get_text(e);
    if (g_file_test(t, G_FILE_TEST_IS_DIR))
        load_path(t, TRUE);
    else
        gtk_entry_set_text(GTK_ENTRY(path_entry), current_path);
}

static void on_search(GtkEntry *e){
    const char *q = gtk_entry_get_text(e);

    if (!q || !q[0]) {
        ui_load_directory(GTK_ICON_VIEW(grid_view), current_path);
        update_status();
        return;
    }

    ui_filter_search(GTK_ICON_VIEW(grid_view), current_path, q);
    update_status();
}

static void open_with_default(const char *path)
{
    char cmd[6000];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" 2>/dev/null &", path);
    system(cmd);
}

static void on_item_activated(GtkIconView *v, GtkTreePath *p)
{
    GtkTreeModel *m = gtk_icon_view_get_model(v);
    GtkTreeIter it;

    if (!gtk_tree_model_get_iter(m, &it, p))
        return;

    gchar *full = NULL;
    gboolean dir = FALSE;

    gtk_tree_model_get(m, &it,
        2, &full,
        3, &dir,
        -1
    );

    if (dir)
        load_path(full, TRUE);
    else
        open_with_default(full);

    g_free(full);
}

static gboolean get_sel(char **path, gboolean *dir)
{
    GList *l = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(grid_view));
    if (!l) return FALSE;

    GtkTreePath *tp = l->data;
    GtkTreeModel *m = gtk_icon_view_get_model(GTK_ICON_VIEW(grid_view));
    GtkTreeIter it;

    if (!gtk_tree_model_get_iter(m, &it, tp))
        return FALSE;

    gtk_tree_model_get(m, &it,
        2, path,
        3, dir,
        -1
    );

    g_list_free_full(l, (GDestroyNotify)gtk_tree_path_free);
    return TRUE;
}

static void file_delete(const char *p)
{
    char cmd[6000];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", p);
    system(cmd);

    ui_load_directory(GTK_ICON_VIEW(grid_view), current_path);
    update_status();
}

static void file_copy(const char *p, gboolean cut)
{
    g_strlcpy(clipboard_path, p, sizeof(clipboard_path));
    clipboard_cut = cut;
}

static void do_cut(const char *p){ file_copy(p, TRUE); }

static void file_paste(const char *dest)
{
    if (!clipboard_path[0]) return;

    char *base = g_path_get_basename(clipboard_path);

    char out[4096];
    snprintf(out, sizeof(out), "%s/%s", dest, base);

    char cmd[9000];

    if (clipboard_cut)
        snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s\"", clipboard_path, out);
    else
        snprintf(cmd, sizeof(cmd), "cp -r \"%s\" \"%s\"", clipboard_path, out);

    system(cmd);

    clipboard_cut = FALSE;
    clipboard_path[0] = 0;

    g_free(base);

    ui_load_directory(GTK_ICON_VIEW(grid_view), current_path);
    update_status();
}

static void file_rename(GtkMenuItem *i, gpointer user_data)
{
    const char *old = user_data;

    char oldc[4096];
    g_strlcpy(oldc, old, sizeof(oldc));

    GtkWidget *d = gtk_dialog_new_with_buttons(
        "Rename", GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL,
        "OK", GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget *e = gtk_entry_new();

    gtk_entry_set_text(GTK_ENTRY(e), g_path_get_basename(oldc));
    gtk_box_pack_start(GTK_BOX(box), e, FALSE, FALSE, 5);
    gtk_widget_show_all(d);

    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
    {
        const char *nn = gtk_entry_get_text(GTK_ENTRY(e));
        char *dir = g_path_get_dirname(oldc);

        char np[4096];
        snprintf(np, sizeof(np), "%s/%s", dir, nn);

        rename(oldc, np);
        g_free(dir);
    }

    gtk_widget_destroy(d);
    ui_load_directory(GTK_ICON_VIEW(grid_view), current_path);
    update_status();
}

static gboolean on_right(GtkWidget *w, GdkEventButton *ev)
{
    if (ev->type != GDK_BUTTON_PRESS || ev->button != 3)
        return FALSE;

    GtkWidget *menu = gtk_menu_new();

    GtkWidget *m_open   = gtk_menu_item_new_with_label("Open");
    GtkWidget *m_copy   = gtk_menu_item_new_with_label("Copy");
    GtkWidget *m_cut    = gtk_menu_item_new_with_label("Cut");
    GtkWidget *m_paste  = gtk_menu_item_new_with_label("Paste");
    GtkWidget *m_rename = gtk_menu_item_new_with_label("Rename");
    GtkWidget *m_delete = gtk_menu_item_new_with_label("Delete");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_open);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_copy);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_cut);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_paste);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_rename);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_delete);
    gtk_widget_show_all(menu);

    char *path = NULL;
    gboolean is_dir = FALSE;
    gboolean ok = get_sel(&path, &is_dir);

    if (ok)
    {
        if (is_dir)
            g_signal_connect_swapped(m_open, "activate",
                G_CALLBACK(load_path), g_strdup(path));
        else
            g_signal_connect_swapped(m_open, "activate",
                G_CALLBACK(open_with_default), g_strdup(path));

        g_signal_connect_swapped(m_copy, "activate",
             G_CALLBACK(file_copy), g_strdup(path));

        clipboard_cut = FALSE;

        g_signal_connect_swapped(m_cut, "activate",
             G_CALLBACK(do_cut), g_strdup(path));

        g_signal_connect(m_rename, "activate",
             G_CALLBACK(file_rename), g_strdup(path));

        g_signal_connect_swapped(m_delete, "activate",
             G_CALLBACK(file_delete), g_strdup(path));
    }

    g_signal_connect_swapped(m_paste, "activate",
         G_CALLBACK(file_paste), g_strdup(current_path));

    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)ev);

    g_free(path);
    return TRUE;
}

static void on_sudo(GtkToggleButton *b, gpointer d)
{
    sudo_mode = gtk_toggle_button_get_active(b);
    ui_load_directory(GTK_ICON_VIEW(grid_view), current_path);
    update_status();
}

static void sidebar_open(GtkButton *b, gpointer d)
{
    load_path(d, TRUE);
}

static GtkWidget* create_sidebar(void)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    sidebar_top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(scroll), sidebar_top);

    gtk_box_pack_start(GTK_BOX(outer), scroll, TRUE, TRUE, 0);

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *add = gtk_button_new_with_label("+ Add Shortcut");
    g_signal_connect(add, "clicked", G_CALLBACK(add_shortcut), NULL);

    gtk_box_pack_start(GTK_BOX(bottom), add, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(outer), bottom, FALSE, FALSE, 0);

    const char *H = g_get_home_dir();

    struct { const char *lbl; const char *path; } a[] = {
        {"Home", H},
        {"Desktop",  g_strdup_printf("%s/Desktop", H)},
        {"Downloads",g_strdup_printf("%s/Downloads", H)},
        {"Root", "/"}
    };

    for (int i = 0; i < 4; i++)
    {
        GtkWidget *b = gtk_button_new_with_label(a[i].lbl);
        g_signal_connect(b, "clicked", G_CALLBACK(sidebar_open),
            g_strdup(a[i].path));
        gtk_box_pack_start(GTK_BOX(sidebar_top), b, FALSE, FALSE, 2);
    }

    return outer;
}

static void add_shortcut(GtkButton *b)
{
    GtkWidget *d = gtk_file_chooser_dialog_new(
        "Select Folder", NULL,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Add", GTK_RESPONSE_ACCEPT,
        NULL
    );

    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
    {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        char *base = g_path_get_basename(f);

        char shortname[64];

        if (strlen(base) > 14)
            snprintf(shortname, sizeof(shortname), "%.12s…", base);
        else
            snprintf(shortname, sizeof(shortname), "%s", base);

        GtkWidget *btn = gtk_button_new_with_label(shortname);

        g_signal_connect(btn, "clicked",
            G_CALLBACK(sidebar_open), g_strdup(f));

        gtk_box_pack_start(GTK_BOX(sidebar_top), btn, FALSE, FALSE, 2);
        gtk_widget_show_all(sidebar_top);

        g_free(base);
    }

    gtk_widget_destroy(d);
}

static GtkWidget* create_statusbar(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(box, "statusbar");

    status_label = gtk_label_new("Items: 0");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);

    gtk_box_pack_start(GTK_BOX(box), status_label, FALSE, FALSE, 8);

    return box;
}

GtkWidget* explorer_create_window(void)
{
    history_back    = g_ptr_array_new();
    history_forward = g_ptr_array_new();

    g_strlcpy(current_path, g_get_home_dir(), sizeof(current_path));

    GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    main_window = w;

    gtk_window_set_title(GTK_WINDOW(w), "WO Files");
    gtk_window_set_default_size(GTK_WINDOW(w), 1400, 900);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_path(css, "assets/theme.css", NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_add(GTK_CONTAINER(w), hbox);

    GtkWidget *sidebar = create_sidebar();
    gtk_widget_set_name(sidebar, "sidebar");
    gtk_widget_set_size_request(sidebar, 220, -1);
    gtk_box_pack_start(GTK_BOX(hbox), sidebar, FALSE, FALSE, 0);

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(hbox), right, TRUE, TRUE, 0);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(right), bar, FALSE, FALSE, 0);

    GtkWidget *back = gtk_button_new_with_label("◀");
    GtkWidget *fwd  = gtk_button_new_with_label("▶");
    GtkWidget *up   = gtk_button_new_with_label("⬆");
    GtkWidget *sudo_btn = gtk_toggle_button_new_with_label("SUDO");

    gtk_box_pack_start(GTK_BOX(bar), back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), fwd,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), up,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), sudo_btn, FALSE, FALSE, 0);

    path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(path_entry), current_path);
    gtk_box_pack_start(GTK_BOX(bar), path_entry, TRUE, TRUE, 0);

    search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search…");
    gtk_box_pack_start(GTK_BOX(bar), search_entry, FALSE, FALSE, 0);

    grid_view = ui_create_grid();

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    gtk_container_add(GTK_CONTAINER(scroll), grid_view);
    gtk_box_pack_start(GTK_BOX(right), scroll, TRUE, TRUE, 0);

    GtkWidget *status = create_statusbar();
    gtk_box_pack_start(GTK_BOX(right), status, FALSE, FALSE, 0);

    ui_load_directory(GTK_ICON_VIEW(grid_view), current_path);
    update_status();

    g_signal_connect(back, "clicked", G_CALLBACK(on_back), NULL);
    g_signal_connect(fwd, "clicked", G_CALLBACK(on_forward), NULL);
    g_signal_connect(up, "clicked", G_CALLBACK(on_up), NULL);

    g_signal_connect(path_entry, "activate", G_CALLBACK(on_path_enter), NULL);
    g_signal_connect(search_entry, "changed", G_CALLBACK(on_search), NULL);

    g_signal_connect(grid_view, "item-activated", G_CALLBACK(on_item_activated), NULL);
    g_signal_connect(grid_view, "button-press-event", G_CALLBACK(on_right), NULL);

    g_signal_connect(sudo_btn, "toggled", G_CALLBACK(on_sudo), NULL);

    return w;
}

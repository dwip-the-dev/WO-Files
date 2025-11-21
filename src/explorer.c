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
static GtkCssProvider *css_provider = NULL;
static GtkWidget *main_window;

static void add_shortcut(GtkButton *b);
static void load_path(const char *path, gboolean hist);

static void ensure_theme_directory(void)
{
    struct stat st = {0};
    if (stat("assets/themes", &st) == -1) {
        mkdir("assets", 0755);
        mkdir("assets/themes", 0755);
    }
}


static gboolean copy_file(const char *src, const char *dst)
{
    FILE *src_file = fopen(src, "rb");
    if (!src_file) return FALSE;
    
    FILE *dst_file = fopen(dst, "wb");
    if (!dst_file) {
        fclose(src_file);
        return FALSE;
    }
    
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
        fwrite(buffer, 1, bytes, dst_file);
    }
    
    fclose(src_file);
    fclose(dst_file);
    return TRUE;
}


static char* extract_css_from_wo(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    GString *css = g_string_new("");
    char line[4096];
    gboolean in_css = FALSE;
    gboolean has_theme_name = FALSE;

    while (fgets(line, sizeof(line), f)) {
        if (!in_css) {
         
            if (g_str_has_prefix(line, "THEMENAME:")) {
                has_theme_name = TRUE;
                continue;
            }
            
    
            if (g_str_has_prefix(line, "---") || has_theme_name) {
                in_css = TRUE;
                
           
                if (g_str_has_prefix(line, "---")) {
                    continue;
                }
            } else {
              
                continue;
            }
        }

   
        g_string_append(css, line);
    }

    fclose(f);
    
    if (css->len == 0) {
        g_string_free(css, TRUE);
        return NULL;
    }
    
    return g_string_free(css, FALSE);
}


static char* extract_theme_name_from_wo(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }

    char line[256];
    char *theme_name = NULL;

    if (fgets(line, sizeof(line), f)) {
        if (g_str_has_prefix(line, "THEMENAME:")) {
            char name[128] = {0};
            if (sscanf(line, "THEMENAME: %127[^\n]", name) == 1) {
                theme_name = g_strdup(name);
            }
        }
    }
    
    fclose(f);
    return theme_name;
}


static void load_theme_from_wo(const char *path)
{
    char *css_content = extract_css_from_wo(path);
    if (!css_content) {
        g_warning("No CSS content extracted from: %s", path);
        return;
    }


    GtkCssProvider *provider = gtk_css_provider_new();

    GError *error = NULL;
    if (!gtk_css_provider_load_from_data(provider, css_content, -1, &error)) {
        g_warning("CSS load failed: %s", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        g_free(css_content);
        g_object_unref(provider);
        return;
    }


    if (css_provider) {
        gtk_style_context_remove_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(css_provider)
        );
        g_object_unref(css_provider);
        css_provider = NULL;
    }


    css_provider = provider;


    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    g_free(css_content);
}


static gboolean save_wo_file(const char *src_path, char **saved_filename)
{
    ensure_theme_directory();
    

    char *theme_name = extract_theme_name_from_wo(src_path);
    if (!theme_name) {
        
        const char *base_name = g_path_get_basename(src_path);
        theme_name = g_strdup(base_name);
    }
    
   
    char safe_name[256];
    char *p = safe_name;
    const char *src = theme_name;
    while (*src && (p - safe_name) < sizeof(safe_name) - 5) { 
        if ((*src >= 'a' && *src <= 'z') || 
            (*src >= 'A' && *src <= 'Z') || 
            (*src >= '0' && *src <= '9') ||
            *src == '-' || *src == '_') {
            *p++ = *src;
        } else if (*src == ' ') {
            *p++ = '_';
        }
        src++;
    }
    *p = '\0';
    
   
    if (!g_str_has_suffix(safe_name, ".wo")) {
        g_strlcat(safe_name, ".wo", sizeof(safe_name));
    }
    
    char dst_path[512];
    snprintf(dst_path, sizeof(dst_path), "assets/themes/%s", safe_name);
    
   
    gboolean success = copy_file(src_path, dst_path);
    if (success) {
        *saved_filename = g_strdup(safe_name);
    }
    
    g_free(theme_name);
    return success;
}


static void apply_wo_theme(const char *path, GtkWidget *theme_box)
{

    char *saved_filename = NULL;
    if (save_wo_file(path, &saved_filename)) {
    
        char saved_path[512];
        snprintf(saved_path, sizeof(saved_path), "assets/themes/%s", saved_filename);
        load_theme_from_wo(saved_path);
        
    
        char *theme_name = extract_theme_name_from_wo(saved_path);
        if (theme_name && theme_name[0]) {
           
            gboolean found = FALSE;
            GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(theme_box));
            GtkTreeIter iter;
            
            if (gtk_tree_model_get_iter_first(model, &iter)) {
                do {
                    gchar *existing_name;
                    gtk_tree_model_get(model, &iter, 0, &existing_name, -1);
                    if (existing_name && !strcmp(existing_name, theme_name)) {
                        found = TRUE;
                        g_free(existing_name);
                        break;
                    }
                    g_free(existing_name);
                } while (gtk_tree_model_iter_next(model, &iter));
            }
            
            if (!found) {
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_box), theme_name);
            }
            

            int item_count = gtk_tree_model_iter_n_children(model, NULL);
            for (int i = 0; i < item_count; i++) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(theme_box), i);
                const char *current = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(theme_box));
                if (current && !strcmp(current, theme_name)) {
                    break;
                }
            }
            
            g_free(theme_name);
        }
        
        g_free(saved_filename);
    }
}


static void load_saved_wo_themes(GtkWidget *theme_box)
{
    ensure_theme_directory();
    
    GDir *dir = g_dir_open("assets/themes", 0, NULL);
    if (!dir) return;
    
    const gchar *filename;
    while ((filename = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_suffix(filename, ".wo")) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "assets/themes/%s", filename);
            
           
            char *theme_name = extract_theme_name_from_wo(full_path);
            if (theme_name && theme_name[0]) {
              
                gboolean found = FALSE;
                GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(theme_box));
                GtkTreeIter iter;
                
                if (gtk_tree_model_get_iter_first(model, &iter)) {
                    do {
                        gchar *existing_name;
                        gtk_tree_model_get(model, &iter, 0, &existing_name, -1);
                        if (existing_name && !strcmp(existing_name, theme_name)) {
                            found = TRUE;
                            g_free(existing_name);
                            break;
                        }
                        g_free(existing_name);
                    } while (gtk_tree_model_iter_next(model, &iter));
                }
                
                if (!found) {
                    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_box), theme_name);
                }
                
                g_free(theme_name);
            }
        }
    }
    
    g_dir_close(dir);
}


static void on_theme_drop(GtkWidget *w,
                          GdkDragContext *ctx,
                          gint x, gint y,
                          GtkSelectionData *data,
                          guint info, guint time,
                          gpointer user_data)
{
    if (!data) return;

    const gchar *uris = (const gchar*) gtk_selection_data_get_data(data);
    if (!uris) return;

    gchar **list = g_uri_list_extract_uris(uris);
    if (!list || !list[0]) {
        g_strfreev(list);
        return;
    }

  
    char *path = g_filename_from_uri(list[0], NULL, NULL);

    if (path && g_str_has_suffix(path, ".wo")) {
    
        GtkWidget *theme_box = NULL;
        GtkWidget *hbox = gtk_bin_get_child(GTK_BIN(main_window));
        if (hbox) {
            GList *children = gtk_container_get_children(GTK_CONTAINER(hbox));
            if (children && children->next) {
                GtkWidget *right = children->next->data;
                GList *right_children = gtk_container_get_children(GTK_CONTAINER(right));
                if (right_children) {
                    GtkWidget *bar = right_children->data;
                    GList *bar_children = gtk_container_get_children(GTK_CONTAINER(bar));
                    if (bar_children) {
                       
                        theme_box = g_list_last(bar_children)->data;
                    }
                    g_list_free(bar_children);
                }
                g_list_free(right_children);
            }
            g_list_free(children);
        }
        
        if (theme_box) {
            apply_wo_theme(path, theme_box);
        }
    }

    g_free(path);
    g_strfreev(list);
}


static void enable_theme_drop(GtkWidget *win)
{
    GtkTargetEntry targets[] = {
        { "text/uri-list", 0, 0 }
    };

    gtk_drag_dest_set(win,
        GTK_DEST_DEFAULT_ALL,
        targets, 1,
        GDK_ACTION_COPY);

    g_signal_connect(win, "drag-data-received",
        G_CALLBACK(on_theme_drop), NULL);
}


static gchar* get_free(const char *path){
    struct statvfs s;
    if(statvfs(path,&s)!=0) return g_strdup("?");
    unsigned long long fb=(unsigned long long)s.f_bsize*(unsigned long long)s.f_bavail;
    double gb = (double)fb/(1024.0*1024.0*1024.0);
    return g_strdup_printf("%.1f GB",gb);
}

static gchar* get_human_size(const char* p){
    struct stat st;
    if(stat(p,&st)!=0) return g_strdup("?");
    double s = st.st_size;
    if(s<1024) return g_strdup_printf("%.0f B",s);
    s/=1024.0;
    if(s<1024) return g_strdup_printf("%.1f KB",s);
    s/=1024.0;
    if(s<1024) return g_strdup_printf("%.1f MB",s);
    s/=1024.0;
    return g_strdup_printf("%.2f GB",s);
}

void load_theme(const char *file){
    if(!css_provider) css_provider = gtk_css_provider_new();
    char full[512];
    snprintf(full,sizeof(full),"assets/themes/%s",file);
    
    
    if (g_str_has_suffix(file, ".wo")) {
        load_theme_from_wo(full);
    } else {

        gtk_css_provider_load_from_path(css_provider,full,NULL);
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(css_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
}

static void update_status(void){
    GtkTreeModel *m = gtk_icon_view_get_model(GTK_ICON_VIEW(grid_view));
    GtkTreeIter it;
    int count = 0;
    gboolean ok = gtk_tree_model_get_iter_first(m,&it);
    while(ok){ count++; ok = gtk_tree_model_iter_next(m,&it); }

    gchar *free = get_free(current_path);

    GList *sel = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(grid_view));
    if(!sel){
        gchar *txt = g_strdup_printf("Items: %d | Free: %s",count,free);
        gtk_label_set_text(GTK_LABEL(status_label),txt);
        g_free(txt);
        g_free(free);
        return;
    }

    GtkTreePath *p = sel->data;
    GtkTreeIter it2;
    gchar *path=NULL;
    gtk_tree_model_get_iter(m,&it2,p);
    gtk_tree_model_get(m,&it2,2,&path,-1);

    gchar *sz = get_human_size(path);
    gchar *txt = g_strdup_printf(
        "Items: %d | Free: %s | Selected: %s — %s",
        count,free,g_path_get_basename(path),sz
    );

    gtk_label_set_text(GTK_LABEL(status_label),txt);
    g_free(txt);
    g_free(path);
    g_free(sz);
    g_list_free_full(sel,(GDestroyNotify)gtk_tree_path_free);
    g_free(free);
}

static void push_back(const char *p){ g_ptr_array_add(history_back,g_strdup(p)); }
static void push_forward(const char *p){ g_ptr_array_add(history_forward,g_strdup(p)); }

static char* pop_back(void){
    if(!history_back->len) return NULL;
    char *v = history_back->pdata[history_back->len-1];
    g_ptr_array_remove_index_fast(history_back,history_back->len-1);
    return v;
}

static char* pop_forward(void){
    if(!history_forward->len) return NULL;
    char *v = history_forward->pdata[history_forward->len-1];
    g_ptr_array_remove_index_fast(history_forward,history_forward->len-1);
    return v;
}

static void clear_forward(void){
    for(guint i=0;i<history_forward->len;i++)
        g_free(history_forward->pdata[i]);
    g_ptr_array_set_size(history_forward,0);
}

static void load_path(const char *path,gboolean hist){
    if(hist && current_path[0]) push_back(current_path);
    g_strlcpy(current_path,path,sizeof(current_path));
    gtk_entry_set_text(GTK_ENTRY(path_entry),current_path);
    ui_load_directory(GTK_ICON_VIEW(grid_view),current_path);
    update_status();
    if(hist) clear_forward();
}

static void on_back(GtkButton *b){
    char *p=pop_back(); if(!p) return;
    push_forward(current_path);
    load_path(p,FALSE);
    g_free(p);
}

static void on_forward(GtkButton *b){
    char *p=pop_forward(); if(!p) return;
    push_back(current_path);
    load_path(p,FALSE);
    g_free(p);
}

static void on_up(GtkButton *b){
    if(!strcmp(current_path,"/")) return;
    char p[4096]; g_strlcpy(p,current_path,sizeof(p));
    char *s=g_strrstr(p,"/");
    if(s==p) p[1]=0;
    else *s=0;
    load_path(p,TRUE);
}

static void on_path_enter(GtkEntry *e){
    const char *t=gtk_entry_get_text(e);
    if(g_file_test(t,G_FILE_TEST_IS_DIR))
        load_path(t,TRUE);
    else
        gtk_entry_set_text(GTK_ENTRY(path_entry),current_path);
}

static void on_search(GtkEntry *e){
    const char *q = gtk_entry_get_text(e);
    if(!q || !q[0]){
        ui_load_directory(GTK_ICON_VIEW(grid_view),current_path);
        update_status();
        return;
    }
    ui_filter_search(GTK_ICON_VIEW(grid_view),current_path,q);
    update_status();
}

static void open_with_default(const char *p){
    char cmd[6000];
    snprintf(cmd,sizeof(cmd),"xdg-open \"%s\" 2>/dev/null &",p);
    system(cmd);
}

static void on_item_activated(GtkIconView *v,GtkTreePath *p){
    GtkTreeModel *m = gtk_icon_view_get_model(v);
    GtkTreeIter it;
    if(!gtk_tree_model_get_iter(m,&it,p)) return;

    gchar *full=NULL; gboolean dir=FALSE;
    gtk_tree_model_get(m,&it,2,&full,3,&dir,-1);

    if(dir) load_path(full,TRUE);
    else open_with_default(full);

    g_free(full);
}

static gboolean get_sel(char **path,gboolean *dir){
    GList *l = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(grid_view));
    if(!l) return FALSE;

    GtkTreePath *tp=l->data;
    GtkTreeIter it;
    GtkTreeModel *m=gtk_icon_view_get_model(GTK_ICON_VIEW(grid_view));

    if(!gtk_tree_model_get_iter(m,&it,tp)) return FALSE;

    gtk_tree_model_get(m,&it,2,path,3,dir,-1);
    g_list_free_full(l,(GDestroyNotify)gtk_tree_path_free);
    return TRUE;
}

static void file_delete(const char *p){
    char c[6000]; snprintf(c,sizeof(c),"rm -rf \"%s\"",p);
    system(c);
    ui_load_directory(GTK_ICON_VIEW(grid_view),current_path);
    update_status();
}

static void do_copy(const char *p) {
    g_strlcpy(clipboard_path, p, sizeof(clipboard_path));
    clipboard_cut = FALSE;
}

static void do_cut(const char *p) {
    g_strlcpy(clipboard_path, p, sizeof(clipboard_path));
    clipboard_cut = TRUE;
}


static void file_paste(const char *dest){
    if(!clipboard_path[0]) return;

    char *base=g_path_get_basename(clipboard_path);
    char out[4096];
    snprintf(out,sizeof(out),"%s/%s",dest,base);

    char cmd[9000];
    if(clipboard_cut)
        snprintf(cmd,sizeof(cmd),"mv \"%s\" \"%s\"",clipboard_path,out);
    else
        snprintf(cmd,sizeof(cmd),"cp -r \"%s\" \"%s\"",clipboard_path,out);

    system(cmd);

    clipboard_cut=FALSE;
    clipboard_path[0]=0;
    g_free(base);

    ui_load_directory(GTK_ICON_VIEW(grid_view),current_path);
    update_status();
}

static void file_rename(GtkMenuItem *i,gpointer data){
    const char *old=data;

    char oldc[4096];
    g_strlcpy(oldc,old,sizeof(oldc));

    GtkWidget *d=gtk_dialog_new_with_buttons(
        "Rename",GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL,"Cancel",GTK_RESPONSE_CANCEL,"OK",
        GTK_RESPONSE_ACCEPT,NULL
    );

    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget *e=gtk_entry_new();

    gtk_entry_set_text(GTK_ENTRY(e),g_path_get_basename(oldc));
    gtk_box_pack_start(GTK_BOX(box),e,FALSE,FALSE,5);
    gtk_widget_show_all(d);

    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT){
        const char *nn=gtk_entry_get_text(GTK_ENTRY(e));
        char *dir=g_path_get_dirname(oldc);

        char np[4096];
        snprintf(np,sizeof(np),"%s/%s",dir,nn);

        rename(oldc,np);
        g_free(dir);
    }

    gtk_widget_destroy(d);
    ui_load_directory(GTK_ICON_VIEW(grid_view),current_path);
    update_status();
}

static gboolean on_right(GtkWidget *w,GdkEventButton *ev){
    if(ev->type!=GDK_BUTTON_PRESS || ev->button!=3) return FALSE;

    GtkWidget *m=gtk_menu_new();

    GtkWidget *o = gtk_menu_item_new_with_label("Open");
    GtkWidget *c = gtk_menu_item_new_with_label("Copy");
    GtkWidget *t = gtk_menu_item_new_with_label("Cut");
    GtkWidget *p = gtk_menu_item_new_with_label("Paste");
    GtkWidget *r = gtk_menu_item_new_with_label("Rename");
    GtkWidget *d = gtk_menu_item_new_with_label("Delete");

    gtk_menu_shell_append(GTK_MENU_SHELL(m),o);
    gtk_menu_shell_append(GTK_MENU_SHELL(m),c);
    gtk_menu_shell_append(GTK_MENU_SHELL(m),t);
    gtk_menu_shell_append(GTK_MENU_SHELL(m),p);
    gtk_menu_shell_append(GTK_MENU_SHELL(m),r);
    gtk_menu_shell_append(GTK_MENU_SHELL(m),d);
    gtk_widget_show_all(m);

    char *path=NULL; gboolean isd=FALSE;
    gboolean ok=get_sel(&path,&isd);

    if(ok){
        if(isd)
            g_signal_connect_swapped(o,"activate",
                G_CALLBACK(load_path),g_strdup(path));
        else
            g_signal_connect_swapped(o,"activate",
                G_CALLBACK(open_with_default),g_strdup(path));

        g_signal_connect_swapped(c, "activate",
            G_CALLBACK(do_copy), g_strdup(path));

        g_signal_connect_swapped(t, "activate",
            G_CALLBACK(do_cut), g_strdup(path));


        g_signal_connect(r,"activate",
            G_CALLBACK(file_rename),g_strdup(path));

        g_signal_connect_swapped(d,"activate",
            G_CALLBACK(file_delete),g_strdup(path));
    }

    g_signal_connect_swapped(p,"activate",
        G_CALLBACK(file_paste),g_strdup(current_path));

    gtk_menu_popup_at_pointer(GTK_MENU(m),(GdkEvent*)ev);

    g_free(path);
    return TRUE;
}

static void on_sudo(GtkToggleButton *b,gpointer d){
    sudo_mode = gtk_toggle_button_get_active(b);
    ui_load_directory(GTK_ICON_VIEW(grid_view),current_path);
    update_status();
}

static void sidebar_open(GtkButton *b,gpointer d){
    load_path(d,TRUE);
}

static GtkWidget* create_sidebar(void){
    GtkWidget *outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);

    sidebar_top=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    gtk_container_add(GTK_CONTAINER(scroll),sidebar_top);
    gtk_box_pack_start(GTK_BOX(outer),scroll,TRUE,TRUE,0);

    GtkWidget *bottom=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    GtkWidget *add=gtk_button_new_with_label("+ Add Shortcut");
    g_signal_connect(add,"clicked",G_CALLBACK(add_shortcut),NULL);
    gtk_box_pack_start(GTK_BOX(bottom),add,FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(outer),bottom,FALSE,FALSE,0);

    const char *H = g_get_home_dir();

    struct { const char *lbl; const char *path; } arr[]={
        {"Home",H},
        {"Desktop",g_strdup_printf("%s/Desktop",H)},
        {"Downloads",g_strdup_printf("%s/Downloads",H)},
        {"Root","/"}
    };

    for(int i=0;i<4;i++){
        GtkWidget *b=gtk_button_new_with_label(arr[i].lbl);
        g_signal_connect(b,"clicked",G_CALLBACK(sidebar_open),
            g_strdup(arr[i].path));
        gtk_box_pack_start(GTK_BOX(sidebar_top),b,FALSE,FALSE,2);
    }

    return outer;
}

static void add_shortcut(GtkButton *b){
    GtkWidget *d=gtk_file_chooser_dialog_new(
        "Select Folder",NULL,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Cancel",GTK_RESPONSE_CANCEL,
        "Add",GTK_RESPONSE_ACCEPT,NULL
    );

    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT){
        char *f=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        char *base=g_path_get_basename(f);

        char shortname[64];
        if(strlen(base)>14)
            snprintf(shortname,sizeof(shortname),"%.12s…",base);
        else snprintf(shortname,sizeof(shortname),"%s",base);

        GtkWidget *btn=gtk_button_new_with_label(shortname);
        g_signal_connect(btn,"clicked",
            G_CALLBACK(sidebar_open),g_strdup(f));

        gtk_box_pack_start(GTK_BOX(sidebar_top),btn,FALSE,FALSE,2);
        gtk_widget_show_all(sidebar_top);

        g_free(base);
    }

    gtk_widget_destroy(d);
}

static void on_theme(GtkComboBoxText *b){
    const char *t=gtk_combo_box_text_get_active_text(b);
    if(!t) return;

   
    char wo_file[256];
    snprintf(wo_file, sizeof(wo_file), "%s.wo", t);
    
    if (g_file_test(wo_file, G_FILE_TEST_EXISTS)) {
        load_theme(wo_file);
    } else {

        if(!strcmp(t,"OLED"))
            load_theme("oled.css");
        else if(!strcmp(t,"Red"))
            load_theme("red.css");
        else if(!strcmp(t,"Blue"))
            load_theme("blue.css");
        else {
            
            load_theme(wo_file);
        }
    }
}

static GtkWidget* create_statusbar(void){
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
    gtk_widget_set_name(box,"statusbar");

    status_label=gtk_label_new("Items:");
    gtk_label_set_xalign(GTK_LABEL(status_label),0.0);

    gtk_box_pack_start(GTK_BOX(box),status_label,FALSE,FALSE,8);

    return box;
}

GtkWidget* explorer_create_window(void){
    history_back=g_ptr_array_new();
    history_forward=g_ptr_array_new();

    g_strlcpy(current_path,g_get_home_dir(),sizeof(current_path));

    GtkWidget *w=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    enable_theme_drop(w);  
    main_window=w;

    gtk_window_set_title(GTK_WINDOW(w),"WO Files");
    gtk_window_set_default_size(GTK_WINDOW(w),1400,900);

    load_theme("oled.css");

    GtkWidget *hbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
    gtk_container_add(GTK_CONTAINER(w),hbox);

    GtkWidget *sidebar=create_sidebar();
    gtk_widget_set_name(sidebar,"sidebar");
    gtk_widget_set_size_request(sidebar,220,-1);
    gtk_box_pack_start(GTK_BOX(hbox),sidebar,FALSE,FALSE,0);

    GtkWidget *right=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    gtk_box_pack_start(GTK_BOX(hbox),right,TRUE,TRUE,0);

    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(right),bar,FALSE,FALSE,0);

    GtkWidget *theme_box=gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_box),"OLED");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_box),"Red");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_box),"Blue");
    

    load_saved_wo_themes(theme_box);
    
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_box),0);
    g_signal_connect(theme_box,"changed",G_CALLBACK(on_theme),NULL);

    GtkWidget *back=gtk_button_new_with_label("◀");
    GtkWidget *fwd =gtk_button_new_with_label("▶");
    GtkWidget *up  =gtk_button_new_with_label("⬆");
    GtkWidget *sudo_btn=gtk_toggle_button_new_with_label("SUDO");

    gtk_box_pack_start(GTK_BOX(bar),back,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(bar),fwd,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(bar),up,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(bar),sudo_btn,FALSE,FALSE,0);

    path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(path_entry),current_path);
    gtk_box_pack_start(GTK_BOX(bar),path_entry,TRUE,TRUE,0);

    search_entry=gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry),"Search…");
    gtk_box_pack_start(GTK_BOX(bar),search_entry,FALSE,FALSE,0);

    gtk_box_pack_end(GTK_BOX(bar),theme_box,FALSE,FALSE,0);

    grid_view = ui_create_grid();
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);

    gtk_container_add(GTK_CONTAINER(scroll),grid_view);
    gtk_box_pack_start(GTK_BOX(right),scroll,TRUE,TRUE,0);

    GtkWidget *status=create_statusbar();
    gtk_box_pack_start(GTK_BOX(right),status,FALSE,FALSE,0);

    ui_load_directory(GTK_ICON_VIEW(grid_view),current_path);
    update_status();

    g_signal_connect(back,"clicked",G_CALLBACK(on_back),NULL);
    g_signal_connect(fwd,"clicked",G_CALLBACK(on_forward),NULL);
    g_signal_connect(up,"clicked",G_CALLBACK(on_up),NULL);
    g_signal_connect(path_entry,"activate",G_CALLBACK(on_path_enter),NULL);
    g_signal_connect(search_entry,"changed",G_CALLBACK(on_search),NULL);
    g_signal_connect(grid_view,"item-activated",G_CALLBACK(on_item_activated),NULL);
    g_signal_connect(grid_view,"button-press-event",G_CALLBACK(on_right),NULL);
    g_signal_connect(sudo_btn,"toggled",G_CALLBACK(on_sudo),NULL);

    return w;
}

#include <gtk/gtk.h>
#include "explorer.h"

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *win = explorer_create_window();
    gtk_widget_show_all(win);

    gtk_main();
    return 0;
}

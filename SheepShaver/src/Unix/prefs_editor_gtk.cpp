/*
 *  prefs_editor_linux.cpp - Preferences editor, Linux implementation using GTK+
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"

#include <gtk/gtk.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>

#include "user_strings.h"
#include "version.h"
#include "cdrom.h"
#include "xpram.h"
#include "prefs.h"
#include "prefs_editor.h"

#define DEBUG 0
#include "debug.h"


// Global variables
static GtkWidget *win;              // Preferences window
static bool start_clicked = false;  // Return value of PrefsEditor() function
static int screen_width, screen_height; // Screen dimensions
static GMainLoop *main_loop;        // Main event loop


// Prototypes
static void create_volumes_pane(GtkWidget *top);
static void create_graphics_pane(GtkWidget *top);
static void create_input_pane(GtkWidget *top);
static void create_serial_pane(GtkWidget *top);
static void create_memory_pane(GtkWidget *top);
static void create_jit_pane(GtkWidget *top);
static void read_settings(void);


/*
 *  Utility functions
 */

struct opt_desc {
	int label_id;
	GCallback func;
};

struct combo_desc {
	int label_id;
};

// Strip GTK item factory path prefix to get display label
// e.g. "/_File" -> "File", "/File/_Start SheepShaver" -> "Start SheepShaver"
static const char *strip_menu_path(const char *path)
{
	const char *p = strrchr(path, '/');
	p = p ? p + 1 : path;
	if (*p == '_') p++;
	return p;
}

// Async file browse callback
static void on_browse_finish(GObject *source, GAsyncResult *result, gpointer user_data)
{
	GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
	GtkWidget *entry = GTK_WIDGET(user_data);
	GError *error = NULL;
	GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
	if (file) {
		char *path = g_file_get_path(file);
		if (path) {
			gtk_editable_set_text(GTK_EDITABLE(entry), path);
			g_free(path);
		}
		g_object_unref(file);
	}
	if (error) g_error_free(error);
}

static void cb_browse(GtkWidget *widget, void *user_data)
{
	GtkFileDialog *dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, GetString(STR_BROWSE_TITLE));
	gtk_file_dialog_open(dialog, GTK_WINDOW(win), NULL, on_browse_finish, user_data);
	g_object_unref(dialog);
}

static GtkWidget *make_browse_button(GtkWidget *entry)
{
	GtkWidget *button = gtk_button_new_with_label(GetString(STR_BROWSE_CTRL));
	g_signal_connect(button, "clicked", G_CALLBACK(cb_browse), entry);
	return button;
}

static GtkWidget *make_pane(GtkWidget *notebook, int title_id)
{
	GtkWidget *frame = gtk_frame_new(NULL);
	gtk_widget_set_margin_start(frame, 4);
	gtk_widget_set_margin_end(frame, 4);
	gtk_widget_set_margin_top(frame, 4);
	gtk_widget_set_margin_bottom(frame, 4);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start(box, 4);
	gtk_widget_set_margin_end(box, 4);
	gtk_widget_set_margin_top(box, 4);
	gtk_widget_set_margin_bottom(box, 4);
	gtk_frame_set_child(GTK_FRAME(frame), box);

	GtkWidget *label = gtk_label_new(GetString(title_id));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, label);
	return box;
}

static GtkWidget *make_button_box(GtkWidget *top, int border, const opt_desc *buttons)
{
	GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_margin_start(bb, border);
	gtk_widget_set_margin_end(bb, border);
	gtk_widget_set_margin_top(bb, border);
	gtk_widget_set_margin_bottom(bb, border);
	gtk_box_append(GTK_BOX(top), bb);

	while (buttons->label_id) {
		GtkWidget *button = gtk_button_new_with_label(GetString(buttons->label_id));
		g_signal_connect(button, "clicked", buttons->func, NULL);
		gtk_widget_set_hexpand(button, TRUE);
		gtk_box_append(GTK_BOX(bb), button);
		buttons++;
	}
	return bb;
}

static GtkWidget *make_separator(GtkWidget *top)
{
	GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_append(GTK_BOX(top), sep);
	return sep;
}

static GtkWidget *make_grid(GtkWidget *top)
{
	GtkWidget *grid = gtk_grid_new();
	gtk_box_append(GTK_BOX(top), grid);
	return grid;
}

// Create a label+combobox row in a grid. Returns the GtkComboBoxText widget.
static GtkWidget *grid_make_option_menu(GtkWidget *grid, int row, int label_id,
                                        const char **items, int num_items,
                                        int active, GCallback changed_cb)
{
	GtkWidget *label = gtk_label_new(GetString(label_id));
	gtk_widget_set_margin_start(label, 4);
	gtk_widget_set_margin_end(label, 4);
	gtk_widget_set_margin_top(label, 4);
	gtk_widget_set_margin_bottom(label, 4);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

	GtkWidget *combo = gtk_combo_box_text_new();
	for (int i = 0; i < num_items; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), items[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
	gtk_widget_set_hexpand(combo, TRUE);
	gtk_widget_set_margin_start(combo, 4);
	gtk_widget_set_margin_end(combo, 4);
	gtk_widget_set_margin_top(combo, 4);
	gtk_widget_set_margin_bottom(combo, 4);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, row, 1, 1);

	if (changed_cb)
		g_signal_connect(combo, "changed", changed_cb, NULL);

	return combo;
}

// Create a label+combobox-with-entry row. Returns the GtkComboBoxText widget.
static GtkWidget *grid_make_combobox(GtkWidget *grid, int row, int label_id,
                                     const char *default_value, GList *glist)
{
	GtkWidget *label = gtk_label_new(GetString(label_id));
	gtk_widget_set_margin_start(label, 4);
	gtk_widget_set_margin_end(label, 4);
	gtk_widget_set_margin_top(label, 4);
	gtk_widget_set_margin_bottom(label, 4);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

	GtkWidget *combo = gtk_combo_box_text_new_with_entry();
	for (GList *l = glist; l; l = l->next)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), (const char *)l->data);
	GtkWidget *entry = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));
	gtk_editable_set_text(GTK_EDITABLE(entry), default_value);
	gtk_widget_set_hexpand(combo, TRUE);
	gtk_widget_set_margin_start(combo, 4);
	gtk_widget_set_margin_end(combo, 4);
	gtk_widget_set_margin_top(combo, 4);
	gtk_widget_set_margin_bottom(combo, 4);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, row, 1, 1);

	return combo;
}

static GtkWidget *grid_make_combobox(GtkWidget *grid, int row, int label_id,
                                     const char *default_value, const combo_desc *options)
{
	GList *glist = NULL;
	while (options->label_id) {
		glist = g_list_append(glist, (void *)GetString(options->label_id));
		options++;
	}
	return grid_make_combobox(grid, row, label_id, default_value, glist);
}

static GtkWidget *grid_make_file_entry(GtkWidget *grid, int row, int label_id,
                                       const char *prefs_item, bool only_dirs = false)
{
	GtkWidget *label = gtk_label_new(GetString(label_id));
	gtk_widget_set_margin_start(label, 4);
	gtk_widget_set_margin_end(label, 4);
	gtk_widget_set_margin_top(label, 4);
	gtk_widget_set_margin_bottom(label, 4);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

	const char *str = PrefsFindString(prefs_item);
	if (str == NULL)
		str = "";

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_hexpand(box, TRUE);
	gtk_widget_set_margin_start(box, 4);
	gtk_widget_set_margin_end(box, 4);
	gtk_widget_set_margin_top(box, 4);
	gtk_widget_set_margin_bottom(box, 4);
	gtk_grid_attach(GTK_GRID(grid), box, 1, row, 1, 1);

	GtkWidget *entry = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(entry), str);
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_box_append(GTK_BOX(box), entry);

	GtkWidget *button = make_browse_button(entry);
	gtk_box_append(GTK_BOX(box), button);
	g_object_set_data(G_OBJECT(entry), "chooser_button", button);
	return entry;
}

// Create label+combobox in an hbox appended to top. Returns GtkComboBoxText.
static GtkWidget *make_option_menu(GtkWidget *top, int label_id,
                                   const char **items, int num_items,
                                   int active, GCallback changed_cb)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(top), box);

	GtkWidget *label = gtk_label_new(GetString(label_id));
	gtk_box_append(GTK_BOX(box), label);

	GtkWidget *combo = gtk_combo_box_text_new();
	for (int i = 0; i < num_items; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), items[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
	gtk_box_append(GTK_BOX(box), combo);

	if (changed_cb)
		g_signal_connect(combo, "changed", changed_cb, NULL);

	return combo;
}

static GtkWidget *make_entry(GtkWidget *top, int label_id, const char *prefs_item)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(top), box);

	GtkWidget *label = gtk_label_new(GetString(label_id));
	gtk_box_append(GTK_BOX(box), label);

	GtkWidget *entry = gtk_entry_new();
	const char *str = PrefsFindString(prefs_item);
	if (str == NULL)
		str = "";
	gtk_editable_set_text(GTK_EDITABLE(entry), str);
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_box_append(GTK_BOX(box), entry);
	return entry;
}

static const gchar *get_file_entry_path(GtkWidget *entry)
{
	return gtk_editable_get_text(GTK_EDITABLE(entry));
}

static GtkWidget *make_checkbox(GtkWidget *top, int label_id, const char *prefs_item, GCallback func)
{
	GtkWidget *button = gtk_check_button_new_with_label(GetString(label_id));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(button), PrefsFindBool(prefs_item));
	g_signal_connect(button, "toggled", func, NULL);
	gtk_box_append(GTK_BOX(top), button);
	return button;
}

static GtkWidget *make_checkbox(GtkWidget *top, int label_id, bool active, GCallback func)
{
	GtkWidget *button = gtk_check_button_new_with_label(GetString(label_id));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(button), active);
	g_signal_connect(button, "toggled", func, NULL);
	gtk_box_append(GTK_BOX(top), button);
	return button;
}


/*
 *  Show preferences editor
 *  Returns true when user clicked on "Start", false otherwise
 */

// Window destroyed
static void window_destroyed(GtkWidget *widget, gpointer user_data)
{
	g_main_loop_quit(main_loop);
}

// Shared action implementations
static void do_start(void)
{
	start_clicked = true;
	read_settings();
	SavePrefs();
	gtk_window_destroy(GTK_WINDOW(win));
}

static void do_quit(void)
{
	start_clicked = false;
	gtk_window_destroy(GTK_WINDOW(win));
}

// "Start" button clicked
static void cb_start(GtkWidget *widget, gpointer user_data)
{
	do_start();
}

// "Quit" button clicked
static void cb_quit(GtkWidget *widget, gpointer user_data)
{
	do_quit();
}

// GAction handlers for menu bar
static void action_start(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	do_start();
}

static void action_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	do_quit();
}

static void action_zap_pram(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ZapPRAM();
}

// "About" dialog
static void action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	char str[512];
	sprintf(str,
		"SheepShaver\nVersion %d.%d\n\n"
		"Copyright (C) 1997-2008 Christian Bauer and Marc Hellwig\n"
		"https://sheepshaver.cebix.net/\n\n"
		"SheepShaver comes with ABSOLUTELY NO\n"
		"WARRANTY. This is free software, and\n"
		"you are welcome to redistribute it\n"
		"under the terms of the GNU General\n"
		"Public License.\n",
		VERSION_MAJOR, VERSION_MINOR
	);

	GtkWidget *dialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dialog), GetString(STR_ABOUT_TITLE));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_window_set_child(GTK_WINDOW(dialog), box);

	GtkWidget *label = gtk_label_new(str);
	gtk_box_append(GTK_BOX(box), label);

	GtkWidget *button = gtk_button_new_with_label(GetString(STR_OK_BUTTON));
	g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
	gtk_box_append(GTK_BOX(box), button);

	gtk_window_present(GTK_WINDOW(dialog));
}

bool PrefsEditor(void)
{
	// Get screen dimensions
	GdkDisplay *display = gdk_display_get_default();
	GListModel *monitors = gdk_display_get_monitors(display);
	GdkMonitor *monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
	if (monitor) {
		GdkRectangle geometry;
		gdk_monitor_get_geometry(monitor, &geometry);
		screen_width = geometry.width;
		screen_height = geometry.height;
		g_object_unref(monitor);
	} else {
		screen_width = 1920;
		screen_height = 1080;
	}

	// Create window
	win = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(win), GetString(STR_PREFS_TITLE));
	g_signal_connect(win, "destroy", G_CALLBACK(window_destroyed), NULL);

	// Set up GAction group for menu bar
	static const GActionEntry action_entries[] = {
		{"start",    action_start,    NULL, NULL, NULL},
		{"quit",     action_quit,     NULL, NULL, NULL},
		{"zappram",  action_zap_pram, NULL, NULL, NULL},
		{"about",    action_about,    NULL, NULL, NULL},
	};
	GSimpleActionGroup *ag = g_simple_action_group_new();
	g_action_map_add_action_entries(G_ACTION_MAP(ag), action_entries,
	                                G_N_ELEMENTS(action_entries), NULL);
	gtk_widget_insert_action_group(win, "prefs", G_ACTION_GROUP(ag));

	// Build menu model
	GMenu *file_menu = g_menu_new();
	g_menu_append(file_menu, strip_menu_path(GetString(STR_PREFS_ITEM_START_GTK)),   "prefs.start");
	g_menu_append(file_menu, strip_menu_path(GetString(STR_PREFS_ITEM_ZAP_PRAM_GTK)), "prefs.zappram");
	g_menu_append(file_menu, strip_menu_path(GetString(STR_PREFS_ITEM_QUIT_GTK)),    "prefs.quit");

	GMenu *help_menu = g_menu_new();
	g_menu_append(help_menu, strip_menu_path(GetString(STR_HELP_ITEM_ABOUT_GTK)), "prefs.about");

	GMenu *menu_bar_model = g_menu_new();
	g_menu_append_submenu(menu_bar_model, strip_menu_path(GetString(STR_PREFS_MENU_FILE_GTK)),
	                      G_MENU_MODEL(file_menu));
	g_menu_append_submenu(menu_bar_model, strip_menu_path(GetString(STR_HELP_MENU_GTK)),
	                      G_MENU_MODEL(help_menu));
	g_object_unref(file_menu);
	g_object_unref(help_menu);

	GtkWidget *menu_bar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(menu_bar_model));
	g_object_unref(menu_bar_model);

	// Create window contents
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_window_set_child(GTK_WINDOW(win), box);

	gtk_box_append(GTK_BOX(box), menu_bar);

	GtkWidget *notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), FALSE);
	gtk_widget_set_vexpand(notebook, TRUE);
	gtk_box_append(GTK_BOX(box), notebook);

	create_volumes_pane(notebook);
	create_graphics_pane(notebook);
	create_input_pane(notebook);
	create_serial_pane(notebook);
	create_memory_pane(notebook);
	create_jit_pane(notebook);

	static const opt_desc buttons[] = {
		{STR_START_BUTTON, G_CALLBACK(cb_start)},
		{STR_QUIT_BUTTON,  G_CALLBACK(cb_quit)},
		{0, NULL}
	};
	make_button_box(box, 4, buttons);

	// Show window and enter main loop
	gtk_window_present(GTK_WINDOW(win));
	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);
	g_main_loop_unref(main_loop);
	main_loop = NULL;
	return start_clicked;
}


/*
 *  "Volumes" pane
 */

// Column indices for the volume list model
enum {
	VOL_COL_PATH = 0,   // full path (hidden, used for prefs)
	VOL_COL_LOCATION,   // displayed path
	VOL_COL_CDROM,      // gboolean: is a CD-ROM device
	VOL_COL_SIZE,       // human-readable size string
	VOL_N_COLS
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static GtkWidget     *volume_list;   // GtkTreeView
static GtkListStore  *volume_store;  // backing model
static GtkWidget *w_extfs;

// Add a volume path to the list
static void add_to_volume_list(const char *path)
{
	struct stat st;
	gboolean is_cdrom = FALSE;
	char size_str[64] = "";

	if (stat(path, &st) == 0) {
		if (S_ISBLK(st.st_mode)) {
			is_cdrom = TRUE;
		} else if (st.st_size > 0) {
			double sz = (double)st.st_size;
			if (sz >= 1024.0 * 1024.0 * 1024.0)
				snprintf(size_str, sizeof(size_str), "%.1f GB", sz / (1024.0 * 1024.0 * 1024.0));
			else
				snprintf(size_str, sizeof(size_str), "%.0f MB", sz / (1024.0 * 1024.0));
		}
	}

	GtkTreeIter iter;
	gtk_list_store_append(volume_store, &iter);
	gtk_list_store_set(volume_store, &iter,
		VOL_COL_PATH,     path,
		VOL_COL_LOCATION, path,
		VOL_COL_CDROM,    is_cdrom,
		VOL_COL_SIZE,     size_str,
		-1);
}

#pragma GCC diagnostic pop

// CD-ROM checkbox toggled by user
static void on_cdrom_toggled(GtkCellRendererToggle *renderer, gchar *path_str, gpointer user_data)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(volume_store), &iter, path_str)) {
		gboolean current;
		gtk_tree_model_get(GTK_TREE_MODEL(volume_store), &iter, VOL_COL_CDROM, &current, -1);
		gtk_list_store_set(volume_store, &iter, VOL_COL_CDROM, !current, -1);
	}
#pragma GCC diagnostic pop
}

// Async callback for add volume file dialog
static void on_add_volume_finish(GObject *source, GAsyncResult *result, gpointer user_data)
{
	GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
	GError *error = NULL;
	GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
	if (file) {
		char *path = g_file_get_path(file);
		if (path) {
			add_to_volume_list(path);
			g_free(path);
		}
		g_object_unref(file);
	}
	if (error) g_error_free(error);
}

// "Add Volume" button clicked
static void cb_add_volume(GtkWidget *widget, gpointer user_data)
{
	GtkFileDialog *dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, GetString(STR_ADD_VOLUME_TITLE));
	gtk_file_dialog_open(dialog, GTK_WINDOW(win), NULL, on_add_volume_finish, NULL);
	g_object_unref(dialog);
}

// Data for create-volume dialog
struct create_volume_data {
	GtkWidget *path_entry;
	GtkWidget *size_entry;
};

static void on_create_volume_browse_finish(GObject *source, GAsyncResult *result, gpointer user_data)
{
	GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
	GtkWidget *entry = GTK_WIDGET(user_data);
	GError *error = NULL;
	GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
	if (file) {
		char *path = g_file_get_path(file);
		if (path) {
			gtk_editable_set_text(GTK_EDITABLE(entry), path);
			g_free(path);
		}
		g_object_unref(file);
	}
	if (error) g_error_free(error);
}

static void cb_create_volume_browse(GtkWidget *button, gpointer user_data)
{
	GtkFileDialog *dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, GetString(STR_CREATE_VOLUME_TITLE));
	gtk_file_dialog_save(dialog, GTK_WINDOW(win), NULL, on_create_volume_browse_finish, user_data);
	g_object_unref(dialog);
}

static void cb_create_volume_ok(GtkWidget *button, gpointer user_data)
{
	GtkWidget *dlg = GTK_WIDGET(user_data);
	create_volume_data *data = (create_volume_data *)g_object_get_data(G_OBJECT(dlg), "cv_data");

	const char *file = gtk_editable_get_text(GTK_EDITABLE(data->path_entry));
	const char *str  = gtk_editable_get_text(GTK_EDITABLE(data->size_entry));
	int size = atoi(str);

	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "dd if=/dev/zero \"of=%s\" bs=1024k count=%d", file, size);
	int ret = system(cmd);
	if (ret == 0)
		add_to_volume_list(file);
	gtk_window_destroy(GTK_WINDOW(dlg));
}

static void on_create_dialog_destroy(GtkWidget *dlg, gpointer user_data)
{
	create_volume_data *data = (create_volume_data *)g_object_get_data(G_OBJECT(dlg), "cv_data");
	delete data;
}

// "Create Hardfile" button clicked
static void cb_create_volume(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *dlg = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dlg), GetString(STR_CREATE_VOLUME_TITLE));
	gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(vbox, 12);
	gtk_widget_set_margin_end(vbox, 12);
	gtk_widget_set_margin_top(vbox, 12);
	gtk_widget_set_margin_bottom(vbox, 12);
	gtk_window_set_child(GTK_WINDOW(dlg), vbox);

	// File path row
	GtkWidget *path_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(vbox), path_box);
	GtkWidget *path_label = gtk_label_new(GetString(STR_CREATE_VOLUME_TITLE));
	gtk_box_append(GTK_BOX(path_box), path_label);
	GtkWidget *path_entry = gtk_entry_new();
	gtk_widget_set_hexpand(path_entry, TRUE);
	gtk_box_append(GTK_BOX(path_box), path_entry);
	GtkWidget *browse_btn = gtk_button_new_with_label(GetString(STR_BROWSE_CTRL));
	g_signal_connect(browse_btn, "clicked", G_CALLBACK(cb_create_volume_browse), path_entry);
	gtk_box_append(GTK_BOX(path_box), browse_btn);

	// Size row
	GtkWidget *size_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(vbox), size_box);
	GtkWidget *size_label = gtk_label_new(GetString(STR_HARDFILE_SIZE_CTRL));
	gtk_box_append(GTK_BOX(size_box), size_label);
	GtkWidget *size_entry = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(size_entry), "40");
	gtk_box_append(GTK_BOX(size_box), size_entry);

	// Buttons
	GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(vbox), btn_box);
	GtkWidget *ok_btn = gtk_button_new_with_label(GetString(STR_OK_BUTTON));
	g_signal_connect(ok_btn, "clicked", G_CALLBACK(cb_create_volume_ok), dlg);
	gtk_box_append(GTK_BOX(btn_box), ok_btn);
	GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
	g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dlg);
	gtk_box_append(GTK_BOX(btn_box), cancel_btn);

	create_volume_data *data = new create_volume_data{path_entry, size_entry};
	g_object_set_data(G_OBJECT(dlg), "cv_data", data);
	g_signal_connect(dlg, "destroy", G_CALLBACK(on_create_dialog_destroy), NULL);

	gtk_window_present(GTK_WINDOW(dlg));
}

// "Remove Volume" button clicked
static void cb_remove_volume(GtkWidget *widget, gpointer user_data)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(volume_list));
	GtkTreeModel *model;
	GtkTreeIter iter;
	if (gtk_tree_selection_get_selected(sel, &model, &iter))
		gtk_list_store_remove(volume_store, &iter);
#pragma GCC diagnostic pop
}

// "Boot From" combo changed
static void on_bootdriver_changed(GtkComboBox *combo, gpointer user_data)
{
	switch (gtk_combo_box_get_active(combo)) {
		case 0: PrefsReplaceInt32("bootdriver", 0); break;
		case 1: PrefsReplaceInt32("bootdriver", CDROMRefNum); break;
	}
}

// "No CD-ROM Driver" button toggled
static void tb_nocdrom(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("nocdrom", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}

// Read settings from widgets and set preferences
static void read_volumes_settings(void)
{
	while (PrefsFindString("disk"))
		PrefsRemoveItem("disk");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	GtkTreeModel *model = GTK_TREE_MODEL(volume_store);
	GtkTreeIter iter;
	gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
	while (valid) {
		char *path = NULL;
		gtk_tree_model_get(model, &iter, VOL_COL_PATH, &path, -1);
		if (path) {
			PrefsAddString("disk", path);
			g_free(path);
		}
		valid = gtk_tree_model_iter_next(model, &iter);
	}
#pragma GCC diagnostic pop

	PrefsReplaceString("extfs", gtk_editable_get_text(GTK_EDITABLE(w_extfs)));
}

// Create "Volumes" pane
static void create_volumes_pane(GtkWidget *top)
{
	GtkWidget *box = make_pane(top, STR_VOLUMES_PANE_TITLE);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	// Create the list store: path(hidden), location, cdrom, size
	volume_store = gtk_list_store_new(VOL_N_COLS,
		G_TYPE_STRING,   // VOL_COL_PATH
		G_TYPE_STRING,   // VOL_COL_LOCATION
		G_TYPE_BOOLEAN,  // VOL_COL_CDROM
		G_TYPE_STRING);  // VOL_COL_SIZE

	volume_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(volume_store));
	g_object_unref(volume_store);  // view holds the reference now
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(volume_list), TRUE);

	// Location column
	GtkCellRenderer *loc_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *loc_col = gtk_tree_view_column_new_with_attributes(
		"Location", loc_renderer, "text", VOL_COL_LOCATION, NULL);
	gtk_tree_view_column_set_expand(loc_col, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(volume_list), loc_col);

	// CD-ROM column (checkbox, user-toggleable)
	GtkCellRenderer *toggle_renderer = gtk_cell_renderer_toggle_new();
	g_object_set(toggle_renderer, "activatable", TRUE, NULL);
	g_signal_connect(toggle_renderer, "toggled", G_CALLBACK(on_cdrom_toggled), NULL);
	GtkTreeViewColumn *cdrom_col = gtk_tree_view_column_new_with_attributes(
		"CD-ROM", toggle_renderer, "active", VOL_COL_CDROM, NULL);
	gtk_tree_view_column_set_sizing(cdrom_col, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width(cdrom_col, 80);
	gtk_tree_view_append_column(GTK_TREE_VIEW(volume_list), cdrom_col);

	// Size column
	GtkCellRenderer *size_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *size_col = gtk_tree_view_column_new_with_attributes(
		"Size", size_renderer, "text", VOL_COL_SIZE, NULL);
	gtk_tree_view_column_set_sizing(size_col, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width(size_col, 100);
	gtk_tree_view_append_column(GTK_TREE_VIEW(volume_list), size_col);
#pragma GCC diagnostic pop

	char *str;
	int32 index = 0;
	while ((str = (char *)PrefsFindString("disk", index++)) != NULL)
		add_to_volume_list(str);

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), volume_list);
	gtk_box_append(GTK_BOX(box), scroll);

	static const opt_desc buttons[] = {
		{STR_ADD_VOLUME_BUTTON,    G_CALLBACK(cb_add_volume)},
		{STR_CREATE_VOLUME_BUTTON, G_CALLBACK(cb_create_volume)},
		{STR_REMOVE_VOLUME_BUTTON, G_CALLBACK(cb_remove_volume)},
		{0, NULL},
	};
	make_button_box(box, 0, buttons);
	make_separator(box);

	w_extfs = make_entry(box, STR_EXTFS_CTRL, "extfs");

	int bootdriver = PrefsFindInt32("bootdriver"), active = 0;
	if (bootdriver == CDROMRefNum) active = 1;
	const char *boot_items[] = {GetString(STR_BOOT_ANY_LAB), GetString(STR_BOOT_CDROM_LAB)};
	make_option_menu(box, STR_BOOTDRIVER_CTRL, boot_items, 2, active,
	                 G_CALLBACK(on_bootdriver_changed));

	make_checkbox(box, STR_NOCDROM_CTRL, "nocdrom", G_CALLBACK(tb_nocdrom));
}


/*
 *  "JIT Compiler" pane
 */

// Are we running a JIT capable CPU?
static bool is_jit_capable(void)
{
#if USE_JIT
	return true;
#elif defined __APPLE__ && defined __MACH__
	// XXX run-time detect so that we can use a PPC GUI prefs editor
	static char cpu[10];
	if (cpu[0] == 0) {
		FILE *fp = popen("uname -p", "r");
		if (fp == NULL)
			return false;
		fgets(cpu, sizeof(cpu) - 1, fp);
		fclose(fp);
	}
	if (cpu[0] == 'i' && cpu[2] == '8' && cpu[3] == '6') // XXX assuming i?86
		return true;
#endif
	return false;
}

// Set sensitivity of widgets
static void set_jit_sensitive(void)
{
	const bool jit_enabled = PrefsFindBool("jit");
	(void)jit_enabled;
}

// "Use JIT Compiler" button toggled
static void tb_jit(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("jit", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
	set_jit_sensitive();
}

// Read settings from widgets and set preferences
static void read_jit_settings(void)
{
	bool jit_enabled = is_jit_capable() && PrefsFindBool("jit");
	(void)jit_enabled;
}

// "Use built-in 68k DR emulator" button toggled
static void tb_jit_68k(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("jit68k", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}

// Create "JIT Compiler" pane
static void create_jit_pane(GtkWidget *top)
{
	GtkWidget *box = make_pane(top, STR_JIT_PANE_TITLE);

	if (is_jit_capable()) {
		make_checkbox(box, STR_JIT_CTRL, "jit", G_CALLBACK(tb_jit));
		set_jit_sensitive();
	}

	make_checkbox(box, STR_JIT_68K_CTRL, "jit68k", G_CALLBACK(tb_jit_68k));
}


/*
 *  "Graphics/Sound" pane
 */

// Display types
enum {
	DISPLAY_WINDOW,
	DISPLAY_SCREEN
};

static GtkWidget *w_frameskip, *w_display_x, *w_display_y;
static GtkWidget *l_frameskip, *l_display_x, *l_display_y;
static int display_type;
static int dis_width, dis_height;
static bool is_fbdev_dga_mode = false;

static GtkWidget *w_dspdevice_file, *w_mixerdevice_file;

// Hide/show graphics widgets
static void hide_show_graphics_widgets(void)
{
	switch (display_type) {
		case DISPLAY_WINDOW:
			gtk_widget_set_visible(w_frameskip, TRUE);
			gtk_widget_set_visible(l_frameskip, TRUE);
			break;
		case DISPLAY_SCREEN:
			gtk_widget_set_visible(w_frameskip, FALSE);
			gtk_widget_set_visible(l_frameskip, FALSE);
			break;
	}
}

// Video type combo changed
static void on_video_type_changed(GtkComboBox *combo, gpointer user_data)
{
	display_type = gtk_combo_box_get_active(combo); // 0=WINDOW, 1=SCREEN
	hide_show_graphics_widgets();
}

// Frameskip combo changed
static void on_frameskip_changed(GtkComboBox *combo, gpointer user_data)
{
	static const int values[] = {12, 8, 6, 4, 2, 1};
	int active = gtk_combo_box_get_active(combo);
	if (active >= 0 && active < (int)(sizeof(values) / sizeof(values[0])))
		PrefsReplaceInt32("frameskip", values[active]);
}

// QuickDraw acceleration
static void tb_gfxaccel(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("gfxaccel", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}

// Set sensitivity of widgets
static void set_graphics_sensitive(void)
{
	const bool sound_enabled = !PrefsFindBool("nosound");
	gtk_widget_set_sensitive(w_dspdevice_file, sound_enabled);
	gtk_widget_set_sensitive(w_mixerdevice_file, sound_enabled);
}

// "Disable Sound Output" button toggled
static void tb_nosound(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("nosound", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
	set_graphics_sensitive();
}

// Read and convert graphics preferences
static void parse_graphics_prefs(void)
{
	display_type = DISPLAY_WINDOW;
	dis_width = 640;
	dis_height = 480;

	const char *str = PrefsFindString("screen");
	if (str) {
		if (sscanf(str, "win/%d/%d", &dis_width, &dis_height) == 2)
			display_type = DISPLAY_WINDOW;
		else if (sscanf(str, "dga/%d/%d", &dis_width, &dis_height) == 2)
			display_type = DISPLAY_SCREEN;
#ifdef ENABLE_FBDEV_DGA
		else if (sscanf(str, "fbdev/%d/%d", &dis_width, &dis_height) == 2) {
			is_fbdev_dga_mode = true;
			display_type = DISPLAY_SCREEN;
		}
#endif
	}
	else {
		uint32 window_modes = PrefsFindInt32("windowmodes");
		uint32 screen_modes = PrefsFindInt32("screenmodes");
		if (screen_modes) {
			display_type = DISPLAY_SCREEN;
			static const struct {
				int id;
				int width;
				int height;
			}
			modes[] = {
				{  1,	 640,	 480 },
				{  2,	 800,	 600 },
				{  4,	1024,	 768 },
				{ 64,	1152,	 768 },
				{  8,	1152,	 900 },
				{ 16,	1280,	1024 },
				{ 32,	1600,	1200 },
				{ 0, }
			};
			for (int i = 0; modes[i].id != 0; i++) {
				if (screen_modes & modes[i].id) {
					if (modes[i].width <= screen_width && modes[i].height <= screen_height) {
						dis_width = modes[i].width;
						dis_height = modes[i].height;
					}
				}
			}
		}
		else if (window_modes) {
			display_type = DISPLAY_WINDOW;
			if (window_modes & 1)
				dis_width = 640, dis_height = 480;
			if (window_modes & 2)
				dis_width = 800, dis_height = 600;
		}
	}
	if (dis_width == screen_width)
		dis_width = 0;
	if (dis_height == screen_height)
		dis_height = 0;
}

// Read settings from widgets and set preferences
static void read_graphics_settings(void)
{
	const char *str;

	str = gtk_editable_get_text(GTK_EDITABLE(w_display_x));
	dis_width = atoi(str);

	str = gtk_editable_get_text(GTK_EDITABLE(w_display_y));
	dis_height = atoi(str);

	char pref[256];
	bool use_screen_mode = true;
	switch (display_type) {
		case DISPLAY_WINDOW:
			sprintf(pref, "win/%d/%d", dis_width, dis_height);
			break;
		case DISPLAY_SCREEN:
			sprintf(pref, "dga/%d/%d", dis_width, dis_height);
			break;
		default:
			use_screen_mode = false;
			PrefsRemoveItem("screen");
			return;
	}
	if (use_screen_mode) {
		PrefsReplaceString("screen", pref);
		// Old prefs are now migrated
		PrefsRemoveItem("windowmodes");
		PrefsRemoveItem("screenmodes");
	}

	PrefsReplaceString("dsp", get_file_entry_path(w_dspdevice_file));
	PrefsReplaceString("mixer", get_file_entry_path(w_mixerdevice_file));
}

// Create "Graphics/Sound" pane
static void create_graphics_pane(GtkWidget *top)
{
	GtkWidget *box, *grid, *combo;
	char str[32];

	parse_graphics_prefs();

	box = make_pane(top, STR_GRAPHICS_SOUND_PANE_TITLE);
	grid = make_grid(box);

	// Video type row (row 0)
	GtkWidget *vtype_label = gtk_label_new(GetString(STR_VIDEO_TYPE_CTRL));
	gtk_widget_set_halign(vtype_label, GTK_ALIGN_START);
	gtk_widget_set_margin_start(vtype_label, 4);
	gtk_widget_set_margin_end(vtype_label, 4);
	gtk_widget_set_margin_top(vtype_label, 4);
	gtk_widget_set_margin_bottom(vtype_label, 4);
	gtk_grid_attach(GTK_GRID(grid), vtype_label, 0, 0, 1, 1);

	GtkWidget *vtype_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(vtype_combo), GetString(STR_WINDOW_CTRL));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(vtype_combo), GetString(STR_FULLSCREEN_CTRL));
	gtk_combo_box_set_active(GTK_COMBO_BOX(vtype_combo), display_type == DISPLAY_SCREEN ? 1 : 0);
	gtk_widget_set_hexpand(vtype_combo, TRUE);
	gtk_widget_set_margin_start(vtype_combo, 4);
	gtk_widget_set_margin_end(vtype_combo, 4);
	gtk_widget_set_margin_top(vtype_combo, 4);
	gtk_widget_set_margin_bottom(vtype_combo, 4);
	gtk_grid_attach(GTK_GRID(grid), vtype_combo, 1, 0, 1, 1);
	g_signal_connect(vtype_combo, "changed", G_CALLBACK(on_video_type_changed), NULL);

	// Frameskip row (row 1)
	l_frameskip = gtk_label_new(GetString(STR_FRAMESKIP_CTRL));
	gtk_widget_set_halign(l_frameskip, GTK_ALIGN_START);
	gtk_widget_set_margin_start(l_frameskip, 4);
	gtk_widget_set_margin_end(l_frameskip, 4);
	gtk_widget_set_margin_top(l_frameskip, 4);
	gtk_widget_set_margin_bottom(l_frameskip, 4);
	gtk_grid_attach(GTK_GRID(grid), l_frameskip, 0, 1, 1, 1);

	w_frameskip = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w_frameskip), GetString(STR_REF_5HZ_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w_frameskip), GetString(STR_REF_7_5HZ_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w_frameskip), GetString(STR_REF_10HZ_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w_frameskip), GetString(STR_REF_15HZ_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w_frameskip), GetString(STR_REF_30HZ_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w_frameskip), GetString(STR_REF_60HZ_LAB));
	int frameskip = PrefsFindInt32("frameskip");
	int fs_item = 5; // default 60Hz
	switch (frameskip) {
		case 12: fs_item = 0; break;
		case 8:  fs_item = 1; break;
		case 6:  fs_item = 2; break;
		case 4:  fs_item = 3; break;
		case 2:  fs_item = 4; break;
		case 1:
		case 0:  fs_item = 5; break;
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(w_frameskip), fs_item);
	gtk_widget_set_hexpand(w_frameskip, TRUE);
	gtk_widget_set_margin_start(w_frameskip, 4);
	gtk_widget_set_margin_end(w_frameskip, 4);
	gtk_widget_set_margin_top(w_frameskip, 4);
	gtk_widget_set_margin_bottom(w_frameskip, 4);
	gtk_grid_attach(GTK_GRID(grid), w_frameskip, 1, 1, 1, 1);
	g_signal_connect(w_frameskip, "changed", G_CALLBACK(on_frameskip_changed), NULL);

	// Display width row (row 2)
	l_display_x = gtk_label_new(GetString(STR_DISPLAY_X_CTRL));
	gtk_widget_set_halign(l_display_x, GTK_ALIGN_START);
	gtk_widget_set_margin_start(l_display_x, 4);
	gtk_widget_set_margin_end(l_display_x, 4);
	gtk_widget_set_margin_top(l_display_x, 4);
	gtk_widget_set_margin_bottom(l_display_x, 4);
	gtk_grid_attach(GTK_GRID(grid), l_display_x, 0, 2, 1, 1);

	combo = gtk_combo_box_text_new_with_entry();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_512_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_640_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_800_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_1024_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_MAX_LAB));
	if (dis_width)
		sprintf(str, "%d", dis_width);
	else
		strcpy(str, GetString(STR_SIZE_MAX_LAB));
	w_display_x = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));
	gtk_editable_set_text(GTK_EDITABLE(w_display_x), str);
	gtk_widget_set_hexpand(combo, TRUE);
	gtk_widget_set_margin_start(combo, 4);
	gtk_widget_set_margin_end(combo, 4);
	gtk_widget_set_margin_top(combo, 4);
	gtk_widget_set_margin_bottom(combo, 4);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, 2, 1, 1);

	// Display height row (row 3)
	l_display_y = gtk_label_new(GetString(STR_DISPLAY_Y_CTRL));
	gtk_widget_set_halign(l_display_y, GTK_ALIGN_START);
	gtk_widget_set_margin_start(l_display_y, 4);
	gtk_widget_set_margin_end(l_display_y, 4);
	gtk_widget_set_margin_top(l_display_y, 4);
	gtk_widget_set_margin_bottom(l_display_y, 4);
	gtk_grid_attach(GTK_GRID(grid), l_display_y, 0, 3, 1, 1);

	combo = gtk_combo_box_text_new_with_entry();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_384_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_480_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_600_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_768_LAB));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), GetString(STR_SIZE_MAX_LAB));
	if (dis_height)
		sprintf(str, "%d", dis_height);
	else
		strcpy(str, GetString(STR_SIZE_MAX_LAB));
	w_display_y = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));
	gtk_editable_set_text(GTK_EDITABLE(w_display_y), str);
	gtk_widget_set_hexpand(combo, TRUE);
	gtk_widget_set_margin_start(combo, 4);
	gtk_widget_set_margin_end(combo, 4);
	gtk_widget_set_margin_top(combo, 4);
	gtk_widget_set_margin_bottom(combo, 4);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, 3, 1, 1);

	make_checkbox(box, STR_GFXACCEL_CTRL, PrefsFindBool("gfxaccel"), G_CALLBACK(tb_gfxaccel));

	make_separator(box);
	make_checkbox(box, STR_NOSOUND_CTRL, "nosound", G_CALLBACK(tb_nosound));
	w_dspdevice_file = make_entry(box, STR_DSPDEVICE_FILE_CTRL, "dsp");
	w_mixerdevice_file = make_entry(box, STR_MIXERDEVICE_FILE_CTRL, "mixer");

	set_graphics_sensitive();
	hide_show_graphics_widgets();
}


/*
 *  "Input" pane
 */

static GtkWidget *w_keycode_file;
static GtkWidget *w_mouse_wheel_lines;

// Set sensitivity of widgets
static void set_input_sensitive(void)
{
	const bool use_keycodes = PrefsFindBool("keycodes");
	gtk_widget_set_sensitive(w_keycode_file, use_keycodes);
	gtk_widget_set_sensitive(GTK_WIDGET(g_object_get_data(G_OBJECT(w_keycode_file), "chooser_button")), use_keycodes);
	gtk_widget_set_sensitive(w_mouse_wheel_lines, PrefsFindInt32("mousewheelmode") == 1);
}

// "Use Raw Keycodes" button toggled
static void tb_keycodes(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("keycodes", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
	set_input_sensitive();
}

// Mouse wheel mode combo changed
static void on_wheelmode_changed(GtkComboBox *combo, gpointer user_data)
{
	PrefsReplaceInt32("mousewheelmode", gtk_combo_box_get_active(combo));
	set_input_sensitive();
}

// Read settings from widgets and set preferences
static void read_input_settings(void)
{
	const char *str = get_file_entry_path(w_keycode_file);
	if (str && strlen(str))
		PrefsReplaceString("keycodefile", str);
	else
		PrefsRemoveItem("keycodefile");

	PrefsReplaceInt32("mousewheellines", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w_mouse_wheel_lines)));
}

// Create "Input" pane
static void create_input_pane(GtkWidget *top)
{
	GtkWidget *box, *hbox, *label, *button;

	box = make_pane(top, STR_INPUT_PANE_TITLE);

	make_checkbox(box, STR_KEYCODES_CTRL, "keycodes", G_CALLBACK(tb_keycodes));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(box), hbox);

	label = gtk_label_new(GetString(STR_KEYCODES_CTRL));
	gtk_box_append(GTK_BOX(hbox), label);

	const char *str = PrefsFindString("keycodefile");
	if (str == NULL)
		str = "";

	w_keycode_file = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(w_keycode_file), str);
	gtk_widget_set_hexpand(w_keycode_file, TRUE);
	gtk_box_append(GTK_BOX(hbox), w_keycode_file);

	button = make_browse_button(w_keycode_file);
	gtk_box_append(GTK_BOX(hbox), button);
	g_object_set_data(G_OBJECT(w_keycode_file), "chooser_button", button);

	make_separator(box);

	int wheelmode = PrefsFindInt32("mousewheelmode"), wactive = 0;
	if (wheelmode == 1) wactive = 1;
	const char *wheel_items[] = {GetString(STR_MOUSEWHEELMODE_PAGE_LAB), GetString(STR_MOUSEWHEELMODE_CURSOR_LAB)};
	make_option_menu(box, STR_MOUSEWHEELMODE_CTRL, wheel_items, 2, wactive,
	                 G_CALLBACK(on_wheelmode_changed));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(box), hbox);

	label = gtk_label_new(GetString(STR_MOUSEWHEELLINES_CTRL));
	gtk_box_append(GTK_BOX(hbox), label);

	GtkAdjustment *adj = gtk_adjustment_new(PrefsFindInt32("mousewheellines"), 1, 1000, 1, 5, 0);
	w_mouse_wheel_lines = gtk_spin_button_new(adj, 0.0, 0);
	gtk_box_append(GTK_BOX(hbox), w_mouse_wheel_lines);

	set_input_sensitive();
}


/*
 *  "Serial/Network" pane
 */

static GtkWidget *w_seriala, *w_serialb, *w_ether;

// Read settings from widgets and set preferences
static void read_serial_settings(void)
{
	const char *str;

	str = gtk_editable_get_text(GTK_EDITABLE(w_seriala));
	PrefsReplaceString("seriala", str);

	str = gtk_editable_get_text(GTK_EDITABLE(w_serialb));
	PrefsReplaceString("serialb", str);

	str = gtk_editable_get_text(GTK_EDITABLE(w_ether));
	if (str && strlen(str))
		PrefsReplaceString("ether", str);
	else
		PrefsRemoveItem("ether");
}

// Add names of serial devices
static gint gl_str_cmp(gconstpointer a, gconstpointer b)
{
	return strcmp((char *)a, (char *)b);
}

static GList *add_serial_names(void)
{
	GList *glist = NULL;

	// Search /dev for ttyS* and lp*
	DIR *d = opendir("/dev");
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
#if defined(__linux__)
			if (strncmp(de->d_name, "ttyS", 4) == 0 || strncmp(de->d_name, "lp", 2) == 0) {
#elif defined(__FreeBSD__)
			if (strncmp(de->d_name, "cuaa", 4) == 0 || strncmp(de->d_name, "lpt", 3) == 0) {
#elif defined(__NetBSD__)
			if (strncmp(de->d_name, "tty0", 4) == 0 || strncmp(de->d_name, "lpt", 3) == 0) {
#elif defined(sgi)
			if (strncmp(de->d_name, "ttyf", 4) == 0 || strncmp(de->d_name, "plp", 3) == 0) {
#else
			if (false) {
#endif
				char *str = new char[64];
				sprintf(str, "/dev/%s", de->d_name);
				glist = g_list_append(glist, str);
			}
		}
		closedir(d);
	}
	if (glist)
		g_list_sort(glist, gl_str_cmp);
	else
		glist = g_list_append(glist, (void *)"<none>");
	return glist;
}

// Add names of ethernet interfaces
static GList *add_ether_names(void)
{
	GList *glist = NULL;

	// Get list of all Ethernet interfaces
	int s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		char inbuf[8192];
		struct ifconf ifc;
		ifc.ifc_len = sizeof(inbuf);
		ifc.ifc_buf = inbuf;
		if (ioctl(s, SIOCGIFCONF, &ifc) == 0) {
			struct ifreq req, *ifr = ifc.ifc_req;
			for (int i=0; i<ifc.ifc_len; i+=sizeof(ifreq), ifr++) {
				req = *ifr;
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(sgi)
				if (ioctl(s, SIOCGIFADDR, &req) == 0 && (req.ifr_addr.sa_family == ARPHRD_ETHER || req.ifr_addr.sa_family == ARPHRD_ETHER+1)) {
#elif defined(__linux__)
				if (ioctl(s, SIOCGIFHWADDR, &req) == 0 && req.ifr_hwaddr.sa_family == ARPHRD_ETHER) {
#else
				if (false) {
#endif
					char *str = new char[64];
					strncpy(str, ifr->ifr_name, 63);
					glist = g_list_append(glist, str);
				}
			}
		}
		close(s);
	}
#ifdef HAVE_SLIRP
	static char s_slirp[] = "slirp";
	glist = g_list_append(glist, s_slirp);
#endif
	if (glist)
		g_list_sort(glist, gl_str_cmp);
	else
		glist = g_list_append(glist, (void *)"<none>");
	return glist;
}

// Create "Serial/Network" pane
static void create_serial_pane(GtkWidget *top)
{
	GtkWidget *box, *grid, *combo;
	GList *glist = add_serial_names();

	box = make_pane(top, STR_SERIAL_NETWORK_PANE_TITLE);
	grid = make_grid(box);

	// Serial port A (row 0)
	GtkWidget *label = gtk_label_new(GetString(STR_SERPORTA_CTRL));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_margin_start(label, 4);
	gtk_widget_set_margin_end(label, 4);
	gtk_widget_set_margin_top(label, 4);
	gtk_widget_set_margin_bottom(label, 4);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

	combo = gtk_combo_box_text_new_with_entry();
	for (GList *l = glist; l; l = l->next)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), (const char *)l->data);
	const char *str = PrefsFindString("seriala");
	if (str == NULL) str = "";
	w_seriala = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));
	gtk_editable_set_text(GTK_EDITABLE(w_seriala), str);
	gtk_widget_set_hexpand(combo, TRUE);
	gtk_widget_set_margin_start(combo, 4);
	gtk_widget_set_margin_end(combo, 4);
	gtk_widget_set_margin_top(combo, 4);
	gtk_widget_set_margin_bottom(combo, 4);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, 0, 1, 1);

	// Serial port B (row 1)
	label = gtk_label_new(GetString(STR_SERPORTB_CTRL));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_margin_start(label, 4);
	gtk_widget_set_margin_end(label, 4);
	gtk_widget_set_margin_top(label, 4);
	gtk_widget_set_margin_bottom(label, 4);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);

	combo = gtk_combo_box_text_new_with_entry();
	for (GList *l = glist; l; l = l->next)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), (const char *)l->data);
	str = PrefsFindString("serialb");
	if (str == NULL) str = "";
	w_serialb = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));
	gtk_editable_set_text(GTK_EDITABLE(w_serialb), str);
	gtk_widget_set_hexpand(combo, TRUE);
	gtk_widget_set_margin_start(combo, 4);
	gtk_widget_set_margin_end(combo, 4);
	gtk_widget_set_margin_top(combo, 4);
	gtk_widget_set_margin_bottom(combo, 4);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, 1, 1, 1);

	// Ethernet interface (row 2)
	label = gtk_label_new(GetString(STR_ETHERNET_IF_CTRL));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_margin_start(label, 4);
	gtk_widget_set_margin_end(label, 4);
	gtk_widget_set_margin_top(label, 4);
	gtk_widget_set_margin_bottom(label, 4);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);

	glist = add_ether_names();
	combo = gtk_combo_box_text_new_with_entry();
	for (GList *l = glist; l; l = l->next)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), (const char *)l->data);
	str = PrefsFindString("ether");
	if (str == NULL) str = "";
	w_ether = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));
	gtk_editable_set_text(GTK_EDITABLE(w_ether), str);
	gtk_widget_set_hexpand(combo, TRUE);
	gtk_widget_set_margin_start(combo, 4);
	gtk_widget_set_margin_end(combo, 4);
	gtk_widget_set_margin_top(combo, 4);
	gtk_widget_set_margin_bottom(combo, 4);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, 2, 1, 1);
}


/*
 *  "Memory/Misc" pane
 */

static GtkWidget *w_ramsize;
static GtkWidget *w_rom_file;

// Don't use CPU when idle?
static void tb_idlewait(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("idlewait", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}

// "Ignore SEGV" button toggled
static void tb_ignoresegv(GtkWidget *widget, gpointer user_data)
{
	PrefsReplaceBool("ignoresegv", gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}

// Read settings from widgets and set preferences
static void read_memory_settings(void)
{
	GtkWidget *entry = gtk_combo_box_get_child(GTK_COMBO_BOX(w_ramsize));
	const char *str = gtk_editable_get_text(GTK_EDITABLE(entry));
	PrefsReplaceInt32("ramsize", atoi(str) << 20);

	str = gtk_editable_get_text(GTK_EDITABLE(w_rom_file));
	if (str && strlen(str))
		PrefsReplaceString("rom", str);
	else
		PrefsRemoveItem("rom");
}

// Create "Memory/Misc" pane
static void create_memory_pane(GtkWidget *top)
{
	GtkWidget *box, *grid;

	box = make_pane(top, STR_MEMORY_MISC_PANE_TITLE);
	grid = make_grid(box);

	static const combo_desc options[] = {
		STR_RAMSIZE_4MB_LAB,
		STR_RAMSIZE_8MB_LAB,
		STR_RAMSIZE_16MB_LAB,
		STR_RAMSIZE_32MB_LAB,
		STR_RAMSIZE_64MB_LAB,
		STR_RAMSIZE_128MB_LAB,
		STR_RAMSIZE_256MB_LAB,
		STR_RAMSIZE_512MB_LAB,
		STR_RAMSIZE_1024MB_LAB,
		0
	};
	char default_ramsize[16];
	sprintf(default_ramsize, "%d", PrefsFindInt32("ramsize") >> 20);
	w_ramsize = grid_make_combobox(grid, 0, STR_RAMSIZE_CTRL, default_ramsize, options);

	w_rom_file = grid_make_file_entry(grid, 1, STR_ROM_FILE_CTRL, "rom");

	make_checkbox(box, STR_IGNORESEGV_CTRL, "ignoresegv", G_CALLBACK(tb_ignoresegv));
	make_checkbox(box, STR_IDLEWAIT_CTRL, "idlewait", G_CALLBACK(tb_idlewait));
}


/*
 *  Read settings from widgets and set preferences
 */

static void read_settings(void)
{
	read_volumes_settings();
	read_graphics_settings();
	read_input_settings();
	read_serial_settings();
	read_memory_settings();
	read_jit_settings();
}


#ifdef STANDALONE_GUI
#include <errno.h>
#include <sys/wait.h>
#include "rpc.h"

/*
 *  Fake unused data and functions
 */

uint8 XPRAM[XPRAM_SIZE];
void MountVolume(void *fh) { }
void FileDiskLayout(loff_t size, uint8 *data, loff_t &start_byte, loff_t &real_size) { }

#if defined __APPLE__ && defined __MACH__
void DarwinSysInit(void) { }
void DarwinSysExit(void) { }
void DarwinAddFloppyPrefs(void) { }
void DarwinAddSerialPrefs(void) { }
bool DarwinCDReadTOC(char *, uint8 *) { }
#endif


/*
 *  Display alert
 */

static void display_alert(int title_id, int prefix_id, int button_id, const char *text)
{
	char str[256];
	sprintf(str, GetString(prefix_id), text);

	GtkWidget *dialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dialog), GetString(title_id));
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_window_set_child(GTK_WINDOW(dialog), box);

	GtkWidget *label = gtk_label_new(str);
	gtk_box_append(GTK_BOX(box), label);

	GtkWidget *button = gtk_button_new_with_label(GetString(button_id));
	gtk_box_append(GTK_BOX(box), button);

	// Run a nested main loop until dialog closes
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
	g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_main_loop_quit), loop);

	gtk_window_present(GTK_WINDOW(dialog));
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
}


/*
 *  Display error alert
 */

void ErrorAlert(const char *text)
{
	display_alert(STR_ERROR_ALERT_TITLE, STR_GUI_ERROR_PREFIX, STR_QUIT_BUTTON, text);
}


/*
 *  Display warning alert
 */

void WarningAlert(const char *text)
{
	display_alert(STR_WARNING_ALERT_TITLE, STR_GUI_WARNING_PREFIX, STR_OK_BUTTON, text);
}


/*
 *  RPC handlers
 */

static GMainLoop *g_gui_loop;

static int handle_ErrorAlert(rpc_connection_t *connection)
{
	D(bug("handle_ErrorAlert\n"));

	int error;
	char *str;
	if ((error = rpc_method_get_args(connection, RPC_TYPE_STRING, &str, RPC_TYPE_INVALID)) < 0)
		return error;

	ErrorAlert(str);
	free(str);
	return RPC_ERROR_NO_ERROR;
}

static int handle_WarningAlert(rpc_connection_t *connection)
{
	D(bug("handle_WarningAlert\n"));

	int error;
	char *str;
	if ((error = rpc_method_get_args(connection, RPC_TYPE_STRING, &str, RPC_TYPE_INVALID)) < 0)
		return error;

	WarningAlert(str);
	free(str);
	return RPC_ERROR_NO_ERROR;
}

static int handle_Exit(rpc_connection_t *connection)
{
	D(bug("handle_Exit\n"));

	g_main_loop_quit(g_gui_loop);
	return RPC_ERROR_NO_ERROR;
}


/*
 *  SIGCHLD handler
 */

static char g_app_path[PATH_MAX];
static rpc_connection_t *g_gui_connection = NULL;

static void sigchld_handler(int sig, siginfo_t *sip, void *)
{
	D(bug("Child %d exitted with status = %x\n", sip->si_pid, sip->si_status));

	// XXX perform a new wait because sip->si_status is sometimes not
	// the exit _value_ on MacOS X but rather the usual status field
	// from waitpid() -- we could arrange this code in some other way...
	int status;
	if (waitpid(sip->si_pid, &status, 0) < 0)
		status = sip->si_status;
	if (WIFEXITED(status))
		status = WEXITSTATUS(status);
	if (status & 0x80)
		status |= -1 ^0xff;

	if (status < 0) {	// negative -> execlp/-errno
		char str[256];
		sprintf(str, GetString(STR_NO_B2_EXE_FOUND), g_app_path, strerror(-status));
		ErrorAlert(str);
		status = 1;
	}

	if (status != 0) {
		if (g_gui_connection)
			rpc_exit(g_gui_connection);
		exit(status);
	}
}


/*
 *  Start standalone GUI
 */

int main(int argc, char *argv[])
{
	// Init GTK
	gtk_init();

	// Read preferences
	PrefsInit(0, argc, argv);

	// Show preferences editor
	bool start = PrefsEditor();

	// Exit preferences
	PrefsExit();

	// Transfer control to the executable
	if (start) {
		char gui_connection_path[64];
		sprintf(gui_connection_path, "/org/SheepShaver/GUI/%d", getpid());

		// Catch exits from the child process
		struct sigaction sigchld_sa, old_sigchld_sa;
		sigemptyset(&sigchld_sa.sa_mask);
		sigchld_sa.sa_sigaction = sigchld_handler;
		sigchld_sa.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
		if (sigaction(SIGCHLD, &sigchld_sa, &old_sigchld_sa) < 0) {
			char str[256];
			sprintf(str, GetString(STR_SIG_INSTALL_ERR), SIGCHLD, strerror(errno));
			ErrorAlert(str);
			return 1;
		}

		// Search and run the SheepShaver executable
		char *p;
		strcpy(g_app_path, argv[0]);
		if ((p = strstr(g_app_path, "SheepShaverGUI.app/Contents/MacOS")) != NULL) {
		    strcpy(p, "SheepShaver.app/Contents/MacOS/SheepShaver");
			if (access(g_app_path, X_OK) < 0) {
				char str[256];
				sprintf(str, GetString(STR_NO_B2_EXE_FOUND), g_app_path, strerror(errno));
				WarningAlert(str);
				strcpy(g_app_path, "/Applications/SheepShaver.app/Contents/MacOS/SheepShaver");
			}
		} else {
			p = strrchr(g_app_path, '/');
			p = p ? p + 1 : g_app_path;
			strcpy(p, "SheepShaver");
		}

		int pid = fork();
		if (pid == 0) {
			D(bug("Trying to execute %s\n", g_app_path));
			execlp(g_app_path, g_app_path, "--gui-connection", gui_connection_path, (char *)NULL);
#ifdef _POSIX_PRIORITY_SCHEDULING
			// XXX get a chance to run the parent process so that to not confuse/upset GTK...
			sched_yield();
#endif
			_exit(-errno);
		}

		// Establish a connection to Basilisk II
		if ((g_gui_connection = rpc_init_server(gui_connection_path)) == NULL) {
			printf("ERROR: failed to initialize GUI-side RPC server connection\n");
			return 1;
		}
		static const rpc_method_descriptor_t vtable[] = {
			{ RPC_METHOD_ERROR_ALERT,   handle_ErrorAlert },
			{ RPC_METHOD_WARNING_ALERT, handle_WarningAlert },
			{ RPC_METHOD_EXIT,          handle_Exit }
		};
		if (rpc_method_add_callbacks(g_gui_connection, vtable, sizeof(vtable) / sizeof(vtable[0])) < 0) {
			printf("ERROR: failed to setup GUI method callbacks\n");
			return 1;
		}
		int socket;
		if ((socket = rpc_listen_socket(g_gui_connection)) < 0) {
			printf("ERROR: failed to initialize RPC server thread\n");
			return 1;
		}

		g_gui_loop = g_main_loop_new(NULL, TRUE);
		while (g_main_loop_is_running(g_gui_loop)) {

			// Process a few events pending
			const int N_EVENTS_DISPATCH = 10;
			for (int i = 0; i < N_EVENTS_DISPATCH; i++) {
				if (!g_main_context_iteration(NULL, FALSE))
					break;
			}

			// Check for RPC events (100 ms timeout)
			int ret = rpc_wait_dispatch(g_gui_connection, 100000);
			if (ret == 0)
				continue;
			if (ret < 0)
				break;
			rpc_dispatch(g_gui_connection);
		}

		rpc_exit(g_gui_connection);
		return 0;
	}

	return 0;
}
#endif

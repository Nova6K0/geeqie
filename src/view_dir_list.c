/*
 * Geeqie
 * (C) 2004 John Ellis
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */

#include "main.h"
#include "view_dir_list.h"

#include "dnd.h"
#include "dupe.h"
#include "filelist.h"
#include "layout.h"
#include "layout_image.h"
#include "layout_util.h"
#include "utilops.h"
#include "ui_bookmark.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "ui_tree_edit.h"
#include "view_dir.h"

#include <gdk/gdkkeysyms.h> /* for keyboard values */


#define VDLIST_PAD 4

#define VDLIST_INFO(_vd_, _part_) (((ViewDirInfoList *)(_vd_->info))->_part_)


static void vdlist_popup_destroy_cb(GtkWidget *widget, gpointer data);
static gint vdlist_auto_scroll_notify_cb(GtkWidget *widget, gint x, gint y, gpointer data);

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

static gint vdlist_find_row(ViewDir *vd, FileData *fd, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	gint valid;
	gint row = 0;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	valid = gtk_tree_model_get_iter_first(store, iter);
	while (valid)
		{
		FileData *fd_n;
		gtk_tree_model_get(GTK_TREE_MODEL(store), iter, DIR_COLUMN_POINTER, &fd_n, -1);
		if (fd_n == fd) return row;

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), iter);
		row++;
		}

	return -1;
}

static gint vdlist_rename_row_cb(TreeEditData *td, const gchar *old, const gchar *new, gpointer data)
{
	ViewDir *vd = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	FileData *fd;
	gchar *old_path;
	gchar *new_path;
	gchar *base;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	if (!gtk_tree_model_get_iter(store, &iter, td->path)) return FALSE;
	gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
	if (!fd) return FALSE;
	
	old_path = g_strdup(fd->path);

	base = remove_level_from_path(old_path);
	new_path = concat_dir_and_file(base, new);
	g_free(base);

	if (file_util_rename_dir(fd, new_path, vd->view))
		{
		if (vd->layout && strcmp(vd->path, old_path) == 0)
			{
			layout_set_path(vd->layout, new_path);
			}
		else
			{
			vdlist_refresh(vd);
			}
		}

	g_free(old_path);
	g_free(new_path);
	return FALSE;
}

static void vdlist_rename_by_row(ViewDir *vd, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	if (vdlist_find_row(vd, fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	tpath = gtk_tree_model_get_path(store, &iter);

	tree_edit_by_path(GTK_TREE_VIEW(vd->view), tpath, 0, fd->name,
			  vdlist_rename_row_cb, vd);
	gtk_tree_path_free(tpath);
}

static FileData *vdlist_row_by_path(ViewDir *vd, const gchar *path, gint *row)
{
	GList *work;
	gint n;

	if (!path)
		{
		if (row) *row = -1;
		return NULL;
		}

	n = 0;
	work = VDLIST_INFO(vd, list);
	while (work)
		{
		FileData *fd = work->data;
		if (strcmp(fd->path, path) == 0)
			{
			if (row) *row = n;
			return fd;
			}
		work = work->next;
		n++;
		}

	if (row) *row = -1;
	return NULL;
}

static void vdlist_color_set(ViewDir *vd, FileData *fd, gint color_set)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	if (vdlist_find_row(vd, fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	gtk_list_store_set(GTK_LIST_STORE(store), &iter, DIR_COLUMN_COLOR, color_set, -1);
}

/*
 *-----------------------------------------------------------------------------
 * drop menu (from dnd)
 *-----------------------------------------------------------------------------
 */

static void vdlist_drop_menu_copy_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	const gchar *path;
	GList *list;

	if (!vd->drop_fd) return;

	path = vd->drop_fd->path;
	list = vd->drop_list;
	vd->drop_list = NULL;

	file_util_copy_simple(list, path);
}

static void vdlist_drop_menu_move_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	const gchar *path;
	GList *list;

	if (!vd->drop_fd) return;

	path = vd->drop_fd->path;
	list = vd->drop_list;

	vd->drop_list = NULL;

	file_util_move_simple(list, path);
}

static GtkWidget *vdlist_drop_menu(ViewDir *vd, gint active)
{
	GtkWidget *menu;

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vdlist_popup_destroy_cb), vd);

	menu_item_add_stock_sensitive(menu, _("_Copy"), GTK_STOCK_COPY, active,
				      G_CALLBACK(vdlist_drop_menu_copy_cb), vd);
	menu_item_add_sensitive(menu, _("_Move"), active, G_CALLBACK(vdlist_drop_menu_move_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_stock(menu, _("Cancel"), GTK_STOCK_CANCEL, NULL, vd);

	return menu;
}

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */ 

static void vdlist_pop_menu_up_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	gchar *path;

	if (!vd->path || strcmp(vd->path, "/") == 0) return;
	path = remove_level_from_path(vd->path);

	if (vd->select_func)
		{
		vd->select_func(vd, path, vd->select_data);
		}

	g_free(path);
}

static void vdlist_pop_menu_slide_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	gchar *path;

	if (!vd->layout || !vd->click_fd) return;

	path = g_strdup(vd->click_fd->path);

	layout_set_path(vd->layout, path);
	layout_select_none(vd->layout);
	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start(vd->layout);

	g_free(path);
}

static void vdlist_pop_menu_slide_rec_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	gchar *path;
	GList *list;

	if (!vd->layout || !vd->click_fd) return;

	path = g_strdup(vd->click_fd->path);

	list = filelist_recursive(path);

	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start_from_list(vd->layout, list);

	g_free(path);
}

static void vdlist_pop_menu_dupe(ViewDir *vd, gint recursive)
{
	DupeWindow *dw;
	GList *list = NULL;

	if (!vd->click_fd) return;

	if (recursive)
		{
		list = g_list_append(list, file_data_ref(vd->click_fd));
		}
	else
		{
		filelist_read(vd->click_fd->path, &list, NULL);
		list = filelist_filter(list, FALSE);
		}

	dw = dupe_window_new(DUPE_MATCH_NAME);
	dupe_window_add_files(dw, list, recursive);

	filelist_free(list);
}

static void vdlist_pop_menu_dupe_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	vdlist_pop_menu_dupe(vd, FALSE);
}

static void vdlist_pop_menu_dupe_rec_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	vdlist_pop_menu_dupe(vd, TRUE);
}

static void vdlist_pop_menu_new_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	gchar *new_path;
	gchar *buf;

	if (!vd->path) return;

	buf = concat_dir_and_file(vd->path, _("new_folder"));
	new_path = unique_filename(buf, NULL, NULL, FALSE);
	g_free(buf);
	if (!new_path) return;

	if (!mkdir_utf8(new_path, 0755))
		{
		gchar *text;

		text = g_strdup_printf(_("Unable to create folder:\n%s"), new_path);
		file_util_warning_dialog(_("Error creating folder"), text, GTK_STOCK_DIALOG_ERROR, vd->view);
		g_free(text);
		}
	else
		{
		FileData *fd;

		vdlist_refresh(vd);
		fd = vdlist_row_by_path(vd, new_path, NULL);

		vdlist_rename_by_row(vd, fd);
		}

	g_free(new_path);
}

static void vdlist_pop_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	vdlist_rename_by_row(vd, vd->click_fd);
}

static void vdlist_pop_menu_delete_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	if (!vd->click_fd) return;
	file_util_delete_dir(vd->click_fd, vd->widget);
}

static void vdlist_pop_menu_dir_view_as_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	if (vd->layout) layout_views_set(vd->layout, DIRVIEW_TREE, vd->layout->icon_view);
}

static void vdlist_pop_menu_refresh_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	if (vd->layout) layout_refresh(vd->layout);
}

static void vdlist_toggle_show_hidden_files_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	options->file_filter.show_hidden_files = !options->file_filter.show_hidden_files;
	if (vd->layout) layout_refresh(vd->layout);
}

static GtkWidget *vdlist_pop_menu(ViewDir *vd, FileData *fd)
{
	GtkWidget *menu;
	gint active;

	active = (fd != NULL);

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vdlist_popup_destroy_cb), vd);

	menu_item_add_stock_sensitive(menu, _("_Up to parent"), GTK_STOCK_GO_UP,
				      (vd->path && strcmp(vd->path, "/") != 0),
				      G_CALLBACK(vdlist_pop_menu_up_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("_Slideshow"), active,
				G_CALLBACK(vdlist_pop_menu_slide_cb), vd);
	menu_item_add_sensitive(menu, _("Slideshow recursive"), active,
				G_CALLBACK(vdlist_pop_menu_slide_rec_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_stock_sensitive(menu, _("Find _duplicates..."), GTK_STOCK_FIND, active,
				      G_CALLBACK(vdlist_pop_menu_dupe_cb), vd);
	menu_item_add_stock_sensitive(menu, _("Find duplicates recursive..."), GTK_STOCK_FIND, active,
				      G_CALLBACK(vdlist_pop_menu_dupe_rec_cb), vd);

	menu_item_add_divider(menu);

	/* check using . (always row 0) */
	active = (vd->path && access_file(vd->path , W_OK | X_OK));
	menu_item_add_sensitive(menu, _("_New folder..."), active,
				G_CALLBACK(vdlist_pop_menu_new_cb), vd);

	/* ignore .. and . */
	active = (active && fd &&
		  strcmp(fd->name, ".") != 0 &&
		  strcmp(fd->name, "..") != 0 &&
		  access_file(fd->path, W_OK | X_OK));
	menu_item_add_sensitive(menu, _("_Rename..."), active,
				G_CALLBACK(vdlist_pop_menu_rename_cb), vd);
	menu_item_add_stock_sensitive(menu, _("_Delete..."), GTK_STOCK_DELETE, active,
				      G_CALLBACK(vdlist_pop_menu_delete_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_check(menu, _("View as _tree"), FALSE,
			    G_CALLBACK(vdlist_pop_menu_dir_view_as_cb), vd);
	menu_item_add_check(menu, _("Show _hidden files"), options->file_filter.show_hidden_files,
			    G_CALLBACK(vdlist_toggle_show_hidden_files_cb), vd);

	menu_item_add_stock(menu, _("Re_fresh"), GTK_STOCK_REFRESH,
			    G_CALLBACK(vdlist_pop_menu_refresh_cb), vd);

	return menu;
}

static void vdlist_popup_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	vdlist_color_set(vd, vd->click_fd, FALSE);
	vd->click_fd = NULL;
	vd->popup = NULL;

	vdlist_color_set(vd, vd->drop_fd, FALSE);
	filelist_free(vd->drop_list);
	vd->drop_list = NULL;
	vd->drop_fd = NULL;
}

/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

static GtkTargetEntry vdlist_dnd_drop_types[] = {
	{ "text/uri-list", 0, TARGET_URI_LIST }
};
static gint vdlist_dnd_drop_types_count = 1;

static void vdlist_dest_set(ViewDir *vd, gint enable)
{
	if (enable)
		{
		gtk_drag_dest_set(vd->view,
				  GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
				  vdlist_dnd_drop_types, vdlist_dnd_drop_types_count,
				  GDK_ACTION_MOVE | GDK_ACTION_COPY);
		}
	else
		{
		gtk_drag_dest_unset(vd->view);
		}
}

static void vdlist_dnd_get(GtkWidget *widget, GdkDragContext *context,
			   GtkSelectionData *selection_data, guint info,
			   guint time, gpointer data)
{
	ViewDir *vd = data;
	GList *list;
	gchar *text = NULL;
	gint length = 0;

	if (!vd->click_fd) return;

	switch (info)
		{
		case TARGET_URI_LIST:
		case TARGET_TEXT_PLAIN:
			list = g_list_prepend(NULL, vd->click_fd);
			text = uri_text_from_filelist(list, &length, (info == TARGET_TEXT_PLAIN));
			g_list_free(list);
			break;
		}
	if (text)
		{
		gtk_selection_data_set (selection_data, selection_data->target,
				8, (guchar *)text, length);
		g_free(text);
		}
}

static void vdlist_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewDir *vd = data;

	vdlist_color_set(vd, vd->click_fd, TRUE);
	vdlist_dest_set(vd, FALSE);
}

static void vdlist_dnd_end(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewDir *vd = data;

	vdlist_color_set(vd, vd->click_fd, FALSE);

	if (context->action == GDK_ACTION_MOVE)
		{
		vdlist_refresh(vd);
		}
	vdlist_dest_set(vd, TRUE);
}

static void vdlist_dnd_drop_receive(GtkWidget *widget,
				    GdkDragContext *context, gint x, gint y,
				    GtkSelectionData *selection_data, guint info,
				    guint time, gpointer data)
{
	ViewDir *vd = data;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	vd->click_fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x, y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	if (!fd) return;

	if (info == TARGET_URI_LIST)
		{
		GList *list;
		gint active;

		list = uri_filelist_from_text((gchar *)selection_data->data, TRUE);
		if (!list) return;

		active = access_file(fd->path, W_OK | X_OK);

		vdlist_color_set(vd, fd, TRUE);
		vd->popup = vdlist_drop_menu(vd, active);
		gtk_menu_popup(GTK_MENU(vd->popup), NULL, NULL, NULL, NULL, 0, time);

		vd->drop_fd = fd;
		vd->drop_list = list;
		}
}

#if 0
static gint vdlist_get_row_visibility(ViewDir *vd, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeViewColumn *column;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	GdkRectangle vrect;
	GdkRectangle crect;

	if (!fd || vdlist_find_row(vd, fd, &iter) < 0) return 0;

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(vd->view), 0);
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	tpath = gtk_tree_model_get_path(store, &iter);

	gtk_tree_view_get_visible_rect(GTK_TREE_VIEW(vd->view), &vrect);
	gtk_tree_view_get_cell_area(GTK_TREE_VIEW(vd->view), tpath, column, &crect);
printf("window: %d + %d; cell: %d + %d\n", vrect.y, vrect.height, crect.y, crect.height);
	gtk_tree_path_free(tpath);

	if (crect.y + crect.height < vrect.y) return -1;
	if (crect.y > vrect.y + vrect.height) return 1;
	return 0;
}
#endif

static void vdlist_scroll_to_row(ViewDir *vd, FileData *fd, gfloat y_align)
{
	GtkTreeIter iter;

	if (GTK_WIDGET_REALIZED(vd->view) &&
	    vdlist_find_row(vd, fd, &iter) >= 0)
		{
		GtkTreeModel *store;
		GtkTreePath *tpath;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
		tpath = gtk_tree_model_get_path(store, &iter);
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(vd->view), tpath, NULL, TRUE, y_align, 0.0);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(vd->view), tpath, NULL, FALSE);
		gtk_tree_path_free(tpath);

		if (!GTK_WIDGET_HAS_FOCUS(vd->view)) gtk_widget_grab_focus(vd->view);
		}
}

static void vdlist_drop_update(ViewDir *vd, gint x, gint y)
{
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vd->view), x, y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	if (fd != vd->drop_fd)
		{
		vdlist_color_set(vd, vd->drop_fd, FALSE);
		vdlist_color_set(vd, fd, TRUE);
		}

	vd->drop_fd = fd;
}

static void vdlist_dnd_drop_scroll_cancel(ViewDir *vd)
{
	if (vd->drop_scroll_id != -1) g_source_remove(vd->drop_scroll_id);
	vd->drop_scroll_id = -1;
}

static gint vdlist_auto_scroll_idle_cb(gpointer data)
{
	ViewDir *vd = data;

	if (vd->drop_fd)
		{
		GdkWindow *window;
		gint x, y;
		gint w, h;

		window = vd->view->window;
		gdk_window_get_pointer(window, &x, &y, NULL);
		gdk_drawable_get_size(window, &w, &h);
		if (x >= 0 && x < w && y >= 0 && y < h)
			{
			vdlist_drop_update(vd, x, y);
			}
		}

	vd->drop_scroll_id = -1;
	return FALSE;
}

static gint vdlist_auto_scroll_notify_cb(GtkWidget *widget, gint x, gint y, gpointer data)
{
	ViewDir *vd = data;

	if (!vd->drop_fd || vd->drop_list) return FALSE;

	if (vd->drop_scroll_id == -1) vd->drop_scroll_id = g_idle_add(vdlist_auto_scroll_idle_cb, vd);

	return TRUE;
}

static gint vdlist_dnd_drop_motion(GtkWidget *widget, GdkDragContext *context,
				   gint x, gint y, guint time, gpointer data)
{
	ViewDir *vd = data;

	vd->click_fd = NULL;

	if (gtk_drag_get_source_widget(context) == vd->view)
		{
		/* from same window */
		gdk_drag_status(context, 0, time);
		return TRUE;
		}
	else
		{
		gdk_drag_status(context, context->suggested_action, time);
		}

	vdlist_drop_update(vd, x, y);

        if (vd->drop_fd)
		{
		GtkAdjustment *adj = gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(vd->view));
		widget_auto_scroll_start(vd->view, adj, -1, -1, vdlist_auto_scroll_notify_cb, vd);
		}

	return FALSE;
}

static void vdlist_dnd_drop_leave(GtkWidget *widget, GdkDragContext *context, guint time, gpointer data)
{
	ViewDir *vd = data;

	if (vd->drop_fd != vd->click_fd) vdlist_color_set(vd, vd->drop_fd, FALSE);

	vd->drop_fd = NULL;
}

static void vdlist_dnd_init(ViewDir *vd)
{
	gtk_drag_source_set(vd->view, GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			    dnd_file_drag_types, dnd_file_drag_types_count,
			    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
	g_signal_connect(G_OBJECT(vd->view), "drag_data_get",
			 G_CALLBACK(vdlist_dnd_get), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_begin",
			 G_CALLBACK(vdlist_dnd_begin), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_end",
			 G_CALLBACK(vdlist_dnd_end), vd);

	vdlist_dest_set(vd, TRUE);
	g_signal_connect(G_OBJECT(vd->view), "drag_data_received",
			 G_CALLBACK(vdlist_dnd_drop_receive), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_motion",
			 G_CALLBACK(vdlist_dnd_drop_motion), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_leave",
			 G_CALLBACK(vdlist_dnd_drop_leave), vd);
}

/*
 *-----------------------------------------------------------------------------
 * main
 *-----------------------------------------------------------------------------
 */ 

static void vdlist_select_row(ViewDir *vd, FileData *fd)
{
	if (fd && vd->select_func)
		{
		gchar *path;

		path = g_strdup(fd->path);
		vd->select_func(vd, path, vd->select_data);
		g_free(path);
		}
}

const gchar *vdlist_row_get_path(ViewDir *vd, gint row)
{
	FileData *fd;

	fd = g_list_nth_data(VDLIST_INFO(vd, list), row);

	if (fd) return fd->path;

	return NULL;
}

static void vdlist_populate(ViewDir *vd)
{
	GtkListStore *store;
	GList *work;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view)));
	gtk_list_store_clear(store);

	work = VDLIST_INFO(vd, list);
	while (work)
		{
		FileData *fd;
		GtkTreeIter iter;
		GdkPixbuf *pixbuf;

		fd = work->data;

		if (access_file(fd->path, R_OK | X_OK) && fd->name)
			{
			if (fd->name[0] == '.' && fd->name[1] == '\0')
				{
				pixbuf = vd->pf->open;
				}
			else if (fd->name[0] == '.' && fd->name[1] == '.' && fd->name[2] == '\0')
				{
				pixbuf = vd->pf->parent;
				}
			else
				{
				pixbuf = vd->pf->close;
				}
			}
		else
			{
			pixbuf = vd->pf->deny;
			}

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
				   DIR_COLUMN_POINTER, fd,
				   DIR_COLUMN_ICON, pixbuf,
				   DIR_COLUMN_NAME, fd->name, -1);

		work = work->next;
		}

	vd->click_fd = NULL;
	vd->drop_fd = NULL;
}

gint vdlist_set_path(ViewDir *vd, const gchar *path)
{
	gint ret;
	FileData *fd;
	gchar *old_path = NULL;
	gchar *filepath;

	if (!path) return FALSE;
	if (vd->path && strcmp(path, vd->path) == 0) return TRUE;

	if (vd->path)
		{
		gchar *base;

		base = remove_level_from_path(vd->path);
		if (strcmp(base, path) == 0)
			{
			old_path = g_strdup(filename_from_path(vd->path));
			}
		g_free(base);
		}

	g_free(vd->path);
	vd->path = g_strdup(path);

	filelist_free(VDLIST_INFO(vd, list));
	VDLIST_INFO(vd, list) = NULL;

	ret = filelist_read(vd->path, NULL, &VDLIST_INFO(vd, list));

	VDLIST_INFO(vd, list) = filelist_sort(VDLIST_INFO(vd, list), SORT_NAME, TRUE);

	/* add . and .. */

	if (strcmp(vd->path, "/") != 0)
		{
		filepath = g_strconcat(vd->path, "/", "..", NULL); 
		fd = file_data_new_simple(filepath);
		VDLIST_INFO(vd, list) = g_list_prepend(VDLIST_INFO(vd, list), fd);
		g_free(filepath);
		}
	
	if (options->file_filter.show_dot_directory)
		{
		filepath = g_strconcat(vd->path, "/", ".", NULL); 
		fd = file_data_new_simple(filepath);
		VDLIST_INFO(vd, list) = g_list_prepend(VDLIST_INFO(vd, list), fd);
		g_free(filepath);
	}

	vdlist_populate(vd);

	if (old_path)
		{
		/* scroll to make last path visible */
		FileData *found = NULL;
		GList *work;

		work = VDLIST_INFO(vd, list);
		while (work && !found)
			{
			FileData *fd = work->data;
			if (strcmp(old_path, fd->name) == 0) found = fd;
			work = work->next;
			}

		if (found) vdlist_scroll_to_row(vd, found, 0.5);

		g_free(old_path);
		return ret;
		}

	if (GTK_WIDGET_REALIZED(vd->view))
		{
		gtk_tree_view_scroll_to_point(GTK_TREE_VIEW(vd->view), 0, 0);
		}

	return ret;
}

void vdlist_refresh(ViewDir *vd)
{
	gchar *path;

	path = g_strdup(vd->path);
	vd->path = NULL;
	vdlist_set_path(vd, path);
	g_free(path);
}

static void vdlist_menu_position_cb(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
{
	ViewDir *vd = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreePath *tpath;
	gint cw, ch;

	if (vdlist_find_row(vd, vd->click_fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	tpath = gtk_tree_model_get_path(store, &iter);
	tree_view_get_cell_clamped(GTK_TREE_VIEW(vd->view), tpath, 0, TRUE, x, y, &cw, &ch);
	gtk_tree_path_free(tpath);
	*y += ch;
	popup_menu_position_clamp(menu, x, y, 0);
}

static gint vdlist_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	ViewDir *vd = data;
	GtkTreePath *tpath;
	
	if (event->keyval != GDK_Menu) return FALSE;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(vd->view), &tpath, NULL);
	if (tpath)
		{
		GtkTreeModel *store;
		GtkTreeIter iter;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &vd->click_fd, -1);
		
		gtk_tree_path_free(tpath);
		}
	else
		{
		vd->click_fd = NULL;
		}

	vdlist_color_set(vd, vd->click_fd, TRUE);

	vd->popup = vdlist_pop_menu(vd, vd->click_fd);

	gtk_menu_popup(GTK_MENU(vd->popup), NULL, NULL, vdlist_menu_position_cb, vd, 0, GDK_CURRENT_TIME);

	return TRUE;
}

static gint vdlist_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewDir *vd = data;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, NULL, FALSE);
		gtk_tree_path_free(tpath);
		}

	vd->click_fd = fd;
	vdlist_color_set(vd, vd->click_fd, TRUE);

	if (bevent->button == 3)
		{
		vd->popup = vdlist_pop_menu(vd, vd->click_fd);
		gtk_menu_popup(GTK_MENU(vd->popup), NULL, NULL, NULL, NULL,
			       bevent->button, bevent->time);
		}

	return TRUE;
}

static gint vdlist_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewDir *vd = data;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	vdlist_color_set(vd, vd->click_fd, FALSE);

	if (bevent->button != 1) return TRUE;

	if ((bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	if (fd && vd->click_fd == fd)
		{
		vdlist_select_row(vd, vd->click_fd);
		}

	return TRUE;
}

static void vdlist_select_cb(GtkTreeView *tview, GtkTreePath *tpath, GtkTreeViewColumn *column, gpointer data)
{
	ViewDir *vd = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	FileData *fd;

	store = gtk_tree_view_get_model(tview);
	gtk_tree_model_get_iter(store, &iter, tpath);
	gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);

	vdlist_select_row(vd, fd);
}

static GdkColor *vdlist_color_shifted(GtkWidget *widget)
{
	static GdkColor color;
	static GtkWidget *done = NULL;

	if (done != widget)
		{
		GtkStyle *style;

		style = gtk_widget_get_style(widget);
		memcpy(&color, &style->base[GTK_STATE_NORMAL], sizeof(color));
		shift_color(&color, -1, 0);
		done = widget;
		}

	return &color;
}

static void vdlist_color_cb(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
			    GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	ViewDir *vd = data;
	gboolean set;

	gtk_tree_model_get(tree_model, iter, DIR_COLUMN_COLOR, &set, -1);
	g_object_set(G_OBJECT(cell),
		     "cell-background-gdk", vdlist_color_shifted(vd->view),
		     "cell-background-set", set, NULL);
}

static void vdlist_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	if (vd->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(vd->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, 0, NULL, vd);
		gtk_widget_destroy(vd->popup);
		}

	vdlist_dnd_drop_scroll_cancel(vd);
	widget_auto_scroll_stop(vd->view);

	filelist_free(vd->drop_list);

	folder_icons_free(vd->pf);

	g_free(vd->path);
	filelist_free(VDLIST_INFO(vd, list));
	g_free(vd->info);
	g_free(vd);
}

ViewDir *vdlist_new(const gchar *path)
{
	ViewDir *vd;
	GtkListStore *store;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	vd = g_new0(ViewDir, 1);
	vd->info = g_new0(ViewDirInfoList, 1);
	vd->type = DIRVIEW_LIST;

	vd->path = NULL;
	VDLIST_INFO(vd, list) = NULL;
	vd->click_fd = NULL;

	vd->drop_fd = NULL;
	vd->drop_list = NULL;

	vd->drop_scroll_id = -1;

	vd->popup = NULL;

	vd->widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(vd->widget), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vd->widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	g_signal_connect(G_OBJECT(vd->widget), "destroy",
			 G_CALLBACK(vdlist_destroy_cb), vd);

	store = gtk_list_store_new(4, G_TYPE_POINTER, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN);
	vd->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(vd->view), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(vd->view), FALSE);
	g_signal_connect(G_OBJECT(vd->view), "row_activated",

			 G_CALLBACK(vdlist_select_cb), vd);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vd->view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "pixbuf", DIR_COLUMN_ICON);
	gtk_tree_view_column_set_cell_data_func(column, renderer, vdlist_color_cb, vd, NULL);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", DIR_COLUMN_NAME);
	gtk_tree_view_column_set_cell_data_func(column, renderer, vdlist_color_cb, vd, NULL);

	gtk_tree_view_append_column(GTK_TREE_VIEW(vd->view), column);

	g_signal_connect(G_OBJECT(vd->view), "key_press_event",
			   G_CALLBACK(vdlist_press_key_cb), vd);
	gtk_container_add(GTK_CONTAINER(vd->widget), vd->view);
	gtk_widget_show(vd->view);

	vd->pf = folder_icons_new();

	vdlist_dnd_init(vd);

	g_signal_connect(G_OBJECT(vd->view), "button_press_event",
			 G_CALLBACK(vdlist_press_cb), vd);
	g_signal_connect(G_OBJECT(vd->view), "button_release_event",
			 G_CALLBACK(vdlist_release_cb), vd);

	if (path) vdlist_set_path(vd, path);

	return vd;
}

/*
 * Geeqie
 * (C) 2004 John Ellis
 * Copyright (C) 2008 - 2009 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */


#ifndef BAR_H
#define BAR_H

typedef struct _PaneData PaneData;

struct _PaneData {
	/* filled in by pane */
	void (*pane_set_fd)(GtkWidget *pane, FileData *fd);
	gint (*pane_event)(GtkWidget *pane, GdkEvent *event);
	void (*pane_write_config)(GtkWidget *pane, GString *outstr, gint indent);
	GtkWidget *title;
	gboolean expanded;
	
	/* filled in by bar */
	GtkWidget *bar;
	LayoutWindow *lw;
};



GtkWidget *bar_new(LayoutWindow *lw);
GtkWidget *bar_new_default(LayoutWindow *lw);
GtkWidget *bar_new_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values);

void bar_close(GtkWidget *bar);

void bar_write_config(GtkWidget *bar, GString *outstr, gint indent);

void bar_add(GtkWidget *bar, GtkWidget *pane);


void bar_set_fd(GtkWidget *bar, FileData *fd);
gboolean bar_event(GtkWidget *bar, GdkEvent *event);

gint bar_get_width(GtkWidget *bar);

GtkWidget *bar_pane_expander_title(const gchar *title);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */

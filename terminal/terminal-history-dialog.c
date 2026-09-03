/*-
 * Copyright (c) 2026 Xfce Development Team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libxfce4util/libxfce4util.h>

#include "terminal-history-dialog.h"
#include "terminal-history.h"
#include "terminal-private.h"

enum
{
  COLUMN_COMMAND,
  N_COLUMNS
};



#if !LIBXFCE4UI_CHECK_VERSION(4, 21, 8)
struct _TerminalHistoryDialogClass
{
  XfceTitledDialogClass parent_class;
};
#endif

struct _TerminalHistoryDialog
{
  XfceTitledDialog parent_instance;

  TerminalHistory *history;

  GtkWidget *entry;
  GtkWidget *view;
  GtkListStore *store;
};



static void
terminal_history_dialog_finalize (GObject *object);
static void
terminal_history_dialog_populate (TerminalHistoryDialog *dialog);



G_DEFINE_TYPE (TerminalHistoryDialog, terminal_history_dialog, XFCE_TYPE_TITLED_DIALOG)



static void
terminal_history_dialog_class_init (TerminalHistoryDialogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = terminal_history_dialog_finalize;
}



static void
terminal_history_dialog_select (TerminalHistoryDialog *dialog,
                                gint offset)
{
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->view));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return;

  path = gtk_tree_model_get_path (model, &iter);
  if (offset > 0)
    gtk_tree_path_next (path);
  else if (!gtk_tree_path_prev (path))
    {
      gtk_tree_path_free (path);
      return;
    }

  if (gtk_tree_model_get_iter (model, &iter, path))
    {
      gtk_tree_selection_select_path (selection, path);
      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (dialog->view), path, NULL, FALSE, 0.0, 0.0);
    }

  gtk_tree_path_free (path);
}



static gboolean
terminal_history_dialog_entry_key_press (TerminalHistoryDialog *dialog,
                                         GdkEventKey *event)
{
  /* the list is driven from the entry, so the entry keeps the focus */
  switch (event->keyval)
    {
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
      terminal_history_dialog_select (dialog, -1);
      return TRUE;

    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
      terminal_history_dialog_select (dialog, 1);
      return TRUE;

    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
      return TRUE;

    default:
      return FALSE;
    }
}



static void
terminal_history_dialog_row_activated (TerminalHistoryDialog *dialog)
{
  gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
}



static void
terminal_history_dialog_init (TerminalHistoryDialog *dialog)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *vbox;
  GtkWidget *swin;

  dialog->history = terminal_history_get ();

  gtk_window_set_title (GTK_WINDOW (dialog), _("Command History"));
  gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 350);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);

  xfce_titled_dialog_add_action_widget (XFCE_TITLED_DIALOG (dialog),
                                        gtk_button_new_with_mnemonic (_("_Close")),
                                        GTK_RESPONSE_CLOSE);
  xfce_titled_dialog_add_action_widget (XFCE_TITLED_DIALOG (dialog),
                                        gtk_button_new_with_mnemonic (_("_Insert")),
                                        GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox, TRUE, TRUE, 0);

  dialog->entry = gtk_search_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (dialog->entry), _("Search previous commands"));
  gtk_entry_set_activates_default (GTK_ENTRY (dialog->entry), FALSE);
  g_signal_connect_swapped (G_OBJECT (dialog->entry), "search-changed",
                            G_CALLBACK (terminal_history_dialog_populate), dialog);
  g_signal_connect_swapped (G_OBJECT (dialog->entry), "key-press-event",
                            G_CALLBACK (terminal_history_dialog_entry_key_press), dialog);
  gtk_box_pack_start (GTK_BOX (vbox), dialog->entry, FALSE, FALSE, 0);

  swin = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (swin), GTK_SHADOW_IN);
  gtk_box_pack_start (GTK_BOX (vbox), swin, TRUE, TRUE, 0);

  dialog->store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING);
  dialog->view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (dialog->store));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (dialog->view), FALSE);
  gtk_widget_set_can_focus (dialog->view, FALSE);
  g_signal_connect_swapped (G_OBJECT (dialog->view), "row-activated",
                            G_CALLBACK (terminal_history_dialog_row_activated), dialog);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer), "ellipsize", PANGO_ELLIPSIZE_END, "family", "monospace", NULL);
  column = gtk_tree_view_column_new_with_attributes (NULL, renderer, "text", COLUMN_COMMAND, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->view), column);
  gtk_container_add (GTK_CONTAINER (swin), dialog->view);

  terminal_history_dialog_populate (dialog);

  gtk_widget_show_all (vbox);
  gtk_widget_grab_focus (dialog->entry);
}



static void
terminal_history_dialog_finalize (GObject *object)
{
  TerminalHistoryDialog *dialog = TERMINAL_HISTORY_DIALOG (object);

  g_object_unref (G_OBJECT (dialog->store));
  g_object_unref (G_OBJECT (dialog->history));

  (*G_OBJECT_CLASS (terminal_history_dialog_parent_class)->finalize) (object);
}



static void
terminal_history_dialog_populate (TerminalHistoryDialog *dialog)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GPtrArray *matches;

  gtk_list_store_clear (dialog->store);

  matches = terminal_history_search (dialog->history, gtk_entry_get_text (GTK_ENTRY (dialog->entry)));
  for (guint i = 0; i < matches->len; i++)
    {
      gtk_list_store_append (dialog->store, &iter);
      gtk_list_store_set (dialog->store, &iter, COLUMN_COMMAND, g_ptr_array_index (matches, i), -1);
    }
  g_ptr_array_free (matches, TRUE);

  /* the best match is preselected, so typing and pressing Enter is enough */
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->view));
  if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dialog->store), &iter))
    gtk_tree_selection_select_iter (selection, &iter);
}



/**
 * terminal_history_dialog_new:
 * @parent : the window the dialog belongs to.
 *
 * Creates a dialog listing the commands remembered from previous successful
 * runs, filtered by what is typed in its entry.
 **/
GtkWidget *
terminal_history_dialog_new (GtkWindow *parent)
{
  return g_object_new (TERMINAL_TYPE_HISTORY_DIALOG,
                       "transient-for", parent,
                       "destroy-with-parent", TRUE,
                       NULL);
}



/**
 * terminal_history_dialog_get_command:
 * @dialog : a #TerminalHistoryDialog.
 *
 * Returns the selected command, or %NULL when nothing is selected. Free the
 * returned string with g_free().
 **/
gchar *
terminal_history_dialog_get_command (TerminalHistoryDialog *dialog)
{
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *command = NULL;

  g_return_val_if_fail (TERMINAL_IS_HISTORY_DIALOG (dialog), NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->view));
  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter, COLUMN_COMMAND, &command, -1);

  return command;
}

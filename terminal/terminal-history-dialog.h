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

#ifndef TERMINAL_HISTORY_DIALOG_H
#define TERMINAL_HISTORY_DIALOG_H

#include <libxfce4ui/libxfce4ui.h>

G_BEGIN_DECLS

/* older libxfce4ui does not declare the autoptr cleanup G_DECLARE_FINAL_TYPE chains up to */
#if !LIBXFCE4UI_CHECK_VERSION(4, 21, 8)
#define TERMINAL_TYPE_HISTORY_DIALOG (terminal_history_dialog_get_type ())
#define TERMINAL_HISTORY_DIALOG(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), TERMINAL_TYPE_HISTORY_DIALOG, TerminalHistoryDialog))
#define TERMINAL_HISTORY_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), TERMINAL_TYPE_HISTORY_DIALOG, TerminalHistoryDialogClass))
#define TERMINAL_IS_HISTORY_DIALOG(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TERMINAL_TYPE_HISTORY_DIALOG))
#define TERMINAL_IS_HISTORY_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TERMINAL_TYPE_HISTORY_DIALOG))
#define TERMINAL_HISTORY_DIALOG_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), TERMINAL_TYPE_HISTORY_DIALOG, TerminalHistoryDialogClass))
typedef struct _TerminalHistoryDialog TerminalHistoryDialog;
typedef struct _TerminalHistoryDialogClass TerminalHistoryDialogClass;
GType
terminal_history_dialog_get_type (void);
#else
#define TERMINAL_TYPE_HISTORY_DIALOG (terminal_history_dialog_get_type ())
G_DECLARE_FINAL_TYPE (TerminalHistoryDialog, terminal_history_dialog, TERMINAL, HISTORY_DIALOG, XfceTitledDialog)
#endif

GtkWidget *
terminal_history_dialog_new (GtkWindow *parent);

gchar *
terminal_history_dialog_get_command (TerminalHistoryDialog *dialog);

G_END_DECLS

#endif /* !TERMINAL_HISTORY_DIALOG_H */

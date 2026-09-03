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

#ifndef TERMINAL_HISTORY_H
#define TERMINAL_HISTORY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define TERMINAL_TYPE_HISTORY (terminal_history_get_type ())
G_DECLARE_FINAL_TYPE (TerminalHistory, terminal_history, TERMINAL, HISTORY, GObject)

TerminalHistory *
terminal_history_get (void);

void
terminal_history_add (TerminalHistory *history,
                      const gchar *command);

const gchar *
terminal_history_lookup (TerminalHistory *history,
                         const gchar *prefix);

GPtrArray *
terminal_history_search (TerminalHistory *history,
                         const gchar *query);

void
terminal_history_clear (TerminalHistory *history);

void
terminal_history_flush (TerminalHistory *history);

G_END_DECLS

#endif /* !TERMINAL_HISTORY_H */

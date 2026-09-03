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

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <libxfce4util/libxfce4util.h>

#include "terminal-history.h"
#include "terminal-preferences.h"
#include "terminal-private.h"

#define HISTORY_PATH "xfce4/terminal/command-history"
#define HISTORY_SAVE_DELAY 10



struct _TerminalHistory
{
  GObject parent_instance;

  /* commands, oldest first, newest last; no duplicates */
  GPtrArray *commands;

  TerminalPreferences *preferences;

  guint save_id;
};



static void
terminal_history_finalize (GObject *object);
static void
terminal_history_load (TerminalHistory *history);
static void
terminal_history_queue_save (TerminalHistory *history);



G_DEFINE_TYPE (TerminalHistory, terminal_history, G_TYPE_OBJECT)



static void
terminal_history_class_init (TerminalHistoryClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = terminal_history_finalize;
}



static void
terminal_history_init (TerminalHistory *history)
{
  history->commands = g_ptr_array_new_with_free_func (g_free);
  history->preferences = terminal_preferences_get ();

  terminal_history_load (history);
}



static void
terminal_history_finalize (GObject *object)
{
  TerminalHistory *history = TERMINAL_HISTORY (object);

  /* write out anything still pending */
  if (history->save_id != 0)
    terminal_history_flush (history);

  g_ptr_array_free (history->commands, TRUE);
  g_object_unref (G_OBJECT (history->preferences));

  (*G_OBJECT_CLASS (terminal_history_parent_class)->finalize) (object);
}



static guint
terminal_history_get_max_size (TerminalHistory *history)
{
  guint size;

  g_object_get (G_OBJECT (history->preferences), "misc-command-history-size", &size, NULL);

  return size;
}



static void
terminal_history_load (TerminalHistory *history)
{
  gchar *path;
  gchar *contents;
  gchar **lines;
  guint max_size;
  guint n_lines;
  guint first;

  path = xfce_resource_lookup (XFCE_RESOURCE_DATA, HISTORY_PATH);
  if (path == NULL)
    return;

  if (!g_file_get_contents (path, &contents, NULL, NULL))
    {
      g_free (path);
      return;
    }

  g_free (path);

  lines = g_strsplit (contents, "\n", -1);
  g_free (contents);

  /* only keep the newest entries, they are stored last */
  max_size = terminal_history_get_max_size (history);
  n_lines = g_strv_length (lines);
  first = n_lines > max_size ? n_lines - max_size : 0;

  for (guint i = first; lines[i] != NULL; i++)
    if (IS_STRING (lines[i]))
      g_ptr_array_add (history->commands, g_strdup (lines[i]));

  g_strfreev (lines);
}



static gboolean
terminal_history_save (gpointer user_data)
{
  TerminalHistory *history = TERMINAL_HISTORY (user_data);
  GString *contents;
  gchar *path;

  history->save_id = 0;

  path = xfce_resource_save_location (XFCE_RESOURCE_DATA, HISTORY_PATH, TRUE);
  if (G_UNLIKELY (path == NULL))
    return FALSE;

  contents = g_string_new (NULL);
  for (guint i = 0; i < history->commands->len; i++)
    {
      g_string_append (contents, g_ptr_array_index (history->commands, i));
      g_string_append_c (contents, '\n');
    }

  if (!g_file_set_contents (path, contents->str, contents->len, NULL))
    g_warning ("Failed to save the command history to \"%s\"", path);

  g_string_free (contents, TRUE);
  g_free (path);

  return FALSE;
}



static void
terminal_history_queue_save (TerminalHistory *history)
{
  if (history->save_id == 0)
    history->save_id = gdk_threads_add_timeout_seconds (HISTORY_SAVE_DELAY, terminal_history_save, history);
}



/**
 * terminal_history_get:
 *
 * Returns the singleton command history, which is loaded from disk on first
 * use. The caller owns a reference and must release it with g_object_unref().
 **/
TerminalHistory *
terminal_history_get (void)
{
  static TerminalHistory *history = NULL;

  if (G_UNLIKELY (history == NULL))
    {
      history = g_object_new (TERMINAL_TYPE_HISTORY, NULL);
      g_object_add_weak_pointer (G_OBJECT (history), (gpointer) &history);
    }
  else
    {
      g_object_ref (G_OBJECT (history));
    }

  return history;
}



/**
 * terminal_history_add:
 * @history : a #TerminalHistory.
 * @command : the command line to remember.
 *
 * Appends @command as the most recent entry, dropping an older occurrence of
 * the same command and trimming the history to its configured size.
 **/
void
terminal_history_add (TerminalHistory *history,
                      const gchar *command)
{
  gboolean ignore_space;
  guint max_size;

  g_return_if_fail (TERMINAL_IS_HISTORY (history));

  if (!IS_STRING (command))
    return;

  /* honor the shell convention of hiding space-prefixed commands */
  g_object_get (G_OBJECT (history->preferences), "misc-command-history-ignore-space", &ignore_space, NULL);
  if (ignore_space && g_ascii_isspace (command[0]))
    return;

  /* commands are unique, so the most recent use wins */
  for (guint i = 0; i < history->commands->len; i++)
    if (g_strcmp0 (g_ptr_array_index (history->commands, i), command) == 0)
      {
        g_ptr_array_remove_index (history->commands, i);
        break;
      }

  g_ptr_array_add (history->commands, g_strdup (command));

  max_size = terminal_history_get_max_size (history);
  while (history->commands->len > max_size)
    g_ptr_array_remove_index (history->commands, 0);

  terminal_history_queue_save (history);
}



/**
 * terminal_history_lookup:
 * @history : a #TerminalHistory.
 * @prefix  : the text typed so far.
 *
 * Returns the most recent command starting with @prefix, or %NULL if there is
 * none. The returned string is owned by @history.
 **/
const gchar *
terminal_history_lookup (TerminalHistory *history,
                         const gchar *prefix)
{
  g_return_val_if_fail (TERMINAL_IS_HISTORY (history), NULL);

  if (!IS_STRING (prefix))
    return NULL;

  for (guint i = history->commands->len; i > 0; i--)
    {
      const gchar *command = g_ptr_array_index (history->commands, i - 1);

      /* a command equal to the prefix would suggest nothing */
      if (g_str_has_prefix (command, prefix) && strlen (command) > strlen (prefix))
        return command;
    }

  return NULL;
}



static gboolean
terminal_history_matches (const gchar *haystack,
                          const gchar *needle)
{
  const gchar *p = haystack;

  /* every character of the query in order, but not necessarily adjacent */
  for (const gchar *n = needle; *n != '\0'; n = g_utf8_next_char (n))
    {
      gunichar nc = g_utf8_get_char (n);

      for (;; p = g_utf8_next_char (p))
        {
          if (*p == '\0')
            return FALSE;
          if (g_unichar_tolower (g_utf8_get_char (p)) == nc)
            break;
        }

      p = g_utf8_next_char (p);
    }

  return TRUE;
}



/**
 * terminal_history_search:
 * @history : a #TerminalHistory.
 * @query   : the search string, or %NULL or the empty string for everything.
 *
 * Returns the matching commands, most recent first. Exact substring matches
 * come before looser subsequence matches. The returned #GPtrArray must be
 * freed by the caller, its strings are owned by @history.
 **/
GPtrArray *
terminal_history_search (TerminalHistory *history,
                         const gchar *query)
{
  GPtrArray *result;
  GPtrArray *fuzzy;
  gchar *folded;

  g_return_val_if_fail (TERMINAL_IS_HISTORY (history), NULL);

  result = g_ptr_array_new ();

  if (!IS_STRING (query))
    {
      for (guint i = history->commands->len; i > 0; i--)
        g_ptr_array_add (result, g_ptr_array_index (history->commands, i - 1));

      return result;
    }

  folded = g_utf8_strdown (query, -1);
  fuzzy = g_ptr_array_new ();

  for (guint i = history->commands->len; i > 0; i--)
    {
      const gchar *command = g_ptr_array_index (history->commands, i - 1);
      gchar *command_folded = g_utf8_strdown (command, -1);

      if (strstr (command_folded, folded) != NULL)
        g_ptr_array_add (result, (gpointer) command);
      else if (terminal_history_matches (command_folded, folded))
        g_ptr_array_add (fuzzy, (gpointer) command);

      g_free (command_folded);
    }

  /* looser matches go last */
  for (guint i = 0; i < fuzzy->len; i++)
    g_ptr_array_add (result, g_ptr_array_index (fuzzy, i));

  g_ptr_array_free (fuzzy, TRUE);
  g_free (folded);

  return result;
}



/**
 * terminal_history_clear:
 * @history : a #TerminalHistory.
 *
 * Forgets every command and removes the history file.
 **/
void
terminal_history_clear (TerminalHistory *history)
{
  g_return_if_fail (TERMINAL_IS_HISTORY (history));

  if (history->commands->len > 0)
    g_ptr_array_remove_range (history->commands, 0, history->commands->len);

  terminal_history_flush (history);
}



/**
 * terminal_history_flush:
 * @history : a #TerminalHistory.
 *
 * Writes any pending changes to disk right away.
 **/
void
terminal_history_flush (TerminalHistory *history)
{
  g_return_if_fail (TERMINAL_IS_HISTORY (history));

  g_clear_handle_id (&history->save_id, g_source_remove);
  terminal_history_save (history);
}

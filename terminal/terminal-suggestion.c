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

#include "terminal-history.h"
#include "terminal-preferences.h"
#include "terminal-private.h"
#include "terminal-suggestion.h"

/* the shell integration snippets report the command line they are about to
 * forget in this termprop, and mark the start of the input area with the
 * other one; the exit status arrives in VTE_TERMPROP_SHELL_POSTEXEC.
 *
 * VTE only reports a termprop which actually changed, so the snippets unset
 * the prompt termprop right before setting it again on every prompt */
#define TERMPROP_COMMAND "vte.ext.xfceterm.command"
#define TERMPROP_PROMPT "vte.ext.xfceterm.prompt"

/* how much of the terminal foreground color is left in the ghost text */
#define SUGGESTION_ALPHA 0.45



struct _TerminalSuggestion
{
  GObject parent_instance;

  GtkOverlay *overlay;
  VteTerminal *terminal;

  TerminalPreferences *preferences;
  TerminalHistory *history;

  /* dimmed text drawn after the cursor, %NULL until first needed */
  GtkWidget *label;

  /* the part of the match which is not typed yet, or %NULL when nothing is shown */
  gchar *pending;

  /* the command reported by the shell and its exit status; VTE does not
   * guarantee the order the two termprops arrive in, so both are kept until
   * the pair is complete */
  gchar *command;
  guint64 status;
  gboolean has_status;

  /* where the input area starts, only meaningful while at_prompt is TRUE */
  glong anchor_row;
  glong anchor_col;
  gboolean at_prompt;

  /* what the shown ghost text was computed from, to skip the work when
   * nothing relevant changed; VTE emits both cursor-moved and
   * contents-changed for a single keystroke */
  gchar *shown_for;
  glong shown_row;
  glong shown_col;

  gboolean enabled;
};



static void
terminal_suggestion_finalize (GObject *object);
#if VTE_CHECK_VERSION(0, 78, 0)
static void
terminal_suggestion_update (TerminalSuggestion *suggestion);
#endif



G_DEFINE_TYPE (TerminalSuggestion, terminal_suggestion, G_TYPE_OBJECT)



static void
terminal_suggestion_class_init (TerminalSuggestionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = terminal_suggestion_finalize;
}



static void
terminal_suggestion_init (TerminalSuggestion *suggestion)
{
  suggestion->preferences = terminal_preferences_get ();
  suggestion->history = terminal_history_get ();
}



static void
terminal_suggestion_finalize (GObject *object)
{
  TerminalSuggestion *suggestion = TERMINAL_SUGGESTION (object);

  if (suggestion->label != NULL)
    {
      g_object_remove_weak_pointer (G_OBJECT (suggestion->label), (gpointer) &suggestion->label);
      gtk_widget_destroy (suggestion->label);
    }

  g_free (suggestion->pending);
  g_free (suggestion->command);
  g_free (suggestion->shown_for);
  g_object_unref (G_OBJECT (suggestion->preferences));
  g_object_unref (G_OBJECT (suggestion->history));

  (*G_OBJECT_CLASS (terminal_suggestion_parent_class)->finalize) (object);
}



#if VTE_CHECK_VERSION(0, 78, 0)
static void
terminal_suggestion_reload_enabled (TerminalSuggestion *suggestion)
{
  g_object_get (G_OBJECT (suggestion->preferences), "misc-command-suggestions", &suggestion->enabled, NULL);
}



/**
 * terminal_suggestion_get_input:
 *
 * Returns the text between the start of the input area and the cursor, or
 * %NULL if the cursor is not sitting in a known input area. Free with g_free().
 **/
static gchar *
terminal_suggestion_get_input (TerminalSuggestion *suggestion)
{
  glong column, row;

  if (!suggestion->at_prompt)
    return NULL;

  vte_terminal_get_cursor_position (suggestion->terminal, &column, &row);

  /* input which wrapped onto another row is not handled */
  if (row != suggestion->anchor_row || column < suggestion->anchor_col)
    return NULL;

  if (column == suggestion->anchor_col)
    return g_strdup ("");

  return vte_terminal_get_text_range_format (suggestion->terminal, VTE_FORMAT_TEXT,
                                             suggestion->anchor_row, suggestion->anchor_col,
                                             row, column, NULL);
}



static void
terminal_suggestion_update_label (TerminalSuggestion *suggestion)
{
  GtkStyleContext *context;
  PangoAttrList *attributes;
  const PangoFontDescription *font;
  GdkRGBA color;
  gboolean use_theme;
  gboolean has_color;

  if (suggestion->label == NULL)
    return;

  /* the ghost text has to line up with the terminal grid */
  font = vte_terminal_get_font (suggestion->terminal);
  attributes = pango_attr_list_new ();
  if (font != NULL)
    pango_attr_list_insert (attributes, pango_attr_font_desc_new (font));

  g_object_get (G_OBJECT (suggestion->preferences), "color-use-theme", &use_theme, NULL);
  has_color = terminal_preferences_get_color (suggestion->preferences, "color-foreground", &color);
  if (use_theme || !has_color)
    {
      context = gtk_widget_get_style_context (GTK_WIDGET (suggestion->terminal));
      gtk_style_context_get_color (context, GTK_STATE_FLAG_ACTIVE, &color);
    }

  pango_attr_list_insert (attributes, pango_attr_foreground_new (color.red * G_MAXUINT16,
                                                                 color.green * G_MAXUINT16,
                                                                 color.blue * G_MAXUINT16));
  pango_attr_list_insert (attributes, pango_attr_foreground_alpha_new (SUGGESTION_ALPHA * G_MAXUINT16));

  gtk_label_set_attributes (GTK_LABEL (suggestion->label), attributes);
  pango_attr_list_unref (attributes);
}



static gboolean
terminal_suggestion_get_child_position (GtkOverlay *overlay,
                                        GtkWidget *widget,
                                        GdkRectangle *allocation,
                                        TerminalSuggestion *suggestion)
{
  GtkStyleContext *context;
  GtkBorder padding, border;
  GtkStateFlags state;
  glong column, row;
  gint terminal_x, terminal_y;
  gint columns_left;

  if (widget != suggestion->label)
    return FALSE;

  if (!gtk_widget_translate_coordinates (GTK_WIDGET (suggestion->terminal),
                                         GTK_WIDGET (overlay), 0, 0,
                                         &terminal_x, &terminal_y))
    return FALSE;

  vte_terminal_get_cursor_position (suggestion->terminal, &column, &row);

  context = gtk_widget_get_style_context (GTK_WIDGET (suggestion->terminal));
  state = gtk_style_context_get_state (context);
  gtk_style_context_get_padding (context, state, &padding);
  gtk_style_context_get_border (context, state, &border);

  allocation->x = terminal_x + padding.left + border.left
                  + column * vte_terminal_get_char_width (suggestion->terminal);
  allocation->y = terminal_y + padding.top + border.top
                  + (row - (glong) gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (suggestion->terminal))))
                      * vte_terminal_get_char_height (suggestion->terminal);

  /* the ghost text stops at the right edge of the terminal */
  columns_left = vte_terminal_get_column_count (suggestion->terminal) - column;
  allocation->width = MAX (columns_left, 0) * vte_terminal_get_char_width (suggestion->terminal);
  allocation->height = vte_terminal_get_char_height (suggestion->terminal);

  return TRUE;
}



static void
terminal_suggestion_show (TerminalSuggestion *suggestion,
                          const gchar *text)
{
  if (suggestion->label == NULL)
    {
      suggestion->label = gtk_label_new (NULL);
      gtk_label_set_ellipsize (GTK_LABEL (suggestion->label), PANGO_ELLIPSIZE_END);
      gtk_label_set_xalign (GTK_LABEL (suggestion->label), 0.0);
      gtk_label_set_yalign (GTK_LABEL (suggestion->label), 0.0);
      gtk_label_set_single_line_mode (GTK_LABEL (suggestion->label), TRUE);
      gtk_widget_set_halign (suggestion->label, GTK_ALIGN_START);
      gtk_widget_set_valign (suggestion->label, GTK_ALIGN_START);
      gtk_widget_set_can_focus (suggestion->label, FALSE);

      g_signal_connect_object (G_OBJECT (suggestion->overlay), "get-child-position",
                               G_CALLBACK (terminal_suggestion_get_child_position), suggestion, 0);

      gtk_overlay_add_overlay (suggestion->overlay, suggestion->label);
      gtk_overlay_set_overlay_pass_through (suggestion->overlay, suggestion->label, TRUE);

      /* the overlay destroys its children before the screen drops us */
      g_object_add_weak_pointer (G_OBJECT (suggestion->label), (gpointer) &suggestion->label);

      terminal_suggestion_update_label (suggestion);
    }

  gtk_label_set_text (GTK_LABEL (suggestion->label), text);
  gtk_widget_show (suggestion->label);

  /* the cursor may have moved without the text changing */
  gtk_widget_queue_resize (GTK_WIDGET (suggestion->overlay));
}



static void
terminal_suggestion_hide (TerminalSuggestion *suggestion)
{
  g_clear_pointer (&suggestion->pending, g_free);
  g_clear_pointer (&suggestion->shown_for, g_free);

  if (suggestion->label != NULL)
    gtk_widget_hide (suggestion->label);
}



static void
terminal_suggestion_update (TerminalSuggestion *suggestion)
{
  const gchar *match;
  gchar *input;
  glong column, row;

  if (!suggestion->enabled)
    {
      terminal_suggestion_hide (suggestion);
      return;
    }

  input = terminal_suggestion_get_input (suggestion);
  if (!IS_STRING (input))
    {
      g_free (input);
      terminal_suggestion_hide (suggestion);
      return;
    }

  /* a single keystroke reaches us twice, and the ghost only has to be
   * recomputed when the input or the place it is drawn at changed */
  vte_terminal_get_cursor_position (suggestion->terminal, &column, &row);
  if (suggestion->shown_for != NULL
      && column == suggestion->shown_col
      && row == suggestion->shown_row
      && g_strcmp0 (suggestion->shown_for, input) == 0)
    {
      g_free (input);
      return;
    }

  match = terminal_history_lookup (suggestion->history, input);
  if (match == NULL)
    {
      g_free (input);
      terminal_suggestion_hide (suggestion);
      return;
    }

  g_free (suggestion->pending);
  suggestion->pending = g_strdup (match + strlen (input));

  g_free (suggestion->shown_for);
  suggestion->shown_for = input;
  suggestion->shown_col = column;
  suggestion->shown_row = row;

  terminal_suggestion_show (suggestion, suggestion->pending);
}



/**
 * terminal_suggestion_commit:
 *
 * Remembers the reported command once both its text and its exit status have
 * arrived, and only if it succeeded.
 **/
static void
terminal_suggestion_commit (TerminalSuggestion *suggestion)
{
  if (suggestion->command == NULL || !suggestion->has_status)
    return;

  if (suggestion->status == 0 && suggestion->enabled)
    terminal_history_add (suggestion->history, suggestion->command);

  g_clear_pointer (&suggestion->command, g_free);
  suggestion->has_status = FALSE;
}



static void
terminal_suggestion_termprop_changed (VteTerminal *terminal,
                                      const gchar *prop,
                                      TerminalSuggestion *suggestion)
{
  if (g_strcmp0 (prop, TERMPROP_COMMAND) == 0)
    {
      const gchar *command = vte_terminal_get_termprop_string (terminal, prop, NULL);

      g_free (suggestion->command);
      suggestion->command = g_strdup (command);

      suggestion->at_prompt = FALSE;
      terminal_suggestion_hide (suggestion);
      terminal_suggestion_commit (suggestion);
    }
  else if (g_strcmp0 (prop, VTE_TERMPROP_SHELL_POSTEXEC) == 0)
    {
      /* the value is ephemeral, it is only readable from here */
      suggestion->has_status = vte_terminal_get_termprop_uint (terminal, prop, &suggestion->status);

      suggestion->at_prompt = FALSE;
      terminal_suggestion_hide (suggestion);
      terminal_suggestion_commit (suggestion);
    }
  else if (g_strcmp0 (prop, TERMPROP_PROMPT) == 0)
    {
      /* the unset half of the snippets' reset carries no value, skip it */
      if (vte_terminal_get_termprop_string (terminal, prop, NULL) != NULL)
        {
          /* the shell marks the end of its prompt, so this is where input starts */
          vte_terminal_get_cursor_position (terminal, &suggestion->anchor_col, &suggestion->anchor_row);
          suggestion->at_prompt = TRUE;

          terminal_suggestion_update (suggestion);
        }
    }
}



static void
terminal_suggestion_preferences_changed (TerminalSuggestion *suggestion,
                                         GParamSpec *pspec)
{
  const gchar *name = g_param_spec_get_name (pspec);

  if (g_str_has_prefix (name, "color-") || g_str_has_prefix (name, "font-"))
    terminal_suggestion_update_style (suggestion);
  else if (g_strcmp0 (name, "misc-command-suggestions") == 0)
    {
      terminal_suggestion_reload_enabled (suggestion);
      terminal_suggestion_update (suggestion);
    }
}
#endif /* VTE_CHECK_VERSION(0, 78, 0) */



/**
 * terminal_suggestion_install_termprops:
 *
 * Registers the termprops the shell integration snippets use. This affects a
 * process wide registry, so it must be called once before any terminal is
 * created.
 **/
void
terminal_suggestion_install_termprops (void)
{
#if VTE_CHECK_VERSION(0, 78, 0)
  vte_install_termprop (TERMPROP_COMMAND, VTE_PROPERTY_STRING, VTE_PROPERTY_FLAG_NONE);
  vte_install_termprop (TERMPROP_PROMPT, VTE_PROPERTY_STRING, VTE_PROPERTY_FLAG_NONE);
#endif
}



/**
 * terminal_suggestion_new:
 * @overlay  : the #GtkOverlay the ghost text is drawn in.
 * @terminal : the #VteTerminal inside @overlay.
 *
 * Creates the controller which turns shell reports into inline command
 * suggestions. With a VTE older than 0.78 it is inert, because the termprops
 * the shell integration relies on do not exist there.
 **/
TerminalSuggestion *
terminal_suggestion_new (GtkOverlay *overlay,
                         VteTerminal *terminal)
{
  TerminalSuggestion *suggestion;

  g_return_val_if_fail (GTK_IS_OVERLAY (overlay), NULL);
  g_return_val_if_fail (VTE_IS_TERMINAL (terminal), NULL);

  suggestion = g_object_new (TERMINAL_TYPE_SUGGESTION, NULL);
  suggestion->overlay = overlay;
  suggestion->terminal = terminal;

#if VTE_CHECK_VERSION(0, 78, 0)
  terminal_suggestion_reload_enabled (suggestion);

  g_signal_connect_object (G_OBJECT (terminal), "termprop-changed",
                           G_CALLBACK (terminal_suggestion_termprop_changed), suggestion, 0);
  g_signal_connect_object (G_OBJECT (terminal), "cursor-moved",
                           G_CALLBACK (terminal_suggestion_update), suggestion, G_CONNECT_SWAPPED);
  g_signal_connect_object (G_OBJECT (terminal), "contents-changed",
                           G_CALLBACK (terminal_suggestion_update), suggestion, G_CONNECT_SWAPPED);
  g_signal_connect_object (G_OBJECT (suggestion->preferences), "notify",
                           G_CALLBACK (terminal_suggestion_preferences_changed), suggestion, G_CONNECT_SWAPPED);
#endif

  return suggestion;
}



/**
 * terminal_suggestion_accept:
 * @suggestion : a #TerminalSuggestion.
 *
 * Sends the currently shown suggestion to the shell. Returns %TRUE if there
 * was something to accept.
 **/
gboolean
terminal_suggestion_accept (TerminalSuggestion *suggestion)
{
  g_return_val_if_fail (TERMINAL_IS_SUGGESTION (suggestion), FALSE);

  if (!IS_STRING (suggestion->pending))
    return FALSE;

  vte_terminal_feed_child (suggestion->terminal, suggestion->pending, strlen (suggestion->pending));

  return TRUE;
}



/**
 * terminal_suggestion_update_style:
 * @suggestion : a #TerminalSuggestion.
 *
 * Makes the ghost text pick up the current terminal font and colors.
 **/
void
terminal_suggestion_update_style (TerminalSuggestion *suggestion)
{
  g_return_if_fail (TERMINAL_IS_SUGGESTION (suggestion));

#if VTE_CHECK_VERSION(0, 78, 0)
  terminal_suggestion_update_label (suggestion);
#endif
}

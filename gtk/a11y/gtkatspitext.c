/* gtkatspitext.c: Text interface for GtkAtspiContext
 *
 * Copyright 2020 Red Hat, Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gtkatspitextprivate.h"

#include "gtkatspiprivate.h"
#include "gtkatspiutilsprivate.h"
#include "gtkatspipangoprivate.h"
#include "gtkatspitextbufferprivate.h"

#include "a11y/atspi/atspi-text.h"

#include "gtkatcontextprivate.h"
#include "gtkdebug.h"
#include "gtkeditable.h"
#include "gtkentryprivate.h"
#include "gtkinscriptionprivate.h"
#include "gtklabelprivate.h"
#include "gtkpangoprivate.h"
#include "gtkpasswordentryprivate.h"
#include "gtksearchentryprivate.h"
#include "gtkspinbuttonprivate.h"
#include "gtktextbufferprivate.h"
#include "gtktextviewprivate.h"
#include "gtkaccessibletext-private.h"

#include <gio/gio.h>

static GtkAccessibleTextGranularity
atspi_granularity_to_gtk (AtspiTextGranularity granularity)
{
  switch (granularity)
    {
    case ATSPI_TEXT_GRANULARITY_CHAR:
      return GTK_ACCESSIBLE_TEXT_GRANULARITY_CHARACTER;
    case ATSPI_TEXT_GRANULARITY_WORD:
      return GTK_ACCESSIBLE_TEXT_GRANULARITY_WORD;
    case ATSPI_TEXT_GRANULARITY_SENTENCE:
      return GTK_ACCESSIBLE_TEXT_GRANULARITY_SENTENCE;
    case ATSPI_TEXT_GRANULARITY_LINE:
      return GTK_ACCESSIBLE_TEXT_GRANULARITY_LINE;
    case ATSPI_TEXT_GRANULARITY_PARAGRAPH:
      return GTK_ACCESSIBLE_TEXT_GRANULARITY_PARAGRAPH;
    default:
      g_assert_not_reached ();
    }
}

/* {{{ GtkAccessibleText */

static void
accessible_text_handle_method (GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data)
{
  GtkATContext *self = user_data;
  GtkAccessible *accessible = gtk_at_context_get_accessible (self);
  GtkAccessibleText *accessible_text = GTK_ACCESSIBLE_TEXT (accessible);

  if (g_strcmp0 (method_name, "GetCaretOffset") == 0)
    {
      guint offset;

      offset = gtk_accessible_text_get_caret_position (accessible_text);

      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(i)", (int)offset));
    }
  else if (g_strcmp0 (method_name, "SetCaretOffset") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "GetText") == 0)
    {
      int start, end;
      GBytes *contents;

      g_variant_get (parameters, "(ii)", &start, &end);

      contents = gtk_accessible_text_get_contents (accessible_text, start, end < 0 ? G_MAXUINT : end);

      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", g_bytes_get_data (contents, NULL)));

      g_bytes_unref (contents);
    }
  else if (g_strcmp0 (method_name, "GetTextBeforeOffset") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This method is deprecated in favor of GetStringAtOffset");
    }
  else if (g_strcmp0 (method_name, "GetTextAtOffset") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This method is deprecated in favor of GetStringAtOffset");
    }
  else if (g_strcmp0 (method_name, "GetTextAfterOffset") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This method is deprecated in favor of GetStringAtOffset");
    }
  else if (g_strcmp0 (method_name, "GetCharacterAtOffset") == 0)
    {
      int offset;
      gunichar ch = 0;

      g_variant_get (parameters, "(i)", &offset);

      GBytes *text = gtk_accessible_text_get_contents (accessible_text, offset, offset + 1);

      if (text != NULL)
        {
          const char *str = g_bytes_get_data (text, NULL);

          if (0 <= offset && offset < g_utf8_strlen (str, -1))
            ch = g_utf8_get_char (g_utf8_offset_to_pointer (str, offset));
        }

      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(i)", ch));
    }
  else if (g_strcmp0 (method_name, "GetStringAtOffset") == 0)
    {
      unsigned int start, end;
      int offset;
      AtspiTextGranularity granularity;
      GBytes *bytes;

      g_variant_get (parameters, "(iu)", &offset, &granularity);

      bytes = gtk_accessible_text_get_contents_at (accessible_text, offset,
                                                   atspi_granularity_to_gtk (granularity),
                                                   &start, &end);

      if (bytes == NULL)
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(sii)", "", -1, -1));
      else
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(sii)", g_bytes_get_data (bytes, NULL), start, end));

      g_bytes_unref (bytes);
    }
  else if (g_strcmp0 (method_name, "GetAttributes") == 0)
    {
      GVariantBuilder builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{ss}"));
      int offset;
      gsize n_attrs = 0;
      GtkAccessibleTextRange *ranges = NULL;
      int start, end;
      char **attr_names = NULL;
      char **attr_values = NULL;

      g_variant_get (parameters, "(i)", &offset);

      gtk_accessible_text_get_attributes (accessible_text,
                                          offset,
                                          &n_attrs,
                                          &ranges,
                                          &attr_names,
                                          &attr_values);

      start = 0;
      end = G_MAXINT;

      for (int i = 0; i < n_attrs; i++)
        {
          g_variant_builder_add (&builder, "{ss}", attr_names[i], attr_values[i]);
          start = MAX (start, ranges[i].start);
          end = MIN (end, start + ranges[i].length);
        }

      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a{ss}ii)", &builder, start, end));

      g_clear_pointer (&ranges, g_free);
      g_strfreev (attr_names);
      g_strfreev (attr_values);
    }
  else if (g_strcmp0 (method_name, "GetAttributeValue") == 0)
    {
      int offset;
      const char *name;
      const char *val = "";
      char **names, **values;
      GtkAccessibleTextRange *ranges;
      gsize n_ranges;

      g_variant_get (parameters, "(i&s)", &offset, &name);

      gtk_accessible_text_get_attributes (accessible_text, offset,
                                          &n_ranges, &ranges,
                                          &names, &values);

      for (unsigned i = 0; names[i] != NULL; i++)
        {
          if (g_strcmp0 (names[i], name) == 0)
            {
              val = values[i];
              break;
            }
        }

      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", val));

      g_strfreev (names);
      g_strfreev (values);
    }
  else if (g_strcmp0 (method_name, "GetAttributeRun") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "GetDefaultAttributes") == 0 ||
           g_strcmp0 (method_name, "GetDefaultAttributeSet") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "GetNSelections") == 0)
    {
      gsize n_ranges;
      GtkAccessibleTextRange *ranges = NULL;

      gtk_accessible_text_get_selection (accessible_text, &n_ranges, &ranges);

      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(i)", (int)n_ranges));

      g_clear_pointer (&ranges, g_free);
    }
  else if (g_strcmp0 (method_name, "GetSelection") == 0)
    {
      int num;
      gsize n_ranges;
      GtkAccessibleTextRange *ranges = NULL;

      g_variant_get (parameters, "(i)", &num);

      gtk_accessible_text_get_selection (accessible_text, &n_ranges, &ranges);

      if (num < 0 || num >= n_ranges)
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Not a valid selection: %d", num);
      else
        {
          int start = ranges[num].start;
          int end = start + ranges[num].length;

          g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ii)", start, end));
        }

      g_clear_pointer (&ranges, g_free);
    }
  else if (g_strcmp0 (method_name, "AddSelection") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "RemoveSelection") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "SetSelection") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "GetCharacterExtents") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "GetRangeExtents") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "GetBoundedRanges") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "ScrollSubstringTo") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
  else if (g_strcmp0 (method_name, "ScrollSubstringToPoint") == 0)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
}

static GVariant *
accessible_text_get_property (GDBusConnection  *connection,
                              const gchar      *sender,
                              const gchar      *object_path,
                              const gchar      *interface_name,
                              const gchar      *property_name,
                              GError          **error,
                              gpointer          user_data)
{
  GtkATContext *self = user_data;
  GtkAccessible *accessible = gtk_at_context_get_accessible (self);
  GtkAccessibleText *accessible_text = GTK_ACCESSIBLE_TEXT (accessible);

  if (g_strcmp0 (property_name, "CharacterCount") == 0)
    {
      GBytes *contents;
      const char *str;
      gsize len;

      contents = gtk_accessible_text_get_contents (accessible_text, 0, G_MAXUINT);
      str = g_bytes_get_data (contents, NULL);
      len = g_utf8_strlen (str, -1);
      g_bytes_unref (contents);

      return g_variant_new_int32 ((int) len);
    }
  else if (g_strcmp0 (property_name, "CaretOffset") == 0)
    {
      guint offset;

      offset = gtk_accessible_text_get_caret_position (accessible_text);

      return g_variant_new_int32 ((int) offset);
    }

  return NULL;
}

static const GDBusInterfaceVTable accessible_text_vtable = {
  accessible_text_handle_method,
  accessible_text_get_property,
  NULL,
};

/* }}} */

const GDBusInterfaceVTable *
gtk_atspi_get_text_vtable (GtkAccessible *accessible)
{
  if (GTK_IS_ACCESSIBLE_TEXT (accessible))
    return &accessible_text_vtable;

  return NULL;
}

/* vim:set foldmethod=marker expandtab: */

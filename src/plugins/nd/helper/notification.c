/*
 * eventd - Small daemon to act on remote or local events
 *
 * Copyright © 2011-2012 Quentin "Sardem FF7" Glidic
 *
 * This file is part of eventd.
 *
 * eventd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * eventd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with eventd. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <glib.h>
#include <glib-object.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <libeventd-event.h>
#include <libeventd-config.h>
#include <libeventd-regex.h>

#include <eventd-nd-notification.h>

struct _EventdNdNotification {
    gchar *title;
    gchar *message;
    GdkPixbuf *image;
    GdkPixbuf *icon;
};

void
eventd_nd_notification_init()
{
    libeventd_regex_init();
}

void
eventd_nd_notification_uninit()
{
    libeventd_regex_clean();
}

static GdkPixbuf *
_eventd_nd_notification_pixbuf_from_file(const gchar *path)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf;

    if ( *path == 0 )
        return NULL;

    if ( ( pixbuf = gdk_pixbuf_new_from_file(path, &error) ) == NULL )
        g_warning("Couldn’t load file '%s': %s", path, error->message);
    g_clear_error(&error);

    return pixbuf;
}

static void
_eventd_nd_notification_pixbuf_data_free(guchar *pixels, gpointer data)
{
    g_free(pixels);
}

static GdkPixbuf *
_eventd_nd_notification_pixbuf_from_base64(EventdEvent *event, const gchar *name)
{
    GdkPixbuf *pixbuf = NULL;
    const gchar *base64;
    guchar *data;
    gsize length;
    const gchar *format;
    gchar *format_name;

    base64 = eventd_event_get_data(event, name);
    if ( base64 == NULL )
        return NULL;
    data = g_base64_decode(base64, &length);

    format_name = g_strconcat(name, "-format", NULL);
    format = eventd_event_get_data(event, format_name);
    g_free(format_name);

    if ( format != NULL )
    {
        gint width, height;
        gint stride;
        gboolean alpha;
        gchar *f;

        width = g_ascii_strtoll(format, &f, 16);
        height = g_ascii_strtoll(f+1, &f, 16);
        stride = g_ascii_strtoll(f+1, &f, 16);
        alpha = g_ascii_strtoll(f+1, &f, 16);

        pixbuf = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, alpha, alpha ? 4 : 3, width, height, stride, _eventd_nd_notification_pixbuf_data_free, NULL);
    }
    else
    {
        GError *error = NULL;
        GdkPixbufLoader *loader;

        loader = gdk_pixbuf_loader_new();

        if ( ! gdk_pixbuf_loader_write(loader, data, length, &error) )
        {
            g_warning("Couldn’t write image data: %s", error->message);
            g_clear_error(&error);
            goto error;
        }

        if ( ! gdk_pixbuf_loader_close(loader, &error) )
        {
            g_warning("Couldn’t load image data: %s", error->message);
            g_clear_error(&error);
            goto error;
        }

        pixbuf = g_object_ref(gdk_pixbuf_loader_get_pixbuf(loader));

    error:
        g_object_unref(loader);
        g_free(data);
    }

    return pixbuf;
}

EventdNdNotification *
eventd_nd_notification_new(EventdEvent *event, const gchar *title, const gchar *message, const gchar *image_name, const gchar *icon_name)
{
    EventdNdNotification *self;
    gchar *path;

    self = g_new0(EventdNdNotification, 1);

    self->title = libeventd_regex_replace_event_data(title, event, NULL, NULL);

    self->message = libeventd_regex_replace_event_data(message, event, NULL, NULL);

    if ( ( path = libeventd_config_get_filename(image_name, event, "icons") ) != NULL )
    {
        self->image = _eventd_nd_notification_pixbuf_from_file(path);
        g_free(path);
    }
    else
       self->image =  _eventd_nd_notification_pixbuf_from_base64(event, image_name);

    if ( ( path = libeventd_config_get_filename(icon_name, event, "icons") ) != NULL )
    {
        self->icon = _eventd_nd_notification_pixbuf_from_file(path);
        g_free(path);
    }
    else
        self->icon = _eventd_nd_notification_pixbuf_from_base64(event, icon_name);

    return self;
}

void
eventd_nd_notification_free(EventdNdNotification *self)
{
    if ( self->icon != NULL )
        g_object_unref(self->icon);
    if ( self->image != NULL )
        g_object_unref(self->image);
    g_free(self->message);
    g_free(self->title);

    g_free(self);
}

const gchar *
eventd_nd_notification_get_title(EventdNdNotification *self)
{
    return self->title;
}

const gchar *
eventd_nd_notification_get_message(EventdNdNotification *self)
{
    return self->message;
}

GdkPixbuf *
eventd_nd_notification_get_image(EventdNdNotification *self)
{
    return self->image;
}

GdkPixbuf *
eventd_nd_notification_get_icon(EventdNdNotification *self)
{
    return self->icon;
}


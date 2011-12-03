/*
 * eventd - Small daemon to act on remote or local events
 *
 * Copyright © 2011 Quentin "Sardem FF7" Glidic
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

#include <cairo.h>

#include <cairo-xcb.h>
#include <xcb/xcb.h>
#include <xcb/shape.h>

#include "../types.h"

#include "backend.h"

struct _EventdNdDisplay {
    xcb_connection_t *xcb_connection;
    xcb_screen_t *screen;
};

struct _EventdNdSurface {
    xcb_connection_t *xcb_connection;
    xcb_window_t window;
};

xcb_visualtype_t *
get_root_visual_type(xcb_screen_t *s)
{
    xcb_visualtype_t *visual_type = NULL;
    xcb_depth_iterator_t depth_iter;

    for ( depth_iter = xcb_screen_allowed_depths_iterator(s) ; depth_iter.rem ; xcb_depth_next(&depth_iter) )
    {
        xcb_visualtype_iterator_t visual_iter;
        for ( visual_iter = xcb_depth_visuals_iterator(depth_iter.data) ; visual_iter.rem ; xcb_visualtype_next(&visual_iter) )
        {
            if ( s->root_visual == visual_iter.data->visual_id )
            {
                visual_type = visual_iter.data;
                break;
            }
        }
    }

    return visual_type;
}

EventdNdDisplay *
eventd_nd_display_new(const gchar *target)
{
    EventdNdDisplay *context;
    xcb_connection_t *c;

    c = xcb_connect(target, NULL);
    if ( xcb_connection_has_error(c) )
    {
        xcb_disconnect(c);
        return NULL;
    }

    context = g_new0(EventdNdDisplay, 1);

    context->xcb_connection = c;

    context->screen = xcb_setup_roots_iterator(xcb_get_setup(context->xcb_connection)).data;

    return context;
}

void
eventd_nd_display_free(gpointer data)
{
    EventdNdDisplay *context = data;
    xcb_disconnect(context->xcb_connection);
    g_free(context);
}

EventdNdSurface *
eventd_nd_surface_new(EventdNdDisplay *context, gint margin, EventdNdStyleAnchor anchor, gint width, gint height, cairo_surface_t *bubble, cairo_surface_t *shape)
{
    guint32 selmask = XCB_CW_OVERRIDE_REDIRECT;
    guint32 selval[] = { 1 };
    gint x = 0, y = 0;
    gint w = 0, h = 0;
    EventdNdSurface *surface;
    cairo_surface_t *cs;
    cairo_t *cr;

    surface = g_new0(EventdNdSurface, 1);

    surface->xcb_connection = context->xcb_connection;

    w = context->screen->width_in_pixels;
    h = context->screen->height_in_pixels;

    switch ( anchor )
    {
    case EVENTD_ND_STYLE_ANCHOR_TOP_LEFT:
        x = margin;
        y = margin;
    break;
    case EVENTD_ND_STYLE_ANCHOR_TOP_RIGHT:
        x = w - width - margin;
        y = margin;
    break;
    case EVENTD_ND_STYLE_ANCHOR_BOTTOM_LEFT:
        x = margin;
        y = h - height - margin;
    break;
    case EVENTD_ND_STYLE_ANCHOR_BOTTOM_RIGHT:
        x = w - width - margin;
        y = h - height - margin;
    break;
    }

    surface->window = xcb_generate_id(surface->xcb_connection);
    xcb_create_window(surface->xcb_connection,
                      context->screen->root_depth,   /* depth         */
                      surface->window,
                      context->screen->root,         /* parent window */
                      x, y,                          /* x, y          */
                      width, height,                 /* width, height */
                      0,                             /* border_width  */
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class         */
                      context->screen->root_visual,  /* visual        */
                      selmask, selval);              /* masks         */
    xcb_map_window(surface->xcb_connection, surface->window);

    cs = cairo_xcb_surface_create(context->xcb_connection, surface->window, get_root_visual_type(context->screen), width, height);
    cr = cairo_create(cs);
    cairo_set_source_surface(cr, bubble, 0, 0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    return surface;
}

void
eventd_nd_surface_show(EventdNdSurface *surface)
{
    xcb_map_window(surface->xcb_connection, surface->window);
    xcb_flush(surface->xcb_connection);
}

void
eventd_nd_surface_hide(EventdNdSurface *surface)
{
    xcb_unmap_window(surface->xcb_connection, surface->window);
    xcb_flush(surface->xcb_connection);
}

void
eventd_nd_surface_free(gpointer data)
{
    EventdNdSurface *surface = data;

    if ( surface == NULL )
        return;

    xcb_destroy_window(surface->xcb_connection, surface->window);
    xcb_flush(surface->xcb_connection);

    g_free(surface);
}

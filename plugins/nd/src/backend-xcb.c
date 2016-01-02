/*
 * eventd - Small daemon to act on remote or local events
 *
 * Copyright © 2011-2015 Quentin "Sardem FF7" Glidic
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

#include <config.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#include <glib.h>
#include <glib-object.h>

#include <cairo.h>

#include <cairo-xcb.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <libgwater-xcb.h>
#include <xcb/randr.h>
#include <xcb/shape.h>

#include <libeventd-event.h>
#include <libeventd-helpers-config.h>

#include "backend.h"

typedef enum {
    EVENTD_ND_XCB_ANCHOR_TOP,
    EVENTD_ND_XCB_ANCHOR_TOP_LEFT,
    EVENTD_ND_XCB_ANCHOR_TOP_RIGHT,
    EVENTD_ND_XCB_ANCHOR_BOTTOM,
    EVENTD_ND_XCB_ANCHOR_BOTTOM_LEFT,
    EVENTD_ND_XCB_ANCHOR_BOTTOM_RIGHT,
} EventdNdXcbCornerAnchor;

static const gchar * const _eventd_nd_xcb_corner_anchors[] = {
    [EVENTD_ND_XCB_ANCHOR_TOP]          = "top",
    [EVENTD_ND_XCB_ANCHOR_TOP_LEFT]     = "top left",
    [EVENTD_ND_XCB_ANCHOR_TOP_RIGHT]    = "top right",
    [EVENTD_ND_XCB_ANCHOR_BOTTOM]       = "bottom",
    [EVENTD_ND_XCB_ANCHOR_BOTTOM_LEFT]  = "bottom left",
    [EVENTD_ND_XCB_ANCHOR_BOTTOM_RIGHT] = "bottom right",
};
struct _EventdNdBackendContext {
    EventdNdInterface *nd;
    gchar **outputs;
    struct {
        EventdNdXcbCornerAnchor anchor;
        gboolean reverse;
        gint margin;
        gint spacing;
    } placement;
    GWaterXcbSource *source;
    xcb_connection_t *xcb_connection;
    xcb_screen_t *screen;
    struct {
        gint x;
        gint y;
        gint width;
        gint height;
    } base_geometry;
    gint randr_event_base;
    gboolean shape;
    GHashTable *bubbles;
    GQueue *queue;
};

struct _EventdNdSurface {
    EventdEvent *event;
    EventdNdBackendContext *context;
    GList *link;
    xcb_window_t window;
    gint width;
    gint height;
    cairo_surface_t *bubble;
};

static EventdNdBackendContext *
_eventd_nd_xcb_init(EventdNdInterface *nd)
{
    EventdNdBackendContext *self;

    self = g_new0(EventdNdBackendContext, 1);

    self->nd = nd;

    /* default bubble position */
    self->placement.anchor    = EVENTD_ND_XCB_ANCHOR_TOP_RIGHT;
    self->placement.margin    = 13;
    self->placement.spacing   = 13;

    return self;
}

static void
_eventd_nd_xcb_uninit(EventdNdBackendContext *self)
{
    g_free(self);
}


static void
_eventd_nd_xcb_global_parse(EventdNdBackendContext *self, GKeyFile *config_file)
{
    if ( ! g_key_file_has_group(config_file, "NotificationXcb") )
        return;

    Int integer;
    guint64 enum_value;
    gboolean boolean;

    evhelpers_config_key_file_get_string_list(config_file, "NotificationXcb", "Outputs", &self->outputs, NULL);

    if ( evhelpers_config_key_file_get_enum(config_file, "NotificationXcb", "Anchor", _eventd_nd_xcb_corner_anchors, G_N_ELEMENTS(_eventd_nd_xcb_corner_anchors), &enum_value) == 0 )
        self->placement.anchor = enum_value;

    if ( evhelpers_config_key_file_get_boolean(config_file, "NotificationXcb", "OldestFirst", &boolean) == 0 )
        self->placement.reverse = boolean;

    if ( evhelpers_config_key_file_get_int(config_file, "NotificationXcb", "Margin", &integer) == 0 )
        self->placement.margin = integer.value;

    if ( evhelpers_config_key_file_get_int(config_file, "NotificationXcb", "Spacing", &integer) == 0 )
        self->placement.spacing = integer.value;
}

static xcb_visualtype_t *
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

static void
_eventd_nd_xcb_geometry_fallback(EventdNdBackendContext *self)
{
    self->base_geometry.width = self->screen->width_in_pixels;
    self->base_geometry.height = self->screen->height_in_pixels;
}

static gboolean
_eventd_nd_xcb_randr_check_primary(EventdNdBackendContext *self)
{
    xcb_randr_get_output_primary_cookie_t pcookie;
    xcb_randr_get_output_primary_reply_t *primary;

    pcookie = xcb_randr_get_output_primary(self->xcb_connection, self->screen->root);
    if ( ( primary = xcb_randr_get_output_primary_reply(self->xcb_connection, pcookie, NULL) ) == NULL )
        return FALSE;

    gboolean found = FALSE;

    xcb_randr_get_output_info_cookie_t ocookie;
    xcb_randr_get_output_info_reply_t *output;

    ocookie = xcb_randr_get_output_info(self->xcb_connection, primary->output, 0);
    if ( ( output = xcb_randr_get_output_info_reply(self->xcb_connection, ocookie, NULL) ) != NULL )
    {

        xcb_randr_get_crtc_info_cookie_t ccookie;
        xcb_randr_get_crtc_info_reply_t *crtc;

        ccookie = xcb_randr_get_crtc_info(self->xcb_connection, output->crtc, output->timestamp);
        if ( ( crtc = xcb_randr_get_crtc_info_reply(self->xcb_connection, ccookie, NULL) ) != NULL )
        {
            found = TRUE;

            self->base_geometry.x = crtc->x;
            self->base_geometry.y = crtc->y;
            self->base_geometry.width = crtc->width;
            self->base_geometry.height = crtc->height;

            free(crtc);
        }
        free(output);
    }
    free(primary);

    return found;
}

typedef struct {
    xcb_randr_get_output_info_reply_t *output;
    xcb_randr_get_crtc_info_reply_t *crtc;
} EventdNdXcbRandrOutput;

static gboolean
_eventd_nd_xcb_randr_check_outputs(EventdNdBackendContext *self)
{
    xcb_randr_get_screen_resources_current_cookie_t rcookie;
    xcb_randr_get_screen_resources_current_reply_t *ressources;

    rcookie = xcb_randr_get_screen_resources_current(self->xcb_connection, self->screen->root);
    if ( ( ressources = xcb_randr_get_screen_resources_current_reply(self->xcb_connection, rcookie, NULL) ) == NULL )
    {
        g_warning("Couldn't get RandR screen ressources");
        return FALSE;
    }

    xcb_timestamp_t cts;
    xcb_randr_output_t *randr_outputs;
    gint i, length;

    cts = ressources->config_timestamp;

    length = xcb_randr_get_screen_resources_current_outputs_length(ressources);
    randr_outputs = xcb_randr_get_screen_resources_current_outputs(ressources);

    EventdNdXcbRandrOutput *outputs;
    EventdNdXcbRandrOutput *output;

    outputs = g_new(EventdNdXcbRandrOutput, length + 1);
    output = outputs;

    for ( i = 0 ; i < length ; ++i )
    {
        xcb_randr_get_output_info_cookie_t ocookie;

        ocookie = xcb_randr_get_output_info(self->xcb_connection, randr_outputs[i], cts);
        if ( ( output->output = xcb_randr_get_output_info_reply(self->xcb_connection, ocookie, NULL) ) == NULL )
            continue;

        xcb_randr_get_crtc_info_cookie_t ccookie;

        ccookie = xcb_randr_get_crtc_info(self->xcb_connection, output->output->crtc, cts);
        if ( ( output->crtc = xcb_randr_get_crtc_info_reply(self->xcb_connection, ccookie, NULL) ) == NULL )
            free(output->output);
        else
            ++output;
    }
    output->output = NULL;

    gchar **config_output;
    gboolean found = FALSE;
    for ( config_output = self->outputs ; ( *config_output != NULL ) && ( ! found ) ; ++config_output )
    {
        for ( output = outputs ; ( output->output != NULL ) && ( ! found ) ; ++output )
        {
            if ( g_ascii_strncasecmp(*config_output, (const gchar *)xcb_randr_get_output_info_name(output->output), xcb_randr_get_output_info_name_length(output->output)) != 0 )
                continue;
            self->base_geometry.x = output->crtc->x;
            self->base_geometry.y = output->crtc->y;
            self->base_geometry.width = output->crtc->width;
            self->base_geometry.height = output->crtc->height;

            found = TRUE;
        }
    }

    for ( output = outputs ; output->output != NULL ; ++output )
    {
        free(output->crtc);
        free(output->output);
    }
    g_free(outputs);

    return found;
}

static void _eventd_nd_xcb_update_surfaces(EventdNdBackendContext *self);

static void
_eventd_nd_xcb_randr_check_geometry(EventdNdBackendContext *self)
{
    gboolean found = FALSE;
    if ( self->outputs != NULL )
        found = _eventd_nd_xcb_randr_check_outputs(self);
    if ( ! found )
        found = _eventd_nd_xcb_randr_check_primary(self);
    if ( ! found )
        _eventd_nd_xcb_geometry_fallback(self);

    _eventd_nd_xcb_update_surfaces(self);
}

static void _eventd_nd_xcb_surface_expose_event(EventdNdSurface *self, xcb_expose_event_t *event);
static void _eventd_nd_xcb_surface_button_release_event(EventdNdSurface *self);


static gboolean
_eventd_nd_xcb_events_callback(xcb_generic_event_t *event, gpointer user_data)
{
    EventdNdBackendContext *self = user_data;
    EventdNdSurface *surface;

    if ( event == NULL )
    {
        self->nd->backend_stop(self->nd->context);
        return FALSE;
    }

    gint type = event->response_type & ~0x80;

    switch ( type - self->randr_event_base )
    {
    case XCB_RANDR_SCREEN_CHANGE_NOTIFY:
        _eventd_nd_xcb_randr_check_geometry(self);
    break;
    case XCB_RANDR_NOTIFY:
    break;
    default:
    switch ( type )
    {
    case XCB_EXPOSE:
    {
        xcb_expose_event_t *e = (xcb_expose_event_t *)event;

        surface = g_hash_table_lookup(self->bubbles, GUINT_TO_POINTER(e->window));
        if ( surface != NULL )
            _eventd_nd_xcb_surface_expose_event(surface, e);
    }
    break;
    case XCB_BUTTON_RELEASE:
    {
        xcb_button_release_event_t *e = (xcb_button_release_event_t *)event;

        surface = g_hash_table_lookup(self->bubbles, GUINT_TO_POINTER(e->event));
        if ( surface != NULL )
            _eventd_nd_xcb_surface_button_release_event(surface);
    }
    break;
    default:
    break;
    }
    }

    return TRUE;
}

static gboolean
_eventd_nd_xcb_start(EventdNdBackendContext *self, const gchar *target)
{
    gint r;
    gchar *h;
    gint d;

    r = xcb_parse_display(target, &h, &d, NULL);
    if ( r == 0 )
        return FALSE;
    free(h);

    const xcb_query_extension_reply_t *extension_query;
    gint screen;

    self->source = g_water_xcb_source_new(NULL, target, &screen, _eventd_nd_xcb_events_callback, self, NULL);
    if ( self->source == NULL )
    {
        g_warning("Couldn't initialize X connection for '%s'", target);
        return FALSE;
    }

    self->xcb_connection = g_water_xcb_source_get_connection(self->source);

    self->screen = xcb_aux_get_screen(self->xcb_connection, screen);

    self->bubbles = g_hash_table_new(NULL, NULL);
    self->queue = g_queue_new();

    extension_query = xcb_get_extension_data(self->xcb_connection, &xcb_shape_id);
    if ( ! extension_query->present )
        g_warning("No Shape extension");
    else
        self->shape = TRUE;

    extension_query = xcb_get_extension_data(self->xcb_connection, &xcb_randr_id);
    if ( ! extension_query->present )
    {
        self->randr_event_base = G_MAXINT;
        g_warning("No RandR extension");
        _eventd_nd_xcb_geometry_fallback(self);
    }
    else
    {
        self->randr_event_base = extension_query->first_event;
        xcb_randr_select_input(self->xcb_connection, self->screen->root,
                XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
                XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
        xcb_flush(self->xcb_connection);
        _eventd_nd_xcb_randr_check_geometry(self);
    }

    return TRUE;
}

static void
_eventd_nd_xcb_stop(EventdNdBackendContext *self)
{
    g_hash_table_unref(self->bubbles);
    g_water_xcb_source_unref(self->source);
    self->bubbles = NULL;
    self->source = NULL;
}

static void
_eventd_nd_xcb_surface_expose_event(EventdNdSurface *self, xcb_expose_event_t *event)
{
    cairo_surface_t *cs;
    cairo_t *cr;

    cs = cairo_xcb_surface_create(self->context->xcb_connection, self->window, get_root_visual_type(self->context->screen), self->width, self->height);
    cr = cairo_create(cs);
    cairo_set_source_surface(cr, self->bubble, 0, 0);
    cairo_rectangle(cr, event->x, event->y, event->width, event->height);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    xcb_flush(self->context->xcb_connection);
}

static void
_eventd_nd_xcb_surface_button_release_event(EventdNdSurface *self)
{
    self->context->nd->surface_remove(self->context->nd->context, eventd_event_get_uuid(self->event));
}

static void
_eventd_nd_xcb_surface_shape(EventdNdSurface *self, cairo_surface_t *bubble)
{
    EventdNdBackendContext *context = self->context;

    if ( ! context->shape )
        return;

    gint width;
    gint height;

    width = cairo_image_surface_get_width(bubble);
    height = cairo_image_surface_get_height(bubble);

    xcb_pixmap_t shape_id;
    cairo_surface_t *shape;
    cairo_t *cr;

    shape_id = xcb_generate_id(context->xcb_connection);
    xcb_create_pixmap(context->xcb_connection, 1,
                      shape_id, context->screen->root,
                      width, height);

    shape = cairo_xcb_surface_create_for_bitmap(context->xcb_connection, context->screen, shape_id, width, height);
    cr = cairo_create(shape);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, bubble, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(shape);

    xcb_shape_mask(context->xcb_connection,
                   XCB_SHAPE_SO_INTERSECT, XCB_SHAPE_SK_BOUNDING,
                   self->window, 0, 0, shape_id);

    xcb_free_pixmap(context->xcb_connection, shape_id);
}

static void
_eventd_nd_xcb_update_surfaces(EventdNdBackendContext *self)
{
    GList *surface_;
    EventdNdSurface *surface;

    gboolean right, center, bottom;
    right = ( self->placement.anchor == EVENTD_ND_XCB_ANCHOR_TOP_RIGHT ) || ( self->placement.anchor == EVENTD_ND_XCB_ANCHOR_BOTTOM_RIGHT );
    center = ( self->placement.anchor == EVENTD_ND_XCB_ANCHOR_TOP ) || ( self->placement.anchor == EVENTD_ND_XCB_ANCHOR_BOTTOM );
    bottom = ( self->placement.anchor == EVENTD_ND_XCB_ANCHOR_BOTTOM_LEFT ) || ( self->placement.anchor == EVENTD_ND_XCB_ANCHOR_BOTTOM ) || ( self->placement.anchor == EVENTD_ND_XCB_ANCHOR_BOTTOM_RIGHT );

    guint16 mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    guint32 vals[] = { 0, 0 };
    gint x, y;
    x = self->placement.margin;
    y = self->placement.margin;
    if ( center )
        x = self->base_geometry.width;
    else if ( right )
        x = self->base_geometry.width - x;
    if ( bottom )
        y = self->base_geometry.height - y;
    for ( surface_ = g_queue_peek_head_link(self->queue) ; surface_ != NULL ; surface_ = g_list_next(surface_) )
    {
        gint width;
        gint height;
        surface = surface_->data;

        width = cairo_image_surface_get_width(surface->bubble);
        height = cairo_image_surface_get_height(surface->bubble);
        if ( bottom )
            y -= height;

        vals[0] = self->base_geometry.x + ( center ? ( ( x / 2 ) - ( width / 2 ) ) : right ? ( x - width ) : x );
        vals[1] = self->base_geometry.y + y;
        xcb_configure_window(self->xcb_connection, surface->window, mask, vals);

        if ( bottom )
            y -= self->placement.spacing;
        else
            y += height + self->placement.spacing;
    }

    xcb_flush(self->xcb_connection);
}

static EventdNdSurface *
_eventd_nd_xcb_surface_new(EventdNdBackendContext *context, EventdEvent *event, cairo_surface_t *bubble)
{
    guint32 selmask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    guint32 selval[] = { 1, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE };
    EventdNdSurface *self;

    gint width;
    gint height;

    width = cairo_image_surface_get_width(bubble);
    height = cairo_image_surface_get_height(bubble);

    self = g_new0(EventdNdSurface, 1);

    self->event = eventd_event_ref(event);

    self->context = context;
    self->width = width;
    self->height = height;
    self->bubble = cairo_surface_reference(bubble);

    self->window = xcb_generate_id(context->xcb_connection);
    xcb_create_window(context->xcb_connection,
                                       context->screen->root_depth,   /* depth         */
                                       self->window,
                                       context->screen->root,         /* parent window */
                                       0, 0,                          /* x, y          */
                                       width, height,                 /* width, height */
                                       0,                             /* border_width  */
                                       XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class         */
                                       context->screen->root_visual,  /* visual        */
                                       selmask, selval);              /* masks         */

    _eventd_nd_xcb_surface_shape(self, bubble);

    xcb_map_window(context->xcb_connection, self->window);

    g_hash_table_insert(context->bubbles, GUINT_TO_POINTER(self->window), self);
    if ( context->placement.reverse )
    {
        g_queue_push_tail(context->queue, self);
        self->link = g_queue_peek_tail_link(context->queue);
    }
    else
    {
        g_queue_push_head(context->queue, self);
        self->link = g_queue_peek_head_link(context->queue);
    }

    _eventd_nd_xcb_update_surfaces(context);

    return self;
}

static void
_eventd_nd_xcb_surface_free(EventdNdSurface *self)
{
    if ( self == NULL )
        return;

    EventdNdBackendContext *context = self->context;

    g_queue_delete_link(context->queue, self->link);
    g_hash_table_remove(context->bubbles, GUINT_TO_POINTER(self->window));

    if ( ! g_source_is_destroyed((GSource *)context->source) )
    {
        xcb_unmap_window(context->xcb_connection, self->window);
        _eventd_nd_xcb_update_surfaces(context);
    }

    cairo_surface_destroy(self->bubble);

    eventd_event_unref(self->event);

    g_free(self);

}

static void
_eventd_nd_xcb_surface_update(EventdNdSurface *self, cairo_surface_t *bubble)
{
    cairo_surface_destroy(self->bubble);
    self->bubble = cairo_surface_reference(bubble);

    gint width;
    gint height;

    width = cairo_image_surface_get_width(bubble);
    height = cairo_image_surface_get_height(bubble);

    guint16 mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    guint32 vals[] = { width, height };

    xcb_configure_window(self->context->xcb_connection, self->window, mask, vals);
    _eventd_nd_xcb_surface_shape(self, bubble);

    xcb_clear_area(self->context->xcb_connection, TRUE, self->window, 0, 0, width, height);

    _eventd_nd_xcb_update_surfaces(self->context);
}

EVENTD_EXPORT
void
eventd_nd_backend_get_info(EventdNdBackend *backend)
{
    backend->init = _eventd_nd_xcb_init;
    backend->uninit = _eventd_nd_xcb_uninit;

    backend->global_parse = _eventd_nd_xcb_global_parse;

    backend->start = _eventd_nd_xcb_start;
    backend->stop  = _eventd_nd_xcb_stop;

    backend->surface_new     = _eventd_nd_xcb_surface_new;
    backend->surface_free    = _eventd_nd_xcb_surface_free;
    backend->surface_update  = _eventd_nd_xcb_surface_update;
}
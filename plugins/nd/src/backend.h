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

#ifndef __EVENTD_ND_BACKEND_H__
#define __EVENTD_ND_BACKEND_H__

#include <libeventd-event.h>

typedef enum {
    EVENTD_ND_BACKEND_NONE = 0,
#ifdef ENABLE_ND_XCB
    EVENTD_ND_BACKEND_XCB,
#endif /* ENABLE_ND_XCB */
#ifdef ENABLE_ND_FBDEV
    EVENTD_ND_BACKEND_FBDEV,
#endif /* ENABLE_ND_FBDEV */
#ifdef ENABLE_ND_WIN
    EVENTD_ND_BACKEND_WIN,
#endif /* ENABLE_ND_WIN */
    _EVENTD_ND_BACKENDS_SIZE
} EventdNdBackends;

const gchar *eventd_nd_backends_names[_EVENTD_ND_BACKENDS_SIZE];

typedef struct _EventdPluginContext EventdNdContext;
typedef struct _EventdNdNotification EventdNdNotification;
typedef struct {
    EventdNdContext *context;

    void (*geometry_update)(EventdNdContext *context, gint x, gint y, gint w, gint h);
    gboolean (*backend_stop)(EventdNdContext *context);

    void (*notification_draw)(EventdNdNotification *notification, cairo_surface_t *bubble);
    void (*notification_dismiss)(EventdNdNotification *notification);
} EventdNdInterface;


typedef struct _EventdNdBackendContext EventdNdBackendContext;
typedef struct _EventdNdSurface EventdNdSurface;

typedef struct {
    EventdNdBackendContext *(*init)(EventdNdInterface *context);
    void (*uninit)(EventdNdBackendContext *context);

    void (*global_parse)(EventdNdBackendContext *context, GKeyFile *config_file);

    gboolean (*start)(EventdNdBackendContext *context, const gchar *target);
    void (*stop)(EventdNdBackendContext *context);

    EventdNdSurface *(*surface_new)(EventdNdBackendContext *context, EventdNdNotification *notification, gint width, gint height);
    void (*surface_update)(EventdNdSurface *surface, gint width, gint height);
    void (*surface_free)(EventdNdSurface *surface);

    gpointer module;
    EventdNdBackendContext *context;
} EventdNdBackend;

typedef void (*EventdNdBackendGetInfoFunc)(EventdNdBackend *backend);

#endif /* __EVENTD_ND_BACKEND_H__ */

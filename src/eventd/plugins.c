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
#include <gmodule.h>

#include <eventd-plugin.h>

#include "types.h"

#include "plugins.h"

static GList *plugins = NULL;

static void
_eventd_plugins_load_dir(const gchar *plugins_dir_name)
{
    GError *error;
    GDir *plugins_dir;
    const gchar *file;


#if DEBUG
    g_debug("Scannig plugins dir: %s", plugins_dir_name);
#endif /* DEBUG */

    plugins_dir = g_dir_open(plugins_dir_name, 0, &error);
    if ( ! plugins_dir )
    {
        g_warning("Can’t read the plugins directory: %s", error->message);
        g_clear_error(&error);
        return;
    }

    while ( ( file = g_dir_read_name(plugins_dir) ) != NULL )
    {
        gchar *full_filename;
        EventdPlugin *plugin;
        EventdPluginGetInfoFunc get_info;
        void *module;

        full_filename = g_build_filename(plugins_dir_name, file, NULL);

        if ( g_file_test(full_filename, G_FILE_TEST_IS_DIR) )
        {
            g_free(full_filename);
            continue;
        }

        module = g_module_open(full_filename, G_MODULE_BIND_LAZY|G_MODULE_BIND_LOCAL);
        if ( module == NULL )
        {
            g_warning("Couldn’t load module '%s': %s", file, g_module_error());
            g_free(full_filename);
            continue;
        }
        g_free(full_filename);

        if ( ! g_module_symbol(module, "eventd_plugin_get_info", (void **)&get_info) )
            continue;

#if DEBUG
        g_debug("Loading plugin '%s'", file);
#endif /* ! DEBUG */

        plugin = g_new0(EventdPlugin, 1);
        plugin->module = module;
        get_info(plugin);

        if ( plugin->init != NULL )
        {
            plugin->context = plugin->init();
            if ( plugin->context == NULL )
            {
                g_warning("Couldn’t load plugin '%s'", file);
                g_module_close(plugin->module);
                g_free(plugin);
                continue;
            }
        }

        plugins = g_list_prepend(plugins, plugin);
    }
}

void
eventd_plugins_load()
{
    const gchar *env_base_dir;
    gchar *plugins_dir;

    if ( ! g_module_supported() )
    {
        g_warning("Couldn’t load plugins: %s", g_module_error());
        return;
    }

    env_base_dir = g_getenv("EVENTD_PLUGINS_DIR");
    if ( env_base_dir != NULL )
    {
        if ( env_base_dir[0] == '~' )
            plugins_dir = g_build_filename(g_get_home_dir(), env_base_dir+2, NULL);
        else
            plugins_dir = g_build_filename(env_base_dir,  NULL);

        if ( g_file_test(plugins_dir, G_FILE_TEST_IS_DIR) )
            _eventd_plugins_load_dir(plugins_dir);
        g_free(plugins_dir);
    }

    plugins_dir = g_build_filename(g_get_user_data_dir(), PACKAGE_NAME, "plugins", NULL);
    if ( g_file_test(plugins_dir, G_FILE_TEST_IS_DIR) )
        _eventd_plugins_load_dir(plugins_dir);
    g_free(plugins_dir);

    plugins_dir = g_build_filename(DATADIR, PACKAGE_NAME, "plugins", NULL);
    if ( g_file_test(plugins_dir, G_FILE_TEST_IS_DIR) )
        _eventd_plugins_load_dir(plugins_dir);
    g_free(plugins_dir);

    plugins_dir = g_build_filename(LIBDIR, PACKAGE_NAME, "plugins", NULL);
    if ( g_file_test(plugins_dir, G_FILE_TEST_IS_DIR) )
        _eventd_plugins_load_dir(plugins_dir);
    g_free(plugins_dir);
}

static void
_eventd_plugins_plugin_free(gpointer data)
{
    EventdPlugin *plugin = data;

    if ( plugin->uninit != NULL )
        plugin->uninit(plugin->context);
    g_module_close(plugin->module);
    g_free(plugin);
}

void
eventd_plugins_unload()
{
    if ( plugins == NULL )
        return;
    g_list_free_full(plugins, _eventd_plugins_plugin_free);
    plugins = NULL;
}

static void
_eventd_plugins_foreach(GFunc func, gpointer user_data)
{
    if ( plugins == NULL )
        return;
    g_list_foreach(plugins, func, user_data);
}

static void
_eventd_plugins_start(gpointer data, gpointer user_data)
{
    EventdPlugin *plugin = data;

    if ( plugin->start != NULL )
        plugin->start(plugin->context);
}

void
eventd_plugins_start_all()
{
    _eventd_plugins_foreach(_eventd_plugins_start, NULL);
}

static void
_eventd_plugins_stop(gpointer data, gpointer user_data)
{
    EventdPlugin *plugin = data;

    if ( plugin->stop != NULL )
        plugin->stop(plugin->context);
}

void
eventd_plugins_stop_all()
{
    _eventd_plugins_foreach(_eventd_plugins_stop, NULL);
}

static void
_eventd_plugins_control_command(gpointer data, gpointer user_data)
{
    EventdPlugin *plugin = data;

    if ( plugin->control_command != NULL )
        plugin->control_command(plugin->context, user_data);
}

void
eventd_plugins_control_command_all(gchar *command)
{
    _eventd_plugins_foreach(_eventd_plugins_control_command, command);
}

static void
_eventd_plugins_config_reset(gpointer data, gpointer user_data)
{
    EventdPlugin *plugin = data;

    if ( plugin->config_reset != NULL )
        plugin->config_reset(plugin->context);
}

void
eventd_plugins_config_reset_all()
{
    _eventd_plugins_foreach(_eventd_plugins_config_reset, NULL);
}

typedef struct {
    const gchar *type;
    const gchar *event;
    GKeyFile *config_file;
} EventdEventParseData;

static void
_eventd_plugins_event_parse(gpointer data, gpointer user_data)
{
    EventdPlugin *plugin = data;
    EventdEventParseData *parse_data = user_data;
    if ( plugin->event_parse != NULL )
        plugin->event_parse(plugin->context, parse_data->type, parse_data->event, parse_data->config_file);
}

void
eventd_plugins_event_parse_all(const gchar *type, const gchar *event, GKeyFile *config_file)
{
    EventdEventParseData data = {
        .type = type,
        .event = event,
        .config_file = config_file,
    };
    _eventd_plugins_foreach(_eventd_plugins_event_parse, &data);
}

static void
_eventd_plugins_event_action(gpointer data, gpointer user_data)
{
    EventdPlugin *plugin = data;
    EventdEvent *event = user_data;

    if ( plugin->event_action == NULL )
        return;

    plugin->event_action(plugin->context, event);
}

void
eventd_plugins_event_action_all(EventdEvent *event)
{
    _eventd_plugins_foreach(_eventd_plugins_event_action, event);
}

static void
_eventd_plugins_event_pong(gpointer data, gpointer user_data)
{
    EventdPlugin *plugin = data;
    EventdEvent *event = user_data;

    if ( plugin->event_pong == NULL )
        return;

    plugin->event_pong(plugin->context, event);
}

void
eventd_plugins_event_pong_all(EventdEvent *event)
{
    _eventd_plugins_foreach(_eventd_plugins_event_pong, event);
}

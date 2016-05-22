/*
 * eventd - Small daemon to act on remote or local events
 *
 * Copyright © 2011-2016 Quentin "Sardem FF7" Glidic
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
#include <libeventd-test.h>

int
main(int argc, char *argv[])
{
    int r = 99;
    eventd_tests_env_setup(argv);
    gchar **args = g_new(char *, 2);
    args[0] = g_strdup("--event-listen");
    args[1] = g_strdup("tcp:localhost:19021");
    EventdTestsEnv *env = eventd_tests_env_new("test-plugin,evp", args, 2);
    if ( ! eventd_tests_env_start_eventd(env) )
        goto end;

    r = eventd_tests_run_libeventc("127.0.0.1:19021");

    if ( ! eventd_tests_env_stop_eventd(env) )
        r = 99;

end:
    eventd_tests_env_free(env);
    return r;
}

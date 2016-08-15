/*
Copyright (C) 2014 - 2015 Eaton

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <string.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <cxxtools/jsondeserializer.h>
#include <cxxtools/directory.h>
#include <bios_proto.h>
#include <malamute.h>

#include "composite_metrics_classes.h"

int
main (int argc, char** argv) {

    // Read configuration
    if(argc < 2) {
        printf("Syntax: %s config\n", argv[0]);
        exit(0);
    }

    char *tmp_arg = strdup(argv[1]);
    char *name;
    if(asprintf(&name, "composite-metrics-%s", basename(tmp_arg)) < 0) {
        zsys_error("Can't allocate name of agent\n");
        exit(1);
    }
    zactor_t *cm_server = zactor_new (bios_composite_metrics_server, (void*) name);
    free(name);
    free(tmp_arg);
    zstr_sendx (cm_server, "CONNECT", "ipc://@/malamute", NULL);
    zclock_sleep (500);  // to settle down the things
    if(strcmp(getenv("BIOS_LOG_LEVEL"), "LOG_DEBUG") == 0)
        zstr_sendx (cm_server, "VERBOSE", NULL);
    zstr_sendx (cm_server, "CONFIG", argv[1], NULL);

    //  Accept and print any message back from server
    //  copy from src/malamute.c under MPL license
    while (true) {
        char *message = zstr_recv (cm_server);
        if (message) {
            puts (message);
            free (message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }

    zactor_destroy (&cm_server);
    return 0;
}

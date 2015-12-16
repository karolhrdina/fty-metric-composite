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

#include "composite_metrics.h"

int main (int argc, char** argv) {

    // Read configuration
    if(argc < 2) {
        printf("Syntax: %s config\n", argv[0]);
    }

    zactor_t *cm_server = zactor_new (bios_composite_metrics_server, (void*) "composite-metrics");
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

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

/*
@header
    Provides actor server implementing METRIC protocol.
@discuss
@end
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

struct value {
  double value;
  time_t valid_till;
};

void bios_composite_metrics_server (zsock_t *pipe, void* args) {
    static const uint64_t TTL = 5*60;
    std::map<std::string, value> cache;
    std::string lua_code;
    bool verbose = false;
    int phase = 0;

    char *name = strdup ((char*) args);

    mlm_client_t *client = mlm_client_new ();

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);

    zsock_signal (pipe, 0);

    while (!zsys_interrupted) {

        void *which = zpoller_wait (poller, -1);
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (msg);

            if (streq (cmd, "$TERM")) {
                zstr_free (&cmd);
                zmsg_destroy (&msg);
                goto exit;
            }
            else
            if (streq (cmd, "VERBOSE")) {
                verbose = true;
                zsys_error ("VERBOSE VERBOSE VERBOSE");
                zmsg_destroy (&msg);
            }
            else
            if (streq (cmd, "CONNECT")) {
                char* endpoint = zmsg_popstr (msg);
                mlm_client_connect(client, endpoint, 1000, name);
                int rv = mlm_client_set_producer(client, "METRICS");
                if (rv == -1) {
                    zsys_error ("mlm_client_set_producer () failed.");
                }
                zstr_free(&endpoint);
                phase = 1;
            }
            else
            if (streq (cmd, "CONFIG")) {
                if(phase < 1) {
                    zsys_error("CONFIG before CONNECT");
                    continue;
                }
                char* filename = zmsg_popstr (msg);
                std::ifstream f(filename);
                if (verbose)
                    zsys_debug ("%s: Opening '%s'", name, filename);
                cxxtools::JsonDeserializer json(f);
                json.deserialize();

                const cxxtools::SerializationInfo *si = json.si();
                si->getMember("evaluation") >>= lua_code;

                // Subscribe to all streams
                for(auto it : si->getMember("in")) {
                    std::string buff;
                    it >>= buff;
                    mlm_client_set_consumer(client, BIOS_PROTO_STREAM_METRICS_SENSOR, buff.c_str());
                    if (verbose)
                        zsys_debug("%s: Registered to receive '%s' from stream '%s'", name, buff.c_str(), BIOS_PROTO_STREAM_METRICS_SENSOR);
                }
                zstr_free (&filename);
                phase = 2;
            }
            zstr_free (&cmd);
            zmsg_destroy (&msg);
            continue;
        }

        if(phase < 2) {
            zsys_error("DATA before CONFIG");
            continue;
        }

        // Get message
        zmsg_t *msg = mlm_client_recv(client);
        if(verbose)
            zsys_debug("Got something not from the pipe");
        if(msg == NULL)
            continue;
        if(verbose)
            zsys_debug("It is not null");
        bios_proto_t *yn = bios_proto_decode(&msg);
        if(yn == NULL)
            continue;
        if(verbose)
            zsys_debug("And it is bios_proto_message");

        // Update cache with updated values
        std::string topic = mlm_client_subject(client);
        value val;
        val.value = atof(bios_proto_value(yn));
        uint32_t ttl = bios_proto_ttl(yn);
        uint64_t timestamp = bios_proto_aux_number (yn, "time", ::time(NULL));
        val.valid_till = timestamp + ttl;
        if (verbose)
            zsys_debug ("%s: Got message '%s' with value %lf", name, topic.c_str(), val.value);
        auto f = cache.find(topic);
        if(f != cache.end()) {
            f->second = val;
        } else {
            cache.insert(std::make_pair(topic, val));
        }
        bios_proto_destroy(&yn);

        // Prepare data for computation
#if LUA_VERSION_NUM > 501
        lua_State *L = luaL_newstate();
#else
        lua_State *L = lua_open();
#endif
        luaL_openlibs(L);
        lua_newtable(L);
        time_t tme = time(NULL);
        for(auto i : cache) {
            if(tme > i.second.valid_till)
                continue;
            if (verbose)
                zsys_debug ("%s - %s, %f", name, i.first.c_str(), i.second.value);
            lua_pushstring(L, i.first.c_str());
            lua_pushnumber(L, i.second.value);
            lua_settable(L, -3);
        }
        lua_setglobal(L, "mt");

        // Do the real processing
        auto error = luaL_loadbuffer(L, lua_code.c_str(), lua_code.length(), "line") ||
            lua_pcall(L, 0, 3, 0);
        if(error) {
            zsys_error("%s", lua_tostring(L, -1));
            goto next_iter;
        }
        if (verbose)
            zsys_debug ("%s: Total: %d", name, lua_gettop(L));
        if(lua_gettop(L) == 3) {
            if(strrchr(lua_tostring(L, -3), '@') == NULL) {
                zsys_error ("Invalid output topic");
                goto next_iter;
            }
            bios_proto_t *n_met = bios_proto_new(BIOS_PROTO_METRIC);
            zsys_debug ("Creating new bios proto message");
            char *buff = strdup(lua_tostring(L, -3));
            zsys_debug ("buff == '%s'", buff);
            bios_proto_set_element_src(n_met, "%s", strrchr(buff, '@') + 1);
            zsys_debug ("strrchr (buff, '@') + 1 == '%s'", strrchr(buff, '@') + 1);
            (*strrchr(buff, '@')) = 0;
            zsys_debug ("buff == '%s'", buff);
            bios_proto_set_type(n_met, "%s", buff);
            bios_proto_set_value(n_met, "%.2f", lua_tonumber(L, -2));
            zsys_debug ("lua_tonumber (L, -2) == '%.2f'", lua_tonumber(L, -2));
            bios_proto_set_unit(n_met,  "%s", lua_tostring(L, -1));
            zsys_debug ("lua_tostring (L, -1) == '%s'", lua_tostring (L, -1));
            bios_proto_set_ttl(n_met,  TTL);
            zmsg_t* z_met = bios_proto_encode(&n_met);
            zsys_debug ("lua_tostring (L, -3) == '%s'", lua_tostring(L, -3));
            int rv = mlm_client_send(client, lua_tostring(L, -3), &z_met);
            if (rv != 0) {
                zsys_error ("mlm_client_send () failed.");
            }
            free (buff);
        } else {
            zsys_error ("Not enough valid data...\n");
        }
next_iter:
        lua_close(L);
    }

exit:
    free (name);
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
}

//  ---------------------------------------------------------------------------
//  Selftest

void
bios_composite_metrics_server_test (bool verbose)
{
    static const char* endpoint = "inproc://bios-cm-server-test";

    printf (" * bios_composite_metrics_server: ");
    if (verbose)
        printf ("\n");

    //  @selftest
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND", endpoint, NULL);
    if (verbose)
        zstr_send (server, "VERBOSE");

    mlm_client_t *producer = mlm_client_new ();
    mlm_client_connect (producer, endpoint, 1000, "producer");
    mlm_client_set_producer (producer, BIOS_PROTO_STREAM_METRICS_SENSOR);

    mlm_client_t *consumer = mlm_client_new ();
    mlm_client_connect (consumer, endpoint, 1000, "consumer");
    mlm_client_set_consumer (consumer, BIOS_PROTO_STREAM_METRICS, "temperature@world");

    char *name = NULL;
    if(asprintf(&name, "composite-metrics-%s", "sd") < 0) {
        printf("Can't allocate name of agent\n");
        exit(1);
    }

    zactor_t *cm_server = zactor_new (bios_composite_metrics_server, (void*) name);
    free(name);
    if (verbose)
        zstr_send (cm_server, "VERBOSE");
    zstr_sendx (cm_server, "CONNECT", endpoint, NULL);
    zstr_sendx (cm_server, "CONFIG", "src/composite-metrics.cfg.example", NULL);
    zclock_sleep (500);   //THIS IS A HACK TO SETTLE DOWN THINGS

    // send one value
    zmsg_t *msg_in;
    msg_in = bios_proto_encode_metric(
            NULL, "temperature", "TH1", "40", "C", ::time (NULL));
    assert (msg_in);
    mlm_client_send (producer, "temperature@TH1", &msg_in);

    zmsg_t *msg_out;
    bios_proto_t *m;
    msg_out = mlm_client_recv (consumer);
    m = bios_proto_decode (&msg_out);
    bios_proto_print (m);
    assert ( streq (mlm_client_sender (consumer), "composite-metrics-sd") );
    assert (m);
    assert (streq (bios_proto_value (m), "40.00"));    // <<< 40 / 1
    bios_proto_destroy (&m);

    // send another value
    msg_in = bios_proto_encode_metric(
            NULL, "temperature", "TH2", "100", "C", ::time (NULL));
    assert (msg_in);
    mlm_client_send (producer, "temperature@TH2", &msg_in);

    msg_out = mlm_client_recv (consumer);
    m = bios_proto_decode (&msg_out);
    assert (m);
    assert (streq (bios_proto_value (m), "70.00"));    // <<< (100 + 40) / 2
    bios_proto_destroy (&m);

    // send value for TH1 again
    msg_in = bios_proto_encode_metric(
            NULL, "temperature", "TH1", "70.00", "C", ::time (NULL));
    assert (msg_in);
    mlm_client_send (producer, "temperature@TH1", &msg_in);

    msg_out = mlm_client_recv (consumer);
    m = bios_proto_decode (&msg_out);
    assert (m);
    assert (streq (bios_proto_value (m), "85.00"));     // <<< (100 + 70) / 2
    bios_proto_destroy (&m);

    zactor_destroy (&cm_server);
    mlm_client_destroy (&consumer);
    mlm_client_destroy (&producer);
    zactor_destroy (&server);

    // @end
}

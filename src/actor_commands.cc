/*  =========================================================================
    actor_commands - actor commands

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
    =========================================================================
*/

/*
@header
    actor_commands - actor commands
@discuss
@end
*/

#include "composite_metrics_classes.h"

int
actor_commands (
        mlm_client_t *client,
        zmsg_t **message_p,
        data_t *data)
{
    assert (message_p && *message_p);
    assert (data);

    zmsg_t *message = *message_p;
    
    char *cmd = zmsg_popstr (message);
    if (!cmd) {
        log_error (
                "Given `which == pipe` function `zmsg_popstr (msg)` returned NULL. "
                "Message received is most probably empty (has no frames).");
        zmsg_destroy (message_p);
        return 0;
    }

    int ret = 0;
    log_debug ("actor command = '%s'", cmd);
    if (streq (cmd, "$TERM")) {
        ret = 1;
    }
    else
    if (streq (cmd, "CONNECT")) {
        char *endpoint = zmsg_popstr (message);
        if (!endpoint) {
            log_error (
                    "Expected multipart string format: CONNECT/endpoint/name. "
                    "Received CONNECT/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        char *name = zmsg_popstr (message);
        if (!name) {
            log_error (
                    "Expected multipart string format: CONNECT/endpoint/name. "
                    "Received CONNECT/endpoint/nullptr");
            zstr_free (&endpoint);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_connect (client, endpoint, 1000, name);
        if (rv == -1) {
            log_error (
                    "mlm_client_connect (endpoint = '%s', timeout = '%d', address = '%s') failed",
                    endpoint, 1000, name);
        }

        zstr_free (&endpoint);
        zstr_free (&name);
    }
    else
    if (streq (cmd, "PRODUCER")) {
        char *stream = zmsg_popstr (message);
        if (!stream) {
            log_error (
                    "Expected multipart string format: PRODUCER/stream. "
                    "Received PRODUCER/nullptr");
            zstr_free (&stream);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_set_producer (client, stream);
        if (rv == -1) {
            log_error ("mlm_client_set_producer (stream = '%s') failed", stream);
        }
        zstr_free (&stream);
    }
    else
    if (streq (cmd, "CONSUMER")) {
        char *stream = zmsg_popstr (message);
        if (!stream) {
            log_error (
                    "Expected multipart string format: CONSUMER/stream/pattern. "
                    "Received CONSUMER/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        char *pattern = zmsg_popstr (message);
        if (!pattern) {
            log_error (
                    "Expected multipart string format: CONSUMER/stream/pattern. "
                    "Received CONSUMER/stream/nullptr");
            zstr_free (&stream);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_set_consumer (client, stream, pattern);
        if (rv == -1) {
            log_error (
                    "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed",
                    stream, pattern);
        }
        zstr_free (&pattern);
        zstr_free (&stream);
    }
    else
    if (streq (cmd, "STATE_FILE")) {
        char *state_file = zmsg_popstr (message);
        if (!state_file) {
            log_error (
                    "Expected multipart string format: STATE_FILE/state_file."
                    "Received STATE_FILE/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        data_set_statefile (data, state_file);
        zstr_free (&state_file); 
    }
    else
    if (streq (cmd, "CFG_DIRECTORY")) {
        char *cfgdir = zmsg_popstr (message);
        if (!cfgdir) {
            log_error (
                    "Expected multipart string format: CFG_DIRECTORY/cfg_directory."
                    "Received CFG_DIRECTORY/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        data_set_cfgdir (data, cfgdir);
        zstr_free (&cfgdir); 
    }   
    else {
        log_warning ("Command '%s' is unknown or not implemented", cmd);
    }

    zstr_free (&cmd);
    zmsg_destroy (message_p);
    return ret;
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
actor_commands_test (bool verbose)
{
    printf (" * actor_commands: ");
    //  @selftest
    
    static const char* endpoint = "ipc://bios-actor-commands-test";
    // malamute broker
    zactor_t *malamute = zactor_new (mlm_server, (void*) "Malamute");
    assert (malamute);
    if (verbose)
        zstr_send (malamute, "VERBOSE");
    zstr_sendx (malamute, "BIND", endpoint, NULL);

    mlm_client_t *client = mlm_client_new ();
    assert (client);

    zmsg_t *message = NULL;
    data_t *data = data_new ();
   
    // empty message - expected fail
    message = zmsg_new ();
    assert (message);
    int rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // empty string - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "");   
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);  
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // unknown command - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "MAGIC!");   
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);  
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // STATE_FILE - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "STATE_FILE");
    // missing config_file here
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // STATE_FILE - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "STATE_FILE");
    zmsg_addstr (message, "."); // supplied path is a directory
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));
 
    // --------------------------------------------------------------
    // STATE_FILE - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "STATE_FILE");
    zmsg_addstr (message, "/dev/null/karolino"); // not writable
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));
 
    // --------------------------------------------------------------
    // STATE_FILE - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "STATE_FILE");
    zmsg_addstr (message, "/lib/state_file"); // not writable
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // STATE_FILE - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "STATE_FILE");
    zmsg_addstr (message, "/root"); // not writable
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // CFG_DIRECTORY - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CFG_DIRECTORY");
    // missing config_file here
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // CFG_DIRECTORY - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CFG_DIRECTORY");
    zmsg_addstr (message, "/etc/passwd"); // supplied path is not a directory
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // CFG_DIRECTORY - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CFG_DIRECTORY");
    zmsg_addstr (message, "/sdssdf/sfef//sdfe"); // non-existing path
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // --------------------------------------------------------------
    // CONNECT - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");   
    zmsg_addstr (message, endpoint);
    // missing name here
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));

    // --------------------------------------------------------------
    // CONNECT - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");   
    // missing endpoint here
    // missing name here
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);  
    assert (streq (data_statefile (data), ""));

    // --------------------------------------------------------------
    // CONNECT - expected fail; bad endpoint
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");   
    zmsg_addstr (message, "ipc://bios-smtp-server-BAD");
    zmsg_addstr (message, "test-agent");
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);  
    assert (streq (data_statefile (data), ""));

    // --------------------------------------------------------------
    // CONSUMER - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONSUMER");
    zmsg_addstr (message, "some-stream");   
    // missing pattern here
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));

    // --------------------------------------------------------------
    // CONSUMER - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONSUMER");
    // missing stream here
    // missing pattern here
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));

    // --------------------------------------------------------------
    // PRODUCER - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "PRODUCER");
    // missing stream here
    rv = actor_commands (client, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));

    // The original client still waiting on the bad endpoint for malamute
    // server to show up. Therefore we must destroy and create it again.
    mlm_client_destroy (&client);

    mlm_client_t *client2 = mlm_client_new ();
    assert (client2);

    // --------------------------------------------------------------
    // $TERM
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "$TERM");   
    rv = actor_commands (client2, &message, data);
    assert (rv == 1);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // CONNECT
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");   
    zmsg_addstr (message, endpoint);
    zmsg_addstr (message, "test-agent");   
    rv = actor_commands (client2, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // CONSUMER
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONSUMER");
    zmsg_addstr (message, "some-stream");   
    zmsg_addstr (message, ".+@.+");   
    rv = actor_commands (client2, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // PRODUCER
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "PRODUCER");
    zmsg_addstr (message, "some-stream");   
    rv = actor_commands (client2, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), ""));
    assert (streq (data_cfgdir (data), ""));

    // STATE_FILE
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "STATE_FILE");
    zmsg_addstr (message, "./test_state_file");
    rv = actor_commands (client2, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), "./test_state_file"));
    assert (streq (data_cfgdir (data), ""));

    // CFG_DIRECTORY
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CFG_DIRECTORY");
    zmsg_addstr (message, "./");
    rv = actor_commands (client2, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), "./test_state_file"));
    assert (streq (data_cfgdir (data), "./"));

    // CFG_DIRECTORY
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CFG_DIRECTORY");
    zmsg_addstr (message, "../");
    rv = actor_commands (client2, &message, data);
    assert (rv == 0);
    assert (message == NULL);
    assert (streq (data_statefile (data), "./test_state_file"));
    assert (streq (data_cfgdir (data), "../"));


    zmsg_destroy (&message);
    data_destroy (&data); 
    mlm_client_destroy (&client2);
    zactor_destroy (&malamute);
    //  @end
    printf ("OK\n");
}

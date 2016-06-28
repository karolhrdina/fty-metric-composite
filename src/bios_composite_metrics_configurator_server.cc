/*  =========================================================================
    bios_composite_metrics_configurator_server - Composite metrics server configurator

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
    bios_composite_metrics_configurator_server - Composite metrics server configurator
@discuss
@end
*/

#include <string>
#include <vector>

#include "composite_metrics_classes.h"

// Wrapper for bios_proto_ext_string
static int32_t
s_bios_proto_ext_int32 (bios_proto_t *self, const char *key, int32_t default_value)
{
    assert (self);
    assert (key);
    const char *str = bios_proto_ext_string (self, key, NULL);
    if (!str)
        return default_value;
    long u = 0;
    try {
        size_t pos = 0;
        u = std::stol (str, &pos);
        if (pos != strlen (str)) {
            zsys_error ("string '%s' does not represent an integer number", str);
            return default_value;
        }
    }
    catch (const std::exception& e) {
        zsys_error ("string '%s' does not represent an integer number", str);
        return default_value;
    }
    return static_cast<int32_t> (u);
}

// Copied from agent-nut
// -1 - error, subprocess code - success
static int
s_bits_systemctl (const char *operation, const char *service)
{
    return 0;
    assert (operation);
    assert (service);

    std::vector <std::string> _argv = {"sudo", "systemctl", operation, service};

    SubProcess systemd (_argv);
    if (systemd.run()) {
        int result = systemd.wait ();
        zsys_info ("sudo systemctl %s %s result: %i (%s)",
                  operation, service, result, result == 0 ? "ok" : "failed");
        return result;
    }
    zsys_error ("can't run sudo systemctl %s %s command", operation, service);
    return -1;
}

// For each config file in top level of 'path_to_dir' do
//  * systemctl stop and disable of service that uses this file
//  * remove config file
// 0 - success, 1 - failure
static int
s_remove_and_stop (const char *path_to_dir)
{
    assert (path_to_dir);

    zdir_t *dir = zdir_new (path_to_dir, "-");
    if (!dir) {
        zsys_error ("zdir_new (path = '%s', parent = '-') failed.", path_to_dir);
        return 1;
    }

    zlist_t *files = zdir_list (dir);
    if (!files) {
        zdir_destroy (&dir);
        zsys_error ("zdir_list () failed.");
        return 1;
    }

    zrex_t *file_rex = zrex_new ("(.+)\\.cfg$");
    if (!file_rex) {
        zdir_destroy (&dir);
        zlist_destroy (&files);
        zsys_error ("zrex_new ('%s') failed", "(.+)\\.cfg$");
        return 1;
    }

    zfile_t *item = (zfile_t *) zlist_first (files);
    while (item) {
        if (zrex_matches (file_rex, zfile_filename (item, NULL))) {
            std::string filename = zrex_hit (file_rex, 1);
            std::string service = "composite-metrics@";
            service += filename.substr (filename.rfind ("/") + 1);
            zsys_debug ("stopping/disabling service %s", service.c_str ());
            s_bits_systemctl ("stop", service.c_str ());
            s_bits_systemctl ("disable", service.c_str ());
            zsys_debug ("removing %s", zfile_filename (item, NULL));
            zfile_remove (item);
        }
        item = (zfile_t *) zlist_next (files);
    }
    zrex_destroy (&file_rex);
    zlist_destroy (&files);
    zdir_destroy (&dir);
    return 0;
}

// Write contents to file
// 0 - success, 1 - failure
static int
s_write_file (const char *fullpath, const char *contents)
{
    assert (fullpath);
    assert (contents);

    zfile_t *file = zfile_new (NULL, fullpath);
    if (!file) {
        zsys_error ("zfile_new (path = NULL, file = '%s') failed.", fullpath);
        return 1;
    }
    if (zfile_output (file) == -1) {
        zsys_error ("zfile_output () failed; filename = '%s'", zfile_filename (file, NULL));
        zfile_close (file);
        zfile_destroy (&file);
        return 1;
    }
    zchunk_t *chunk = zchunk_new ((const void *) contents, strlen (contents));
    if (!chunk) {
        zfile_close (file);
        zfile_destroy (&file);
        zsys_error ("zchunk_new () failed");
        return 1;
    }
    int rv = zfile_write (file, chunk, 0);
    zchunk_destroy (&chunk);
    zfile_close (file);
    zfile_destroy (&file);
    if (rv == -1) {
        zsys_error ("zfile_write () failed");
        return 1;
    }
    return 0;
}

// Generate todo
// 0 - success, 1 - failure
static void 
s_generate_and_start (const char *path_to_dir, const char *sensor_function, const char *asset_name, zlistx_t **sensors_p)
{
    assert (path_to_dir);
    assert (asset_name);
    assert (sensors_p);

    zlistx_t *sensors = *sensors_p;

    if (!sensors) {
        zsys_error ("parameter '*sensors_p' is NULL");
        return;
    }

    if (zlistx_size (sensors) == 0) {
        zsys_debug ("sensors empty");
        zlistx_destroy (sensors_p);
        *sensors_p = NULL;
        return;
    }

    std::string temp_in = "[ ", hum_in = "[ ";

    int32_t temp_offset_total = 0, temp_offset_count = 0;
    int32_t hum_offset_total = 0, hum_offset_count = 0;
    bool first = true;
 
    bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
    while (item) {
        if (first) {
            first = false;
        }
        else {
            temp_in += ", ";
            hum_in += ", ";
        }
        temp_in += "\"temperature.";
        temp_in += bios_proto_ext_string (item, "port", "(unknown)");
        temp_in += "@";
        temp_in += bios_proto_aux_string (item, "parent_name", "(unknown)");
        temp_in += "\"";

        hum_in += "\"humidity.";
        hum_in += bios_proto_ext_string (item, "port", "(unknown)");
        hum_in += "@";
        hum_in += bios_proto_aux_string (item, "parent_name", "(unknown)");
        hum_in += "\"";

        temp_offset_total += s_bios_proto_ext_int32 (item, "calibration_offset_t", 0);
        temp_offset_count++;
        hum_offset_total += s_bios_proto_ext_int32 (item, "calibration_offset_h", 0);
        hum_offset_count++;

        item = (bios_proto_t *) zlistx_next (sensors);
    }
    zlistx_destroy (sensors_p);
    *sensors_p = NULL;

    temp_in += " ]";
    hum_in += " ]";
 
    static const char *json_tmpl =
                           "{\n"
                           "\"in\" : ##IN##,\n"
                           "\"evaluation\": \"sum = 0;\n"
                           "                  num = 0;\n"
                           "                  for key,value in pairs(mt) do\n"
                           "                      sum = sum + value;\n"
                           "                      num = num + 1;\n"
                           "                  end;\n"
                           "                  tmp = sum / num + ##CLB##;\n"
                           "                  return 'average.##QNTY##@##LOGICAL_ASSET##', tmp, '##UNITS##', 0;\"\n"
                           "}\n";
    std::string contents = json_tmpl;

    double clb = 0;
    if (temp_offset_count != 0)
        clb = temp_offset_total / temp_offset_count;

    contents.replace (contents.find ("##IN##"), strlen ("##IN##"), temp_in);
    contents.replace (contents.find ("##CLB##"), strlen ("##CLB##"), std::to_string (clb));
    contents.replace (contents.find ("##LOGICAL_ASSET##"), strlen ("##LOGICAL_ASSET##"), asset_name);
    contents.replace (contents.find ("##QNTY##"), strlen ("##QNTY##"), "temperature");
    contents.replace (contents.find ("##UNITS##"), strlen ("##UNITS##"), "C");

    // name of the file (service) without extension
    std::string filename = asset_name;
    if (sensor_function) {
        filename += "-";
        filename += sensor_function;
    }
    filename += "-temperature";

    std::string fullpath = path_to_dir;
    fullpath += "/";
    fullpath += filename;
    fullpath += ".cfg";

    std::string service = "composite-metrics";
    service += "@";
    service += filename;

    if (s_write_file (fullpath.c_str (), contents.c_str ()) == 0) {
        s_bits_systemctl ("enable", service.c_str ());
        s_bits_systemctl ("start", service.c_str ());
    }
    else {
        zsys_error (
                "Creating config file '%s' failed. Service '%s' not started.",
                fullpath.c_str (), filename.c_str ());
    }

    clb = 0;
    if (hum_offset_count != 0)
        clb = hum_offset_total / hum_offset_count;

    contents = json_tmpl;
    contents.replace (contents.find ("##IN##"), strlen ("##IN##"), hum_in);
    contents.replace (contents.find ("##CLB##"), strlen ("##CLB##"), std::to_string (clb));
    contents.replace (contents.find ("##LOGICAL_ASSET##"), strlen ("##LOGICAL_ASSET##"), asset_name);
    contents.replace (contents.find ("##QNTY##"), strlen ("##QNTY##"), "humidity");
    contents.replace (contents.find ("##UNITS##"), strlen ("##UNITS##"), "%");

    // name of the file (service) without extension
    filename = asset_name;
    if (sensor_function) {
        filename += "-";
        filename += sensor_function;
    }
    filename += "-humidity";

    fullpath = path_to_dir;
    fullpath += "/";
    fullpath += filename;
    fullpath += ".cfg";

    service = "composite-metrics";
    service += "@";
    service += filename;

    if (s_write_file (fullpath.c_str (), contents.c_str ()) == 0) {
        s_bits_systemctl ("enable", service.c_str ());
        s_bits_systemctl ("start", service.c_str ());
    }
    else {
        zsys_error (
                "Creating config file '%s' failed. Service '%s' not started.",
                fullpath.c_str (), filename.c_str ());
    }   
    return;
} 

static void
s_handle_stream_deliver (mlm_client_t *client, zmsg_t **message_p, data_t *data)
{
    assert (client);
    assert (message_p && *message_p);
    assert (data);
    
    bios_proto_t *proto = bios_proto_decode (message_p);
    *message_p = NULL;
    if (!proto) {
        zsys_error (
                "bios_proto_decode () failed; sender = '%s', subject = '%s'",
                mlm_client_sender (client), mlm_client_subject (client));
        return;
    }
    data_asset_put (data, &proto);
    assert (proto == NULL);
    if (!data_asset_sensors_changed (data))
        return;

    // 1. Delete all files in output dir and stop/disable services
    int rv = s_remove_and_stop (data_cfgdir (data));
    if (rv != 0) {
        zsys_error (
                "Error removing old config files from directory '%s'. New config "
                "files were NOT generated and services were NOT started.", data_cfgdir (data));
        return;
    }

    // 2. Generate new files and enable/start services
    zlistx_t *assets = data_asset_names (data);
    if (!assets) {
        zsys_error ("data_asset_names () failed");
        return;
    }

    const char *asset = (const char *) zlistx_first (assets);
    while (asset) {
        bios_proto_t *proto = data_asset (data, asset);
        if (streq (bios_proto_aux_string (proto, "type", ""), "rack")) {
            zlistx_t *sensors = NULL;
            // Ti, Hi
            sensors = data_sensor (data, asset, "input");
            s_generate_and_start (data_cfgdir (data), "input", asset, &sensors);
            // To, Ho
            sensors = data_sensor (data, asset, "output");
            s_generate_and_start (data_cfgdir (data), "output", asset, &sensors);
        }
        else {
            zlistx_t *sensors = NULL;
            // T, H
            sensors = data_sensor (data, asset, NULL);
            s_generate_and_start (data_cfgdir (data), NULL, asset, &sensors);
        }
        asset = (const char *) zlistx_next (assets);
    }
    zlistx_destroy (&assets);
}


//  --------------------------------------------------------------------------
//  composite metrics configurator server 

void
bios_composite_metrics_configurator_server (zsock_t *pipe, void* args)
{
    assert (pipe);

    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        zsys_error ("mlm_client_new () failed");
        return;
    }

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    if (!poller) {
        zsys_error ("zpoller_new () failed");
        mlm_client_destroy (&client);
        return;
    }

    data_t *data = data_new ();
    zsock_signal (pipe, 0);

    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, -1);

        if (which == NULL) {
            zsys_warning (
                    "zpoller_terminated () == '%s' or zsys_interrupted == '%s'",
                    zpoller_terminated (poller) ? "true" : "false",
                    zsys_interrupted ? "true" : "false");
            break;
        }

        if (which == pipe) {
            zmsg_t *message = zmsg_recv (pipe);
            if (!message) {
                zsys_error ("Given `which == pipe`, function `zmsg_recv (pipe)` returned NULL");
                continue;
            }
            if (actor_commands (client, &message, data) == 1) {
                break;
            }
            continue;
        }

        // paranoid non-destructive assertion of a twisted mind 
        if (which != mlm_client_msgpipe (client)) {
            zsys_error ("which was checked for NULL, pipe and now should have been `mlm_client_msgpipe (client)` but is not.");
            continue;
        }

        zmsg_t *message = mlm_client_recv (client);
        if (!message) {
            zsys_error ("Given `which == mlm_client_msgpipe (client)`, function `mlm_client_recv ()` returned NULL");
            continue;
        }

        const char *command = mlm_client_command (client);
        if (streq (command, "STREAM DELIVER")) {
            zsys_debug ("STREAM DELIVER");
            s_handle_stream_deliver (client, &message, data);
        }
        else
        if (streq (command, "MAILBOX DELIVER") ||
            streq (command, "SERVICE DELIVER")) {
            zsys_warning (
                    "Received a message from sender = '%s', command = '%s', subject = '%s'. Throwing away.",
                    mlm_client_sender (client), mlm_client_command (client), mlm_client_subject (client));
            continue;
        }
        else {
            zsys_error ("Unrecognized mlm_client_command () = '%s'", command ? command : "(null)");
        }
        zmsg_destroy (&message);
    }

    data_save (data);
    data_destroy (&data);
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
}

//  --------------------------------------------------------------------------
//  Self test of this class

//  Helper test function
//  create new ASSET message of type bios_proto_t

/*
static bios_proto_t *
test_asset_new (const char *name, const char *operation)
{
    assert (name);
    assert (operation);

    bios_proto_t *asset = bios_proto_new (BIOS_PROTO_ASSET);
    bios_proto_set_name (asset, "%s", name);
    bios_proto_set_operation (asset, "%s", operation);
    return asset;
}
*/

void
bios_composite_metrics_configurator_server_test (bool verbose)
{
    /*
    static const char* endpoint = "inproc://bios-cm-configurator-server-test";
    printf (" * bios_composite_metrics_configurator_server: ");
    if (verbose)
        printf ("\n");

    //  @selftest
// TODO: code copied from agent-nut (systemctl/Subprocess) makes selftest 
//       fail under valgrind. Unless fixed, uncomment and test locally 
    
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND", endpoint, NULL);
    if (verbose)
        zstr_send (server, "VERBOSE");

    mlm_client_t *producer = mlm_client_new ();
    mlm_client_connect (producer, endpoint, 1000, "producer");
    mlm_client_set_producer (producer, "ASSETS");

    zactor_t *configurator = zactor_new (bios_composite_metrics_configurator_server, NULL);
    assert (configurator);
    
    zstr_sendx (configurator, "CONNECT", endpoint, "configurator", NULL);
    zstr_sendx (configurator, "CONSUMER", "ASSETS", ".*", NULL);
    zstr_sendx (configurator, "CFG_DIRECTORY", "./test_dir", NULL);
    zclock_sleep (500);

    bios_proto_t *asset = NULL;
    asset = test_asset_new ("DC-Rozskoky", BIOS_PROTO_ASSET_OP_CREATE); // 1
    bios_proto_aux_insert (asset, "parent", "%s", "0");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "datacenter");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "max_power" , "%s",  "2");
    zmsg_t *zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    int rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Lazer game", BIOS_PROTO_ASSET_OP_CREATE); // 2
    bios_proto_aux_insert (asset, "parent", "%s", "1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "room");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Curie", BIOS_PROTO_ASSET_OP_CREATE); // 3
    bios_proto_aux_insert (asset, "parent", "%s", "1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "room");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);


    asset = test_asset_new ("Lazer game.Row01", BIOS_PROTO_ASSET_OP_CREATE); // 4
    bios_proto_aux_insert (asset, "parent", "%s", "2");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    // testing situation when sensor asset message arrives before asset specified in logical_asset
    asset = test_asset_new ("Sensor01", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH1");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "1");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "10");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "bottom");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Rack01", BIOS_PROTO_ASSET_OP_CREATE); // 5 
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Rack02", BIOS_PROTO_ASSET_OP_CREATE); // 6
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    // Row + Racks for Curie
    asset = test_asset_new ("Curie.Row01", BIOS_PROTO_ASSET_OP_CREATE); // 7
    bios_proto_aux_insert (asset, "parent", "%s", "3");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Curie.Row02", BIOS_PROTO_ASSET_OP_CREATE); // 8
    bios_proto_aux_insert (asset, "parent", "%s", "3");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Rack03", BIOS_PROTO_ASSET_OP_CREATE); // 9 
    bios_proto_aux_insert (asset, "parent", "%s", "7");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Rack04", BIOS_PROTO_ASSET_OP_CREATE); // 10
    bios_proto_aux_insert (asset, "parent", "%s", "8");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Rack01.ups1", BIOS_PROTO_ASSET_OP_CREATE); // 11  
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_aux_insert (asset, "parent", "%s", "5");
    bios_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Sensor02", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH2");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "2");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "20");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "bottom");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    asset = test_asset_new ("Sensor03", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH3");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "3");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "30");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "middle");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    assert (zmessage);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (200);

    zactor_destroy (&configurator);
    mlm_client_destroy (&producer);
    zactor_destroy (&server);
    */
    //  @end
    
    printf ("OK\n");
}

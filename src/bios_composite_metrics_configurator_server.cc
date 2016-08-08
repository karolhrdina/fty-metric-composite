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
#include <regex>

#include "composite_metrics_classes.h"

// Wrapper for bios_proto_ext_string
static float
s_bios_proto_ext_float (bios_proto_t *self, const char *key, float default_value)
{
    assert (self);
    assert (key);
    const char *str = bios_proto_ext_string (self, key, NULL);
    if (!str)
        return default_value;
    float f = 0.0;
    try {
        size_t pos = 0;
        f = std::stof (str, &pos);
        if (pos != strlen (str)) {
            log_error ("string '%s' does not represent a number (floating point)", str);
            return default_value;
        }
    }
    catch (const std::exception& e) {
        log_error ("string '%s' does not represent a number (floating point)", str);
        return default_value;
    }
    return f;
}

// Copied from agent-nut
// -1 - error, subprocess code - success
static int
s_bits_systemctl (const char *operation, const char *service)
{
    assert (operation);
    assert (service);
    log_debug ("calling `sudo systemctl '%s' '%s'`", operation, service);

    std::vector <std::string> _argv = {"sudo", "systemctl", operation, service};

    SubProcess systemd (_argv);
    if (systemd.run()) {
        int result = systemd.wait (false);
        log_info ("sudo systemctl '%s' '%s' result  == %i (%s)",
                  operation, service, result, result == 0 ? "ok" : "failed");
        return result;
    }
    log_error ("can't run sudo systemctl '%s' '%s' command", operation, service);
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
        log_error ("zdir_new (path = '%s', parent = '-') failed.", path_to_dir);
        return 1;
    }

    zlist_t *files = zdir_list (dir);
    if (!files) {
        zdir_destroy (&dir);
        log_error ("zdir_list () failed.");
        return 1;
    }

    std::regex file_rex (".+\\.cfg");

    zfile_t *item = (zfile_t *) zlist_first (files);
    while (item) {
        if (std::regex_match (zfile_filename (item, path_to_dir), file_rex)) {
            std::string filename = zfile_filename (item, path_to_dir);
            filename.erase (filename.size () - 4);
            std::string service = "composite-metrics@";
            service += filename;
            s_bits_systemctl ("stop", service.c_str ());
            s_bits_systemctl ("disable", service.c_str ());
            zfile_remove (item);
            log_debug ("file removed");
        }
        item = (zfile_t *) zlist_next (files);
    }
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
        log_error ("zfile_new (path = NULL, file = '%s') failed.", fullpath);
        return 1;
    }
    if (zfile_output (file) == -1) {
        log_error ("zfile_output () failed; filename = '%s'", zfile_filename (file, NULL));
        zfile_close (file);
        zfile_destroy (&file);
        return 1;
    }
    zchunk_t *chunk = zchunk_new ((const void *) contents, strlen (contents));
    if (!chunk) {
        zfile_close (file);
        zfile_destroy (&file);
        log_error ("zchunk_new () failed");
        return 1;
    }
    int rv = zfile_write (file, chunk, 0);
    zchunk_destroy (&chunk);
    zfile_close (file);
    zfile_destroy (&file);
    if (rv == -1) {
        log_error ("zfile_write () failed");
        return 1;
    }
    return 0;
}

// Generate todo
// 0 - success, 1 - failure
static void 
s_generate_and_start (const char *path_to_dir, const char *sensor_function, const char *asset_name, zlistx_t **sensors_p, std::set <std::string> &newMetricsGenerated)
{
    assert (path_to_dir);
    assert (asset_name);
    assert (sensors_p);

    zlistx_t *sensors = *sensors_p;

    if (!sensors) {
        log_error ("parameter '*sensors_p' is NULL");
        return;
    }

    if (zlistx_size (sensors) == 0) {
        zlistx_destroy (sensors_p);
        *sensors_p = NULL;
        return;
    }

    std::string temp_in = "[ ", hum_in = "[ ";

    float temp_offset_total = 0.0, hum_offset_total = 0.0;
    int32_t temp_offset_count = 0, hum_offset_count = 0;
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

        temp_offset_total += s_bios_proto_ext_float (item, "calibration_offset_t", 0.0);
        temp_offset_count++;
        hum_offset_total += s_bios_proto_ext_float (item, "calibration_offset_h", 0.0);
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
                           "                  return '##RESULT_TOPIC##', tmp, '##UNITS##', 0;\"\n"
                           "}\n";
    std::string contents = json_tmpl;

    double clb = 0;
    if (temp_offset_count != 0)
        clb = temp_offset_total / temp_offset_count;

    contents.replace (contents.find ("##IN##"), strlen ("##IN##"), temp_in);
    contents.replace (contents.find ("##CLB##"), strlen ("##CLB##"), std::to_string (clb));
    std::string qnty = "temperature";
    if (sensor_function) {
        qnty += "-";
        qnty += sensor_function;
    }
    std::string result_topic = "average.";
    result_topic += qnty;
    result_topic += "@";
    result_topic += asset_name;
    contents.replace (contents.find ("##RESULT_TOPIC##"), strlen ("##RESULT_TOPIC##"), result_topic);
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
        newMetricsGenerated.insert (result_topic);
    }
    else {
        log_error (
                "Creating config file '%s' failed. Service '%s' not started.",
                fullpath.c_str (), filename.c_str ());
    }

    clb = 0.0;
    if (hum_offset_count != 0)
        clb = hum_offset_total / hum_offset_count;

    contents = json_tmpl;
    contents.replace (contents.find ("##IN##"), strlen ("##IN##"), hum_in);
    contents.replace (contents.find ("##CLB##"), strlen ("##CLB##"), std::to_string (clb));
    qnty = "humidity";
    if (sensor_function) {
        qnty += "-";
        qnty += sensor_function;
    }
    result_topic = "average.";
    result_topic += qnty;
    result_topic += "@";
    result_topic += asset_name;
    contents.replace (contents.find ("##RESULT_TOPIC##"), strlen ("##RESULT_TOPIC##"), result_topic);
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
        newMetricsGenerated.insert (result_topic);
    }
    else {
        log_error (
                "Creating config file '%s' failed. Service '%s' not started.",
                fullpath.c_str (), filename.c_str ());
    }   
    return;
} 

static void
s_regenerate (data_t *data, std::set <std::string> &metrics_unavailable)
{
    assert (data);
    // potential unavailable metrics are those, what are now still available
    metrics_unavailable = data_get_produced_metrics (data); 
    // 1. Delete all files in output dir and stop/disable services
    int rv = s_remove_and_stop (data_cfgdir (data));
    if (rv != 0) {
        log_error (
                "Error removing old config files from directory '%s'. New config "
                "files were NOT generated and services were NOT started.", data_cfgdir (data));
        return;
    }

    // 2. Generate new files and enable/start services
    zlistx_t *assets = data_asset_names (data);
    if (!assets) {
        log_error ("data_asset_names () failed");
        return;
    }

    const char *asset = (const char *) zlistx_first (assets);
    std::set <std::string> metricsAvailable;
    while (asset) {
        bios_proto_t *proto = data_asset (data, asset);
        if (streq (bios_proto_aux_string (proto, "type", ""), "rack")) {
            zlistx_t *sensors = NULL;
            // Ti, Hi
            sensors = data_sensor (data, asset, "input");
            s_generate_and_start (data_cfgdir (data), "input", asset, &sensors, metricsAvailable);

            // To, Ho
            sensors = data_sensor (data, asset, "output");
            s_generate_and_start (data_cfgdir (data), "output", asset, &sensors, metricsAvailable);
        }
        else {
            zlistx_t *sensors = NULL;
            // T, H
            sensors = data_sensor (data, asset, NULL);
            s_generate_and_start (data_cfgdir (data), NULL, asset, &sensors, metricsAvailable);
        }
        asset = (const char *) zlistx_next (assets);
    }
    for ( const auto &one_metric : metricsAvailable ) {
        metrics_unavailable.erase (one_metric);
    }
    data_set_produced_metrics (data, metricsAvailable);
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
        log_error ("mlm_client_new () failed");
        return;
    }

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    if (!poller) {
        log_error ("zpoller_new () failed");
        mlm_client_destroy (&client);
        return;
    }

    data_t *data = data_new ();
    zsock_signal (pipe, 0);

    bool flag = false;

    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, 30000);

        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted) {
                log_warning (
                    "zpoller_terminated () == '%s' or zsys_interrupted == '%s'",
                    zpoller_terminated (poller) ? "true" : "false", zsys_interrupted ? "true" : "false");
                break;
            }
            if (zpoller_expired (poller) && flag) {
                std::set <std::string> metrics_unavailable;
                s_regenerate (data, metrics_unavailable);
                for ( const auto &one_metric : metrics_unavailable ) {
                    proto_metric_unavailable_send (client, one_metric.c_str());
                }// ACE: HERE 
                flag = false;
            }
            continue;
        }

        if (which == pipe) {
            zmsg_t *message = zmsg_recv (pipe);
            if (!message) {
                log_error ("Given `which == pipe`, function `zmsg_recv (pipe)` returned NULL");
                continue;
            }
            if (actor_commands (client, &message, data) == 1) {
                break;
            }
            continue;
        }

        if (which != mlm_client_msgpipe (client)) {
            log_error ("which was checked for NULL, pipe and now should have been `mlm_client_msgpipe (client)` but is not.");
            continue;
        }

        zmsg_t *message = mlm_client_recv (client);
        if (!message) {
            log_error ("Given `which == mlm_client_msgpipe (client)`, function `mlm_client_recv ()` returned NULL");
            continue;
        }

        const char *command = mlm_client_command (client);
        if (streq (command, "STREAM DELIVER")) {
            bios_proto_t *proto = bios_proto_decode (&message);
            if (!proto) {
                log_error (
                        "bios_proto_decode () failed; sender = '%s', subject = '%s'",
                        mlm_client_sender (client), mlm_client_subject (client));
                continue;
            }
            data_asset_put (data, &proto);
            assert (proto == NULL);

            flag |= data_asset_sensors_changed (data);
        }
        else
        if (streq (command, "MAILBOX DELIVER") ||
            streq (command, "SERVICE DELIVER")) {
            log_warning (
                    "Received a message from sender = '%s', command = '%s', subject = '%s'. Throwing away.",
                    mlm_client_sender (client), mlm_client_command (client), mlm_client_subject (client));
            continue;
        }
        else {
            log_error ("Unrecognized mlm_client_command () = '%s'", command ? command : "(null)");
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


//  Helper test function
//  Test directory contents for expected files
//  0 - ok, 1 - failure

static int 
test_dir_contents (
        const std::string& directory,
        std::vector <std::string>& expected)
{
    printf ("test_dir_contents (): start\n");
    zdir_t *dir = zdir_new (directory.c_str (), "-");
    assert (dir);

    zlist_t *files = zdir_list (dir);
    assert (files);

    std::regex file_rex (".+\\.cfg");

    zfile_t *item = (zfile_t *) zlist_first (files);
    while (item) {
        if (std::regex_match (zfile_filename (item, directory.c_str ()), file_rex)) {
            bool found = false;
            for (auto it = expected.begin (); it != expected.end (); it++)
            {
                if (it->compare (zfile_filename (item, directory.c_str ())) == 0) {
                    expected.erase (it);
                    found = true;
                    break;
                }
            }
            if (!found) {
                printf ("Filename '%s' present in directory but not expected.\n", zfile_filename (item, directory.c_str ()));
                zlist_destroy (&files);
                zdir_destroy (&dir);
                return 1;
            }
        }
        item = (zfile_t *) zlist_next (files);
    }
    zlist_destroy (&files);
    zdir_destroy (&dir);
    if (expected.size () != 0) {
        printf ("Some files were expected but were not present\n");
        return 1;
    }
    return 0;
}


// Improvement memo: make expected_configs a string->string map and check the file contents
//                   as well (or parse json). 

void
bios_composite_metrics_configurator_server_test (bool verbose)
{

    static const char* endpoint = "inproc://bios-composite-configurator-server-test";
    printf (" * bios_composite_metrics_configurator_server: ");
    if (verbose)
        printf ("\n");

    //  @selftest
    
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND", endpoint, NULL);
    if (verbose)
        zstr_send (server, "VERBOSE");
    zclock_sleep (100);

    mlm_client_t *producer = mlm_client_new ();
    mlm_client_connect (producer, endpoint, 1000, "producer");
    mlm_client_set_producer (producer, "ASSETS");
    zclock_sleep (100);

    zactor_t *configurator = zactor_new (bios_composite_metrics_configurator_server, NULL);
    assert (configurator);
    zclock_sleep (100);
 
    zstr_sendx (configurator, "CFG_DIRECTORY", "./test_dir", NULL);
    zstr_sendx (configurator, "STATE_FILE", "./test_state_file", NULL);   
    zstr_sendx (configurator, "CONNECT", endpoint, "configurator", NULL);
    zstr_sendx (configurator, "CONSUMER", "ASSETS", ".*", NULL);
    zstr_sendx (configurator, "PRODUCER", "_METRICS_UNAVAILABLE", ".*", NULL);
    zclock_sleep (500);

    bios_proto_t *asset = NULL;

    printf ("TRACE CREATE DC-Rozskoky\n");
    asset = test_asset_new ("DC-Rozskoky", BIOS_PROTO_ASSET_OP_CREATE); // 1
    bios_proto_aux_insert (asset, "parent", "%s", "0");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "datacenter");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "max_power" , "%s",  "2");
    zmsg_t *zmessage = bios_proto_encode (&asset);
    int rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Lazer game\n");
    asset = test_asset_new ("Lazer game", BIOS_PROTO_ASSET_OP_CREATE); // 2
    bios_proto_aux_insert (asset, "parent", "%s", "1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "room");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Curie\n");
    asset = test_asset_new ("Curie", BIOS_PROTO_ASSET_OP_CREATE); // 3
    bios_proto_aux_insert (asset, "parent", "%s", "1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "room");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Lazer game.Row01\n");
    asset = test_asset_new ("Lazer game.Row01", BIOS_PROTO_ASSET_OP_CREATE); // 4
    bios_proto_aux_insert (asset, "parent", "%s", "2");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    // testing situation when sensor asset message arrives before asset specified in logical_asset
    printf ("TRACE CREATE Sensor01\n");
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
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Rack01\n");
    asset = test_asset_new ("Rack01", BIOS_PROTO_ASSET_OP_CREATE); // 5 
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Rack02\n");
    asset = test_asset_new ("Rack02", BIOS_PROTO_ASSET_OP_CREATE); // 6
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Curie.Row01\n");
    asset = test_asset_new ("Curie.Row01", BIOS_PROTO_ASSET_OP_CREATE); // 7
    bios_proto_aux_insert (asset, "parent", "%s", "3");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Curie.Row02\n");
    asset = test_asset_new ("Curie.Row02", BIOS_PROTO_ASSET_OP_CREATE); // 8
    bios_proto_aux_insert (asset, "parent", "%s", "3");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Rack03\n");
    asset = test_asset_new ("Rack03", BIOS_PROTO_ASSET_OP_CREATE); // 9 
    bios_proto_aux_insert (asset, "parent", "%s", "7");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (2000);

    printf ("TRACE CREATE Rack04\n");
    asset = test_asset_new ("Rack04", BIOS_PROTO_ASSET_OP_CREATE); // 10
    bios_proto_aux_insert (asset, "parent", "%s", "8");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Rack01.ups1\n");
    asset = test_asset_new ("Rack01.ups1", BIOS_PROTO_ASSET_OP_CREATE); // 11  
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_aux_insert (asset, "parent", "%s", "5");
    bios_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor02\n");
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
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor03\n");
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
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (100);

    // The following 4 sensors have important info missing
    printf ("TRACE CREATE Sensor04\n");
    asset = test_asset_new ("Sensor04", BIOS_PROTO_ASSET_OP_CREATE);
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
    // logical_asset missing
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor05\n");
    asset = test_asset_new ("Sensor05", BIOS_PROTO_ASSET_OP_CREATE);
    // parent missing
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
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor06\n");
    asset = test_asset_new ("Sensor06", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    // parent_name missing
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
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor07\n");
    asset = test_asset_new ("Sensor07", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    // port missing
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "3");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "30");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "middle");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor08\n");
    asset = test_asset_new ("Sensor08", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH4");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "1");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "1");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "bottom");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack02");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor09\n");
    asset = test_asset_new ("Sensor09", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH5");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "2.0");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "2.0");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "top");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack02");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor10\n");
    asset = test_asset_new ("Sensor10", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH6");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "top");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor11\n");
    asset = test_asset_new ("Sensor11", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH7");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "15.5");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "20");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "top");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor12\n");
    asset = test_asset_new ("Sensor12", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH8");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "0");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "0");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "neuvedeno");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor13\n");
    asset = test_asset_new ("Sensor13", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH9");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "-1");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "1");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "top");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Curie.Row02");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor14\n");
    asset = test_asset_new ("Sensor14", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH10");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "bottom");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Curie");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor15\n");
    asset = test_asset_new ("Sensor15", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH11");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "1.4");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-1");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "middle");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Curie.Row02");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE ---===### (Test block -1-) ###===---\n");
    {
        printf ("Sleeping 1m for configurator kick in and finish\n");
        zclock_sleep (60000); // magical constant

        std::vector <std::string> expected_configs = {
            "Rack01-input-temperature.cfg",
            "Rack01-input-humidity.cfg",
            "Rack01-output-temperature.cfg",
            "Rack01-output-humidity.cfg",
            "Rack02-input-temperature.cfg",
            "Rack02-input-humidity.cfg",
            "Rack02-output-temperature.cfg",
            "Rack02-output-humidity.cfg",
            "Curie.Row02-temperature.cfg",
            "Curie.Row02-humidity.cfg",
            "Curie-temperature.cfg",
            "Curie-humidity.cfg"
        };

        int rv = test_dir_contents ("./test_dir", expected_configs);
        printf ("rv == %d\n", rv);
        assert (rv == 0);
        printf ("Test block -1- Ok\n");
    }

    printf ("TRACE CREATE ups2\n");
    asset = test_asset_new ("ups2", BIOS_PROTO_ASSET_OP_CREATE); // 12
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_aux_insert (asset, "parent", "%s", "10");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor01\n");
    asset = test_asset_new ("Sensor01", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH1");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "-5.2");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "bottom");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor02\n");
    asset = test_asset_new ("Sensor02", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "12");
    bios_proto_aux_insert (asset, "parent_name", "%s", "ups2");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH1");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "-7");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-4.14");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "middle");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor03\n");
    asset = test_asset_new ("Sensor03", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH3");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor10\n");
    asset = test_asset_new ("Sensor10", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH2");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-0.16");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "top");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE RETIRE Sensor11\n");
    asset = test_asset_new ("Sensor11", BIOS_PROTO_ASSET_OP_RETIRE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor08\n");
    asset = test_asset_new ("Sensor08", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH4");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "2.0");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "12");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "middle");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "input");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "DC-Rozskoky");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor09\n");
    asset = test_asset_new ("Sensor09", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH5");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "5");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "50");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Curie.Row02");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor04\n");
    asset = test_asset_new ("Sensor04", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH12");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "1");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "1");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Lazer game");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor05\n");
    asset = test_asset_new ("Sensor05", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH13");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "4");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-6");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "top");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "DC-Rozskoky");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor06\n");
    asset = test_asset_new ("Sensor06", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH14");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "-1.2");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-1.4");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Lazer game");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor07\n");
    asset = test_asset_new ("Sensor07", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH15");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "4");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-25");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "ambient");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Curie.Row02");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE DELETE Sensor12\n");
    asset = test_asset_new ("Sensor12", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");       
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor14\n");
    asset = test_asset_new ("Sensor14", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "12");
    bios_proto_aux_insert (asset, "parent_name", "%s", "ups2");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH10");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "This-asset-does-not-exist");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE UPDATE Sensor15\n");
    asset = test_asset_new ("Sensor15", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH11");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "2.1");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-3.3");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Curie");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE DELETE Sensor13\n");
    asset = test_asset_new ("Sensor13", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE ---===### (Test block -2-) ###===---\n");
    {
        printf ("Sleeping 1m for configurator kick in and finish\n");
        zclock_sleep (60000);

        std::vector <std::string> expected_configs = {
            "Rack01-input-temperature.cfg",
            "Rack01-input-humidity.cfg",
            "Rack01-output-temperature.cfg",
            "Rack01-output-humidity.cfg",
            "DC-Rozskoky-temperature.cfg",
            "DC-Rozskoky-humidity.cfg",
            "Lazer game-temperature.cfg",
            "Lazer game-humidity.cfg",
            "Curie.Row02-temperature.cfg",
            "Curie.Row02-humidity.cfg",
            "Curie-temperature.cfg",
            "Curie-humidity.cfg"
        };

        int rv = test_dir_contents ("./test_dir", expected_configs);
        printf ("rv == %d\n", rv);
        assert (rv == 0);       
        printf ("Test block -2- Ok\n");
    }

    printf ("TRACE DELETE Sensor15\n");
    asset = test_asset_new ("Sensor15", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE DELETE Curie.Row02\n");
    asset = test_asset_new ("Curie.Row02", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE Sensor16\n");
    asset = test_asset_new ("Sensor16", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "13");
    bios_proto_aux_insert (asset, "parent_name", "%s", "nas rack controller");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH2");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-3.51");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "DC-Rozskoky");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE CREATE nas rack controller\n");
    asset = test_asset_new ("nas rack controller", BIOS_PROTO_ASSET_OP_CREATE); // 12
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "rack controller");
    bios_proto_aux_insert (asset, "parent", "%s", "5");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01");
    zmessage = bios_proto_encode (&asset);
    rv = mlm_client_send (producer, "Nobody here cares about this.", &zmessage);
    assert (rv == 0);
    zclock_sleep (50);

    printf ("TRACE ---===### (Test block -3-) ###===---\n");
    {
        printf ("Sleeping 1m for configurator kick in and finish\n");
        zclock_sleep (60000);

        std::vector <std::string> expected_configs = {
            "Rack01-input-temperature.cfg",
            "Rack01-input-humidity.cfg",
            "Rack01-output-temperature.cfg",
            "Rack01-output-humidity.cfg",
            "DC-Rozskoky-temperature.cfg",
            "DC-Rozskoky-humidity.cfg",
            "Lazer game-temperature.cfg",
            "Lazer game-humidity.cfg"
        };

        int rv = test_dir_contents ("./test_dir", expected_configs);
        printf ("rv == %d\n", rv);
        assert (rv == 0);
        printf ("Test block -3- Ok\n");
    }

    mlm_client_destroy (&producer);
    zactor_destroy (&configurator);
    zactor_destroy (&server);
    //  @end
    printf ("OK\n");
}

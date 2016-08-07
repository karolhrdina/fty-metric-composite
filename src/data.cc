/*  =========================================================================
    data - composite metrics data structure

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
    data - composite metrics data structure
@discuss
@end
*/

#include "composite_metrics_classes.h"
#include <set>
#include <string>
//  Structure of our class

struct _data_t {
    zhashx_t *sensors; // (asset name -> zlistx_t of sensor bios_proto_t* messages)
    zhashx_t *assets; // (asset name -> latest bios_proto_t* asset message)
    bool sensors_updated; // sensors data was change during last data_asset_put () call
    std::set<std::string> produced_metrics; // list of metrics, that are now produced by composite_metric
    char *state_file;
    char *output_dir;
};

//  --------------------------------------------------------------------------
//  Create a new data

data_t *
data_new (void)
{
    data_t *self = (data_t *) zmalloc (sizeof (data_t));
    assert (self);
    //  Initialize class properties here
    //  sensors
    self->sensors = zhashx_new ();
    zhashx_set_destructor (self->sensors, (zhashx_destructor_fn *) zlistx_destroy);
    //  assets
    self->assets = zhashx_new ();
    zhashx_set_destructor (self->assets, (zhashx_destructor_fn *) bios_proto_destroy);
    //  sensors_updated
    self->sensors_updated = false;
    //  state_file
    self->state_file = strdup (""); 
    //  output_dir
    self->output_dir = strdup ("");  
    return self;
}

//  --------------------------------------------------------------------------
//  Store asset 

void
data_asset_put (data_t *self, bios_proto_t **message_p)
{
    assert (self);
    assert (message_p);
    if (!*message_p)
        return;
    bios_proto_t *message = *message_p;

    const char *operation = bios_proto_operation (message);
    const char *type = bios_proto_aux_string (message, "type", "");
    const char *subtype = bios_proto_aux_string (message, "subtype", "");

    self->sensors_updated = false;

    if (streq (type, "datacenter") ||
        streq (type, "room") ||
        streq (type, "row") ||
        streq (type, "rack"))
    {
        if (streq (operation, BIOS_PROTO_ASSET_OP_CREATE) ||
            streq (operation, BIOS_PROTO_ASSET_OP_UPDATE))
        {
            zlistx_t *list = (zlistx_t *) zhashx_lookup (self->sensors, bios_proto_name (message));
            if (list &&
                zhashx_lookup (self->assets, bios_proto_name (message)) == NULL) {
                self->sensors_updated = true;
            }
            if (!list) {
                zlistx_t *list = zlistx_new ();
                zlistx_set_destructor (list, (czmq_destructor *) bios_proto_destroy);
                zhashx_insert (self->sensors, bios_proto_name (message), (void *) list);
            }
            zhashx_update (self->assets, bios_proto_name (message), (void *) message);
        }
        else
        if (streq (operation, BIOS_PROTO_ASSET_OP_DELETE) ||
            streq (operation, BIOS_PROTO_ASSET_OP_RETIRE))
        {
            void *exists = zhashx_lookup (self->assets, bios_proto_name (message));
            if (exists)
                self->sensors_updated = true;
            zhashx_delete (self->sensors, bios_proto_name (message));
            zhashx_delete (self->assets, bios_proto_name (message));
            bios_proto_destroy (message_p);
        }
        else
        {
            bios_proto_destroy (message_p);
        }
    }
    else
    if (streq (type, "device") && streq (subtype, "sensor"))
    {
        if (streq (operation, BIOS_PROTO_ASSET_OP_DELETE) ||
            streq (operation, BIOS_PROTO_ASSET_OP_RETIRE)) {

            void *handle = NULL;
            char *logical_asset = NULL;
            zlistx_t *sensors_list = (zlistx_t *) zhashx_first (self->sensors);
            while (sensors_list) {
                bios_proto_t *proto = (bios_proto_t *) zlistx_first (sensors_list);
                while (proto) {
                    if (streq (bios_proto_name (message), bios_proto_name (proto))) {
                        handle = zlistx_cursor (sensors_list);
                        if (bios_proto_ext_string (proto, "logical_asset", NULL))
                            logical_asset = strdup (bios_proto_ext_string (proto, "logical_asset", NULL));
                        break;
                    }
                    proto = (bios_proto_t *) zlistx_next (sensors_list);
                }
                if (handle) {
                    break;
                }
                sensors_list = (zlistx_t *) zhashx_next (self->sensors);
            }
            if (handle) {
                zlistx_delete (sensors_list, handle);
                if (zlistx_size (sensors_list) != 0 ||
                    zhashx_lookup (self->assets, logical_asset) != NULL) {
                    self->sensors_updated = true;
                }
            }
            zstr_free (&logical_asset);
            bios_proto_destroy (message_p);
            *message_p = NULL;
            return;
        }

        const char *logical_asset = bios_proto_ext_string (message, "logical_asset", NULL);
        const char *parent = bios_proto_aux_string (message, "parent", NULL);
        const char *port = bios_proto_ext_string (message, "port", NULL);
        const char *parent_name = bios_proto_aux_string (message, "parent_name", NULL);

        if (!logical_asset) {
            log_error (
                    "Attribute '%s' is missing from '%s' field of message where asset name = '%s'. "
                    "This message is not stored.",
                    "logical_asset", "ext", bios_proto_name (message));
            bios_proto_destroy (message_p);
            *message_p = NULL;
            return;
        }
        if (!parent) {
            log_error (
                    "Attribute '%s' is missing from '%s' field of message where asset name = '%s'. "
                    "This message is not stored.",
                    "parent", "aux", bios_proto_name (message));
            bios_proto_destroy (message_p);
            *message_p = NULL;
            return;           
        }
        if (!parent_name) {
            log_error (
                    "Attribute '%s' is missing from '%s' field of message where asset name = '%s'. "
                    "This message is not stored.",
                    "parent_name", "ext", bios_proto_name (message));
            bios_proto_destroy (message_p);
            *message_p = NULL;
            return;           
        }
        if (!port) {
            log_error (
                    "Attribute '%s' is missing from '%s' field of message where asset name = '%s'. "
                    "This message is not stored.",
                    "port", "ext", bios_proto_name (message));
            bios_proto_destroy (message_p);
            *message_p = NULL;
            return;           
        }

        zlistx_t *list = (zlistx_t *) zhashx_lookup (self->sensors, logical_asset);

        if (streq (operation, BIOS_PROTO_ASSET_OP_CREATE)) {
            if (!list) {
                list = zlistx_new ();
                zlistx_set_destructor (list, (czmq_destructor *) bios_proto_destroy);
                zhashx_insert (self->sensors, logical_asset, (void *) list);
            }
            zlistx_add_end (list, (void *) message);
            if (zhashx_lookup (self->assets, logical_asset) != NULL) {
                self->sensors_updated = true;
            }
        }
        else
        if (streq (operation, BIOS_PROTO_ASSET_OP_UPDATE)) {
            self->sensors_updated = true;
            // Note: When proper asset messages update is implemented, the messages
            //       will have to be merged instead of deleted and re-inserted

            // remove the old one
            void *handle = NULL;
            zlistx_t *sensors_list = (zlistx_t *) zhashx_first (self->sensors);
            while (sensors_list) {
                bios_proto_t *proto = (bios_proto_t *) zlistx_first (sensors_list);
                while (proto) {
                    if (streq (bios_proto_name (message), bios_proto_name (proto))) {
                        handle = zlistx_cursor (sensors_list);
                        break;
                    }
                    proto = (bios_proto_t *) zlistx_next (sensors_list);
                }
                if (handle) {
                    break;
                }
                sensors_list = (zlistx_t *) zhashx_next (self->sensors);
            }
            if (handle) {
                zlistx_delete (sensors_list, handle);
            }

            // insert the new one
            if (!list) {
                list = zlistx_new ();
                zlistx_set_destructor (list, (czmq_destructor *) bios_proto_destroy);
                zhashx_insert (self->sensors, logical_asset, (void *) list);
            }
            zlistx_add_end (list, (void *) message);
        }
        else
        {
            bios_proto_destroy (message_p);
        }
    }
    else
    {
        bios_proto_destroy (message_p);
    }
    *message_p = NULL;
}

//  --------------------------------------------------------------------------
//  Update list of metrics produced by composite_metrics
void
data_set_produced_metrics (data_t *self, std::set <std::string> &metrics)
{
    self->produced_metrics = std::move (metrics);
}

//  --------------------------------------------------------------------------
//  Get list of metrics produced by composite_metrics
std::set <std::string>
data_get_produced_metrics (data_t *self)
{
    return self->produced_metrics;
}

//  --------------------------------------------------------------------------
//  Last data_asset_put () call made changes to sensors data

bool
data_asset_sensors_changed (data_t *self)
{
    assert (self);
    return self->sensors_updated;
}

//  --------------------------------------------------------------------------
//  Get asset names
//  The caller is responsible for destroying the return value when finished with it.

zlistx_t *
data_asset_names (data_t *self)
{
    assert (self);
    zlistx_t *list = zhashx_keys (self->assets);
    zlistx_set_comparator (list, (czmq_comparator *) strcmp);
    return list;
}

//  --------------------------------------------------------------------------
//  Get information for any given asset name returned by `data_asset_names ()` or NULL
//  Ownership is NOT transferred

bios_proto_t *
data_asset (data_t *self, const char *name)
{
    assert (self);
    assert (name);
    return (bios_proto_t *) zhashx_lookup (self->assets, name);
}

//  --------------------------------------------------------------------------
//  Get list of sensors for asset
//  You can limit the list of sensors returned to a certain 'sensor_function',
//  NULL returns all sensors.
//  Returns NULL when 'asset_name' is not among values returned by `data_asset_names ()`
//  The caller is responsible for destroying the return value when finished with it

zlistx_t *
data_sensor (
        data_t *self,
        const char *asset_name,
        const char *sensor_function)
{
    assert (self);
    assert (asset_name);

    bios_proto_t *asset = (bios_proto_t *) zhashx_lookup (self->assets, asset_name);
    if (!asset) {
        log_debug ("Asset name '%s' not stored.", asset_name);
        return NULL;
    }

    zlistx_t *sensors = (zlistx_t *) zhashx_lookup (self->sensors, asset_name);
    if (!sensors) {
        log_error (
                "Internal structure error. Asset name '%s' stored in self->assets but not in self->sensors.",
                asset_name);
        return NULL;
    }
    zlistx_t *list = zlistx_new ();
    zlistx_set_destructor (list, (czmq_destructor *) bios_proto_destroy);
    zlistx_set_duplicator (list, (czmq_duplicator *) bios_proto_dup);

    bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
    while (item) {
        assert (streq (asset_name, bios_proto_ext_string (item, "logical_asset", "")));
        if (!sensor_function) {
            zlistx_add_end (list, (void *) item); 
        }
        else if (streq (sensor_function, bios_proto_ext_string (item, "sensor_function", ""))) {
            zlistx_add_end (list, (void *) item); 
        }
        item = (bios_proto_t *) zlistx_next (sensors);
    }
    return list;
}

//  --------------------------------------------------------------------------
//  Get state file fullpath or empty string if not set

const char *
data_statefile (data_t *self)
{
    assert (self);
    return self->state_file;
}

//  --------------------------------------------------------------------------
//  Set state file fullpath
//  0 - success, -1 - error

int
data_set_statefile (data_t *self, const char *fullpath)
{
    assert (self);
    assert (fullpath);
    zfile_t *file = zfile_new (NULL, fullpath);
    if (!file) {
        log_error ("zfile_new (NULL, '%s') failed.", fullpath);
        return -1;
    }
    bool is_dir = zfile_is_directory (file);
    if (is_dir) {
        log_error ("Specified argument '%s' is a directory.", fullpath);
        zfile_destroy (&file);
        return -1;
    }
    int rv = zfile_output (file);
    zfile_destroy (&file);
    if (rv == -1) {
        log_error ("Specified argument '%s' is not writable.", fullpath);
        return -1;
    }
    zstr_free (&self->state_file);
    self->state_file = strdup (fullpath);
    return 0;
}

//  --------------------------------------------------------------------------
//  Get path to configuration directory

const char *
data_cfgdir (data_t *self)
{
    assert (self);
    return self->output_dir;
}

//  --------------------------------------------------------------------------
//  Set configuration directory path
//  0 - success, -1 - error 

int
data_set_cfgdir (data_t *self, const char *path)
{
    assert (self);
    assert (path);
    zdir_t *dir = zdir_new (path, "-");
    if (!dir) {
        log_error ("zdir_new ('%s', \"-\") failed.", path);
        return -1;
    }
    zdir_destroy (&dir);
    zstr_free (&self->output_dir);
    self->output_dir = strdup (path);
    return 0;
}

//  --------------------------------------------------------------------------
//  Save data to disk
//  0 - success, -1 - error

int
data_save (data_t *self)
{
    return 0;
}

//  --------------------------------------------------------------------------
//  Load data from disk
//  0 - success, -1 - error

int
data_load (data_t *self)
{
    return 0;
}

//  --------------------------------------------------------------------------
//  Destroy the data

void
data_destroy (data_t **self_p)
{
    if (!self_p)
        return;
    if (*self_p) {
        data_t *self = *self_p;
        //  Free class properties here
        zhashx_destroy (&self->sensors);
        zhashx_destroy (&self->assets);
        zstr_free (&self->state_file);
        zstr_free (&self->output_dir);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
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
//  0 - same, 1 - different  
static int
test_zlistx_compare (zlistx_t *expected, zlistx_t **received_p, int print = 0)
{
    assert (expected); 
    assert (received_p && *received_p);

    zlistx_t *received = *received_p;

    if (print) {
        zsys_debug ("received");
        const char *first = (const char *) zlistx_first (received);
        while (first) {
            zsys_debug ("\t%s", first);
            first = (const char *) zlistx_next (received);
        }
        zsys_debug ("expected");
        first = (const char *) zlistx_first (expected);
        while (first) {
            zsys_debug ("\t%s", first);
            first = (const char *) zlistx_next (expected);
        }
    }

    int rv = 1;
    const char *cursor = (const char *) zlistx_first (expected);
    while (cursor) {
        void *handle = zlistx_find (received, (void *) cursor);
        if (!handle) {
            zlistx_destroy (received_p);
            *received_p = NULL;
            return 1;
        }
        zlistx_delete (received, handle);
        cursor = (const char *) zlistx_next (expected);
    }
    if (zlistx_size (received) == 0)
        rv = 0;
    zlistx_destroy (received_p);
    *received_p = NULL;
    return rv;
}

void
data_test (bool verbose)
{
    printf (" * data: \n");

    //  @selftest
    //  Simple create/destroy test    
    data_t *self = data_new ();
    assert (self);
    
    data_destroy (&self);
    assert (self == NULL);
    
    data_destroy (&self);
    assert (self == NULL);
    
    self = data_new ();

    // data_statefile () 
    // data_set_statefile ()
    {
    const char *state_file = data_statefile (self);
    assert (streq (state_file, ""));

    int rv = data_set_statefile (self, "./state_file");
    assert (rv == 0);
    state_file = data_statefile (self);
    assert (streq (state_file, "./state_file"));
    
    rv = data_set_statefile (self, "/tmp/composite-metrics/state_file");
    assert (rv == 0);
    state_file = data_statefile (self);
    assert (streq (state_file, "/tmp/composite-metrics/state_file"));

    rv = data_set_statefile (self, "./test_dir/state_file");
    assert (rv == 0);
    state_file = data_statefile (self);
    assert (streq (state_file, "./test_dir/state_file"));

    // directory
    rv = data_set_statefile (self, "/lib");
    assert (rv == -1);
    state_file = data_statefile (self);
    assert (streq (state_file, "./test_dir/state_file"));

    // non-existing file
    rv = data_set_statefile (self, "/lib/state_file");
    assert (rv == -1);
    state_file = data_statefile (self);
    assert (streq (state_file, "./test_dir/state_file"));
    }

    // data_cfgdir
    // data_set_cfgdir
    {
    const char *cfgdir = data_cfgdir (self);
    assert (streq (cfgdir, ""));

    int rv = data_set_cfgdir (self, "/tmp");
    assert (rv == 0);
    cfgdir = data_cfgdir (self);
    assert (streq (cfgdir, "/tmp"));

    // non-writable directory
    rv = data_set_cfgdir (self, "/root");
    assert (rv == -1);
    cfgdir = data_cfgdir (self);
    assert (streq (cfgdir, "/tmp"));
    }

    // asset
    zlistx_t *assets_expected = zlistx_new ();
    zlistx_set_destructor (assets_expected, (czmq_destructor *) zstr_free);
    zlistx_set_duplicator (assets_expected, (czmq_duplicator *) strdup);
    zlistx_set_comparator (assets_expected, (czmq_comparator *) strcmp);

    bios_proto_t *asset = NULL;

    printf ("TRACE CREATE DC-Rozskoky\n");
    asset = test_asset_new ("DC-Rozskoky", BIOS_PROTO_ASSET_OP_CREATE); // 1
    bios_proto_aux_insert (asset, "parent", "%s", "0");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "datacenter");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "max_power" , "%s",  "2");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "DC-Rozskoky");
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE CREATE Lazer game\n");
    asset = test_asset_new ("Lazer game", BIOS_PROTO_ASSET_OP_CREATE); // 2
    bios_proto_aux_insert (asset, "parent", "%s", "1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "room");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Lazer game");
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE CREATE Curie\n");
    asset = test_asset_new ("Curie", BIOS_PROTO_ASSET_OP_CREATE); // 3
    bios_proto_aux_insert (asset, "parent", "%s", "1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "room");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Curie");
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE CREATE Lazer game.Row01\n");
    asset = test_asset_new ("Lazer game.Row01", BIOS_PROTO_ASSET_OP_CREATE); // 4
    bios_proto_aux_insert (asset, "parent", "%s", "2");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Lazer game.Row01");
    assert (data_asset_sensors_changed (self) == false);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == false);

    {
        zlistx_t *received = data_asset_names (self);
        assert (received);

        int rv = test_zlistx_compare (assets_expected, &received, 1);
        assert (rv == 0);
    }

    printf ("TRACE CREATE Rack01\n");
    asset = test_asset_new ("Rack01", BIOS_PROTO_ASSET_OP_CREATE); // 5 
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Rack01");
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE CREATE Rack02\n");
    asset = test_asset_new ("Rack02", BIOS_PROTO_ASSET_OP_CREATE); // 6
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Rack02");
    assert (data_asset_sensors_changed (self) == false);

    // Row + Racks for Curie
    printf ("TRACE CREATE Curie.Row01\n");
    asset = test_asset_new ("Curie.Row01", BIOS_PROTO_ASSET_OP_CREATE); // 7
    bios_proto_aux_insert (asset, "parent", "%s", "3");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Curie.Row01");
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE CREATE Curie.Row02\n");
    asset = test_asset_new ("Curie.Row02", BIOS_PROTO_ASSET_OP_CREATE); // 8
    bios_proto_aux_insert (asset, "parent", "%s", "3");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Curie.Row02");
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE CREATE Rack03\n");
    asset = test_asset_new ("Rack03", BIOS_PROTO_ASSET_OP_CREATE); // 9 
    bios_proto_aux_insert (asset, "parent", "%s", "7");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Rack03");
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE CREATE Rack04\n");
    asset = test_asset_new ("Rack04", BIOS_PROTO_ASSET_OP_CREATE); // 10
    bios_proto_aux_insert (asset, "parent", "%s", "8");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "rack");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "42");
    bios_proto_ext_insert (asset, "description" , "%s",  "Lorem ipsum asd asd asd asd asd asd asd");
    data_asset_put (self, &asset);
    zlistx_add_end (assets_expected, (void *) "Rack04");
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE CREATE Rack01.ups1\n");
    asset = test_asset_new ("Rack01.ups1", BIOS_PROTO_ASSET_OP_CREATE); // 11  
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_aux_insert (asset, "parent", "%s", "5");
    bios_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    data_asset_put (self, &asset); 
    assert (data_asset_sensors_changed (self) == false);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == false);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == false);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == false);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == false);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE CREATE Sensor11\n");
    asset = test_asset_new ("Sensor11", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH7");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "15.5");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "20.7");
    bios_proto_ext_insert (asset, "vertical_position", "%s", "top");
    bios_proto_ext_insert (asset, "sensor_function", "%s", "output");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Rack01");
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE ---===### (Test block -1-) ###===---\n");
    {
        zlistx_t *received = data_asset_names (self);
        assert (received);

        int rv = test_zlistx_compare (assets_expected, &received, 1);
        assert (rv == 0);
        
        asset = data_asset (self, "DC-Rozskoky");
        assert (asset);
        assert (streq (bios_proto_aux_string (asset, "type", ""), "datacenter"));
        assert (streq (bios_proto_name (asset), "DC-Rozskoky"));

        asset = data_asset (self, "Lazer game");
        assert (asset);
        assert (streq (bios_proto_aux_string (asset, "type", ""), "room"));
        assert (streq (bios_proto_name (asset), "Lazer game"));
        assert (streq (bios_proto_aux_string (asset, "parent", ""), "1"));

        asset = data_asset (self, "Curie");
        assert (asset);
        assert (streq (bios_proto_aux_string (asset, "type", ""), "room"));
        assert (streq (bios_proto_name (asset), "Curie"));
        assert (streq (bios_proto_aux_string (asset, "parent", ""), "1"));

        asset = data_asset (self, "Lazer game.Row01");
        assert (asset);
        assert (streq (bios_proto_aux_string (asset, "type", ""), "row"));
        assert (streq (bios_proto_name (asset), "Lazer game.Row01"));
        assert (streq (bios_proto_aux_string (asset, "parent", ""), "2"));

        asset = data_asset (self, "Rack01");
        assert (asset);
        asset = data_asset (self, "Rack02");
        assert (asset);
        asset = data_asset (self, "Rack01.ups1");
        assert (asset == NULL);
        asset = data_asset (self, "non-existing-sensor");
        assert (asset == NULL);

        zlistx_t *sensors = data_sensor (self, "Non-existing-dc", NULL);
        assert (sensors == NULL);

        sensors = data_sensor (self, "DC-Rozskoky", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Lazer game", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie", NULL);
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor14"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH10"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "bottom"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), ""));
            item = (bios_proto_t *) zlistx_next (sensors);

        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie", "input");
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Lazer game.Row01", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row01", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row02", NULL);
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor13"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH9"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "top"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "-1"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "1"));
            item = (bios_proto_t *) zlistx_next (sensors);

            assert (streq (bios_proto_name (item), "Sensor15"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH11"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "middle"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "1.4"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-1"));
            item = (bios_proto_t *) zlistx_next (sensors);
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row02", "input");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor13"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row02", "output");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor15"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack02", NULL);
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor08"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH4"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "bottom"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "1"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "1"));
            item = (bios_proto_t *) zlistx_next (sensors);

            assert (streq (bios_proto_name (item), "Sensor09"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH5"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "top"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "2.0"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "2.0"));
            item = (bios_proto_t *) zlistx_next (sensors);
        }       
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack02", "input");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor08"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack02", "output");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor09"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack03", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack04", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack01", NULL);
        assert (zlistx_size (sensors) == 6);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor01"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH1"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "bottom"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "1"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "10"));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor02"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH2"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "bottom"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "2"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "20"));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor03"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH3"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "middle"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "3"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "30"));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor10"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH6"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "top"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), ""));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor11"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH7"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "top"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "15.5"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "20.7"));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor12"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH8"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "neuvedeno"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "0"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "0"));

        }
        zlistx_destroy (&sensors);    

        sensors = data_sensor (self, "Rack01", "input");
        assert (zlistx_size (sensors) == 3);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor01"));
            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor02"));
            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor03"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack01", "output");
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor10"));
            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor11"));
        }
        zlistx_destroy (&sensors);
    }

    printf ("TRACE CREATE ups2\n");
    asset = test_asset_new ("ups2", BIOS_PROTO_ASSET_OP_CREATE); // 12
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_aux_insert (asset, "parent", "%s", "10");
    data_asset_put (self, &asset); 
    assert (data_asset_sensors_changed (self) == false);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE RETIRE Sensor11\n");
    asset = test_asset_new ("Sensor11", BIOS_PROTO_ASSET_OP_RETIRE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE DELETE Sensor12\n");
    asset = test_asset_new ("Sensor12", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");       
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE UPDATE Sensor14\n");
    asset = test_asset_new ("Sensor14", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "12");
    bios_proto_aux_insert (asset, "parent_name", "%s", "ups2");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH10");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "This-asset-does-not-exist");
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE UPDATE Sensor15\n");
    asset = test_asset_new ("Sensor15", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "parent", "%s", "11");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01.ups1");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    bios_proto_ext_insert (asset, "port", "%s", "TH11");
    bios_proto_ext_insert (asset, "calibration_offset_t", "%s", "2");
    bios_proto_ext_insert (asset, "calibration_offset_h", "%s", "-3");
    bios_proto_ext_insert (asset, "logical_asset", "%s", "Curie");
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE DELETE Sensor13\n");
    asset = test_asset_new ("Sensor13", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    data_asset_put (self, &asset);

    printf ("TRACE ---===### (Test block -2-) ###===---\n");
    {
        zlistx_t *received = data_asset_names (self);
        assert (received);
        int rv = test_zlistx_compare (assets_expected, &received, 1);
        assert (rv == 0);
        
        asset = data_asset (self, "ups2");
        assert (asset == NULL);

        asset = data_asset (self, "This-asset-does-not-exist");
        assert (asset == NULL);

        zlistx_t *sensors = data_sensor (self, "This-asset-does-not-exist", NULL);
        assert (sensors == NULL);

        sensors = data_sensor (self, "DC-Rozskoky", NULL);
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor08"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH4"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "middle"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "2.0"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "12"));
            item = (bios_proto_t *) zlistx_next (sensors);

            assert (streq (bios_proto_name (item), "Sensor05"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH13"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "top"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "4"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-6"));
            item = (bios_proto_t *) zlistx_next (sensors);
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "DC-Rozskoky", "input");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor08"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "DC-Rozskoky", "output");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor05"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Lazer game", NULL);
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor04"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH12"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "1"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "1"));
            item = (bios_proto_t *) zlistx_next (sensors);

            assert (streq (bios_proto_name (item), "Sensor06"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH14"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "-1.2"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-1.4"));
            item = (bios_proto_t *) zlistx_next (sensors);           
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Lazer game", "input");
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);       

        sensors = data_sensor (self, "Lazer game", "output");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor06"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Lazer game", "");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor04"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie", NULL);
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor15"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH11"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "2"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-3"));
            item = (bios_proto_t *) zlistx_next (sensors);

        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie", "input");
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Lazer game.Row01", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row01", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row02", NULL);
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor09"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH5"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "5"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "50"));
            item = (bios_proto_t *) zlistx_next (sensors);

            assert (streq (bios_proto_name (item), "Sensor07"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH15"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "ambient"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "4"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-25"));
            item = (bios_proto_t *) zlistx_next (sensors);
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row02", "input");
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Curie.Row02", "ambient");
        assert (zlistx_size (sensors) == 1);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor07"));
        }
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack02", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack03", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack04", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        sensors = data_sensor (self, "Rack01", NULL);
        assert (zlistx_size (sensors) == 4);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor01"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH1"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "bottom"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "-5.2"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), ""));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor02"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "ups2"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH1"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "middle"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "-7"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-4.14"));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor03"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH3"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), ""));

            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor10"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH2"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "top"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-0.16"));
        }
        zlistx_destroy (&sensors);   

        sensors = data_sensor (self, "Rack01", "output");
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor03"));
            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor10"));
            item = (bios_proto_t *) zlistx_next (sensors);
        }        
        zlistx_destroy (&sensors);   

        sensors = data_sensor (self, "Rack01", "input");
        assert (zlistx_size (sensors) == 2);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor01"));
            item = (bios_proto_t *) zlistx_next (sensors);
            assert (streq (bios_proto_name (item), "Sensor02"));
            item = (bios_proto_t *) zlistx_next (sensors);
        }
        zlistx_destroy (&sensors);   
    }

    printf ("TRACE DELETE Sensor15\n");
    asset = test_asset_new ("Sensor15", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "sensor");
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE DELETE Curie.Row02\n");
    asset = test_asset_new ("Curie.Row02", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "row");
    bios_proto_aux_insert (asset, "subtype", "%s", "unknown");
    data_asset_put (self, &asset);

    void *to_delete = zlistx_find  (assets_expected, (void *) "Curie.Row02");
    assert (to_delete);
    int rv = zlistx_delete (assets_expected, to_delete);
    assert (rv == 0);
    assert (data_asset_sensors_changed (self) == true);

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
    data_asset_put (self, &asset);
    assert (data_asset_sensors_changed (self) == true);

    printf ("TRACE CREATE nas rack constroller\n");
    asset = test_asset_new ("nas rack controller", BIOS_PROTO_ASSET_OP_CREATE); // 12
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "rack controller");
    bios_proto_aux_insert (asset, "parent", "%s", "5");
    bios_proto_aux_insert (asset, "parent_name", "%s", "Rack01");
    data_asset_put (self, &asset); 
    assert (data_asset_sensors_changed (self) == false);

    printf ("TRACE ---===### (Test block -3-) ###===---\n");
    {
        zlistx_t *received = data_asset_names (self);
        assert (received);
        int rv = test_zlistx_compare (assets_expected, &received, 1);
        assert (rv == 0);

        zlistx_t *sensors = data_sensor (self, "DC-Rozskoky", NULL);
        assert (zlistx_size (sensors) == 3);
        {
            bios_proto_t *item = (bios_proto_t *) zlistx_first (sensors);
            assert (streq (bios_proto_name (item), "Sensor08"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH4"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "middle"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "input"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "2.0"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "12"));
            item = (bios_proto_t *) zlistx_next (sensors);

            assert (streq (bios_proto_name (item), "Sensor05"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "Rack01.ups1"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH13"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), "top"));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), "output"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), "4"));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-6"));
            item = (bios_proto_t *) zlistx_next (sensors);

            assert (streq (bios_proto_name (item), "Sensor16"));
            assert (streq (bios_proto_aux_string (item, "parent_name", ""), "nas rack controller"));
            assert (streq (bios_proto_ext_string (item, "port", ""), "TH2"));
            assert (streq (bios_proto_ext_string (item, "vertical_position", ""), ""));
            assert (streq (bios_proto_ext_string (item, "sensor_function", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_t", ""), ""));
            assert (streq (bios_proto_ext_string (item, "calibration_offset_h", ""), "-3.51"));
            item = (bios_proto_t *) zlistx_next (sensors);
        }
        zlistx_destroy (&sensors);

        asset = data_asset (self, "Curie");
        assert (asset);
        assert (streq (bios_proto_aux_string (asset, "type", ""), "room"));
        assert (streq (bios_proto_name (asset), "Curie"));
        assert (streq (bios_proto_aux_string (asset, "parent", ""), "1"));

        sensors = data_sensor (self, "Curie", NULL);
        assert (zlistx_size (sensors) == 0);
        zlistx_destroy (&sensors);

        asset = data_asset (self, "Curie.Row02");
        assert (asset == NULL);
 
        sensors = data_sensor (self, "Curie.Row02", NULL);
        assert (sensors == NULL);
    }

    zlistx_destroy (&assets_expected);
    data_destroy (&self);
    //  @end
    printf (" * data: OK\n");
}

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

//  Structure of our class

struct _data_t {
    zhashx_t *assets;
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
    //  assets
    self->assets = zhashx_new ();
    zhashx_set_destructor (self->assets, (zhashx_destructor_fn *) bios_proto_destroy);
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
    zhashx_update (self->assets, bios_proto_name (message), message);
    *message_p = NULL;
}

//  --------------------------------------------------------------------------
//  Get asset of specified name or NULL

zlistx_t *
data_asset_names (data_t *self)
{
    assert (self);
    zlistx_t *list = zhashx_keys (self->assets);
    zlistx_set_comparator (list, (czmq_comparator *) strcmp);
    return list;
}

//  --------------------------------------------------------------------------
//  Get asset of specified name or NULL

bios_proto_t *
data_asset (data_t *self, const char *name)
{
    assert (self);
    assert (name);
    return (bios_proto_t *) zhashx_lookup (self->assets, name);
}

//  --------------------------------------------------------------------------
//  Get fullpath to state file or NULL

const char *
data_statefile (data_t *self)
{
    assert (self);
    return self->state_file;
}

//  --------------------------------------------------------------------------
//  Set state file
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
    bool is_regular = zfile_is_regular (file);
    bool is_writable = zfile_is_writeable (file);
    zfile_destroy (&file);
    if (is_dir) {
        log_error ("Specified argument '%s' is a directory.", fullpath);
        return -1;
    }
    if (is_regular && !is_writable) {
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
//  Set path to configuration directory
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
        if (!handle)
            break;
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
    printf (" * data: ");

    //  @selftest
    //  Simple create/destroy test    
    data_t *self = data_new ();
    assert (self);
    
    data_destroy (&self);
    assert (self == NULL);
    
    data_destroy (&self);
    assert (self == NULL);
    
    self = data_new ();

    // data_(set)_statefile 
    const char *state_file = data_statefile (self);
    assert (streq (state_file, ""));

    data_set_statefile (self, "/home/kj/work/state_file");
    
    state_file = data_statefile (self);
    assert (streq (state_file, "/home/kj/work/state_file"));

    data_set_statefile (self, "./state_file");
    
    state_file = data_statefile (self);
    assert (streq (state_file, "./state_file"));

    // asset
    zlistx_t *expected = zlistx_new ();
    zlistx_set_destructor (expected, (czmq_destructor *) zstr_free);
    zlistx_set_duplicator (expected, (czmq_duplicator *) strdup);
    zlistx_set_comparator (expected, (czmq_comparator *) strcmp);

    bios_proto_t *asset =  test_asset_new ("ups", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");

    bios_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    data_asset_put (self, &asset);
    zlistx_add_end (expected, (void *) "ups");

    asset =  test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    data_asset_put (self, &asset);
    zlistx_add_end (expected, (void *) "epdu");

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "priority", "%s", "2");

    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    bios_proto_ext_insert (asset, "description" , "%s",  "UPS1 9PX 5000i");
    bios_proto_ext_insert (asset, "device.location" , "%s",  "New IT Power LAB");
    bios_proto_ext_insert (asset, "location_u_pos" , "%s",  "1");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "3");
    bios_proto_ext_insert (asset, "device.contact" , "%s",  "Gabriel Szabo");
    bios_proto_ext_insert (asset, "hostname.1" , "%s",  "ups33.roz53.lab.etn.com");
    bios_proto_ext_insert (asset, "battery.type" , "%s",  "PbAc");
    bios_proto_ext_insert (asset, "phases.output" , "%s",  "1");
    bios_proto_ext_insert (asset,  "device.type" , "%s",  "ups");
    bios_proto_ext_insert (asset, "business_critical" , "%s",  "yes");
    bios_proto_ext_insert (asset, "status.outlet.2" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.1" , "%s",  "on");
    bios_proto_ext_insert (asset, "serial_no" , "%s",  "G202D51129");
    bios_proto_ext_insert (asset, "ups.serial" , "%s",  "G202D51129");
    bios_proto_ext_insert (asset, "installation_date" , "%s",  "2015-01-05");
    bios_proto_ext_insert (asset, "model" , "%s",  "Eaton 9PX");
    bios_proto_ext_insert (asset, "phases.input" , "%s",  "1");
    bios_proto_ext_insert (asset, "ip.1" , "%s",  "10.130.53.33");
    bios_proto_ext_insert (asset, "ups.alarm" , "%s",  "Automatic bypass mode!");
    bios_proto_ext_insert (asset, "manufacturer" , "%s",  "EATON");
    data_asset_put (self, &asset);
    zlistx_add_end (expected, (void *) "ROZ.UPS33");

    asset =  test_asset_new ("ROZ.ePDU14", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "priority", "%s", "1"); 
    data_asset_put (self, &asset);
    zlistx_add_end (expected, (void *) "ROZ.ePDU14");

    {
        zlistx_t *received = data_asset_names (self);
        assert (received);

        int rv = test_zlistx_compare (expected, &received);
        assert (rv == 0);

        /*
        assert (nut_asset_ip (self, "non-existing-asset") == NULL);
        assert (nut_asset_daisychain (self, "non-existing-asset") == NULL);

        assert (streq (nut_asset_ip (self, "ups"), ""));
        assert (streq (nut_asset_daisychain (self, "ups"), ""));

        assert (streq (nut_asset_ip (self, "epdu"), ""));
        assert (streq (nut_asset_daisychain (self, "epdu"), ""));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), ""));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "3"));

        assert (streq (nut_asset_ip (self, "ROZ.ePDU14"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.ePDU14"), "2"));
        */
    }

    zlistx_destroy (&expected);
    data_destroy (&self);
    //  @end
    printf ("OK\n");
}

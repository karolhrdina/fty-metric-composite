/*  =========================================================================
    c_metric_conf - structure that represents current start of
            composite-metrics-configurator

    Copyright (C) 2014 - 2016 Eaton

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
    c_metric_conf - structure that represents current start of
            composite-metrics-configurator
@discuss
@end
*/

#include "composite_metrics_classes.h"

/*
 * \brief Destroy the configurator entity
 */
void
c_metric_conf_destroy (c_metric_conf_t **self_p)
{
    if (*self_p)
    {
        c_metric_conf_t *self = *self_p;
        // free structure items
        zstr_free (&self->name);
        data_destroy (&self->asset_data);
        mlm_client_destroy (&self->client);
        zstr_free (&self->statefile_name);
        zstr_free (&self->configuration_dir);
        // free structure itself
        free (self);
        *self_p = NULL;
    }
}

c_metric_conf_t*
c_metric_conf_new (const char* name)
{
    assert (name);
    c_metric_conf_t *self = (c_metric_conf_t*) zmalloc (sizeof (c_metric_conf_t));
    if ( self ) {
        self->name = strdup (name);
        if ( self->name )
            self->asset_data = data_new ();
        if ( self->asset_data )
            self->client = mlm_client_new ();
        if ( self->client )
            self->statefile_name = strdup ("");
        if ( self->statefile_name )
            self->configuration_dir = strdup ("");
        if ( self->configuration_dir ) {
            self->verbose = false;
            self->is_propagation_needed = true;
        }
        else
            c_metric_conf_destroy (&self);
    }
    return self;
}


//  --------------------------------------------------------------------------
//  Get state file fullpath or empty string if not set

const char *
c_metric_conf_statefile (c_metric_conf_t *self)
{
    assert (self);
    return self->statefile_name;
}

//  --------------------------------------------------------------------------
//  Set state file fullpath
//  0 - success, -1 - error

int
c_metric_conf_set_statefile (c_metric_conf_t *self, const char *fullpath)
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
    zfile_destroy (&file);
    zstr_free (&self->statefile_name);
    self->statefile_name = strdup (fullpath);
    return 0;
}

//  --------------------------------------------------------------------------
//  Get path to configuration directory

const char *
c_metric_conf_cfgdir (c_metric_conf_t *self)
{
    assert (self);
    return self->configuration_dir;
}

//  --------------------------------------------------------------------------
//  Set up the physical propagation of sensors
//   true -> do propagate sensors in physical topology
//   false -> do NOT propagate sensors in physical topology
void
c_metric_conf_set_proparation (c_metric_conf_t *self, bool is_propagation_needed)
{
    assert (self);
    self->is_propagation_needed = is_propagation_needed;
}

//  --------------------------------------------------------------------------
//  Set configuration directory path
//  Directory MUST exist! If directory doesn't exist -> error
//  0 - success, -1 - error

int
c_metric_conf_set_cfgdir (c_metric_conf_t *self, const char *path)
{
    assert (self);
    assert (path);
    zdir_t *dir = zdir_new (path, "-");
    if (!dir) {
        log_error ("zdir_new ('%s', \"-\") failed.", path);
        return -1;
    }
    zdir_destroy (&dir);
    zstr_free (&self->configuration_dir);
    self->configuration_dir = strdup (path);
    log_debug ("Configuration dir is set: '%s'", path);
    return 0;
}

void
c_metric_conf_test (bool verbose)
{
    if ( verbose )
        log_set_level (LOG_DEBUG);

    printf (" * c_metric_conf: \n");
    //  @selftest
    //  =================================================================
    if ( verbose )
        log_debug ("Test1: Simple create/destroy test");
    c_metric_conf_t *self = c_metric_conf_new ("myname");
    assert (self);

    c_metric_conf_destroy (&self);
    assert (self == NULL);

    c_metric_conf_destroy (&self);
    assert (self == NULL);

    self = c_metric_conf_new ("myname");

    //  =================================================================
    if ( verbose )
        log_debug ("Test2: statefile set/get test");

    const char *state_file = c_metric_conf_statefile (self);
    assert (streq (state_file, ""));

    int rv = c_metric_conf_set_statefile (self, "./state_file");
    assert (rv == 0);
    state_file = c_metric_conf_statefile (self);
    assert (streq (state_file, "./state_file"));

    rv = c_metric_conf_set_statefile (self, "/tmp/composite-metrics/state_file");
    assert (rv == 0);
    state_file = c_metric_conf_statefile (self);
    assert (streq (state_file, "/tmp/composite-metrics/state_file"));

    rv = c_metric_conf_set_statefile (self, "./test_dir/state_file");
    assert (rv == 0);
    state_file = c_metric_conf_statefile (self);
    assert (streq (state_file, "./test_dir/state_file"));

    // directory
    rv = c_metric_conf_set_statefile (self, "/lib");
    assert (rv == -1);
    state_file = c_metric_conf_statefile (self);
    assert (streq (state_file, "./test_dir/state_file"));

    //  =================================================================
    if ( verbose )
        log_debug ("Test3: cfgdir set/get test");
    {
    const char *cfgdir = c_metric_conf_cfgdir (self);
    assert (streq (cfgdir, ""));

    int rv = c_metric_conf_set_cfgdir (self, "/tmp");
    assert (rv == 0);
    cfgdir = c_metric_conf_cfgdir (self);
    assert (streq (cfgdir, "/tmp"));

    // non-writable directory
    rv = c_metric_conf_set_cfgdir (self, "/root");
    assert (rv == -1);
    cfgdir = c_metric_conf_cfgdir (self);
    assert (streq (cfgdir, "/tmp"));
    }

    c_metric_conf_destroy (&self);
    //  @end
    printf (" * c_metric_conf: OK\n");
}

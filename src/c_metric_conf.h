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

#ifndef C_METRIC_CONF_H_INCLUDED
#define C_METRIC_CONF_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

// It is a configurator entiry entity
// TODO: Temporary it is here
struct _c_metric_conf_t {
    bool verbose;           // is server verbose or not
    char *name;             // server name
    data_t *asset_data;     // asset data
    mlm_client_t *client;   // malamute client
    char *statefile_name;   // state file name
    char *configuration_dir;// directory, where all configuration file would be stored
};

typedef struct _c_metric_conf_t c_metric_conf_t;

//  @interface
//  Create a new empty configuration
COMPOSITE_METRICS_EXPORT c_metric_conf_t *
    c_metric_conf_new (const char *name);

//  Get state file fullpath or empty string if not set
COMPOSITE_METRICS_EXPORT const char *
    c_metric_conf_statefile (c_metric_conf_t *self);

//  Set state file fullpath
//  0 - success, -1 - error
COMPOSITE_METRICS_EXPORT int
    c_metric_conf_set_statefile (c_metric_conf_t *self, const char *fullpath);

//  Get path to confuration directory
COMPOSITE_METRICS_EXPORT const char *
    c_metric_conf_cfgdir (c_metric_conf_t *self);

//  Set configuration directory path
//  Directory MUST exist! If directory doesn't exist -> error
//  0 - success, -1 - error
COMPOSITE_METRICS_EXPORT int
    c_metric_conf_set_cfgdir (c_metric_conf_t *self, const char *path);

//  Destroy the c_metric_conf
COMPOSITE_METRICS_EXPORT void
    c_metric_conf_destroy (c_metric_conf_t **self_p);

//  Self test of this class
COMPOSITE_METRICS_EXPORT void
    c_metric_conf_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif

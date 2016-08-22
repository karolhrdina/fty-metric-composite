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

#ifndef DATA_H_INCLUDED
#define DATA_H_INCLUDED

#include <set>
#include <string>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _data_t data_t;

//  @interface
//  Create a new data
COMPOSITE_METRICS_EXPORT data_t *
    data_new (void);

//  Store asset, takes ownership of the message
COMPOSITE_METRICS_EXPORT void
    data_asset_store (data_t *self, bios_proto_t **message_p);

//  According known information about assets, decide, where sensors logically belong to
COMPOSITE_METRICS_EXPORT void
    data_reassign_sensors (data_t *self);

//  Before using this functionality, sensors should be assigned to the right positions
//  by calling 'data_reassign_sensors' function.
//  Get list of sensors assigned to the asset
//  You can limit the list of sensors returned to a certain 'sensor_function',
//  NULL returns all sensors.
//  Returns NULL when for 'asset_name' T&H sensors are not known or asset_name is not known at all
//  or in case of memory issues
//  The caller is responsible for destroying the return value when finished with it
COMPOSITE_METRICS_EXPORT zlistx_t *
    data_get_assigned_sensors (
        data_t *self,
        const char *asset_name,
        const char *sensor_function);

//  Returns 'true' if some of recently added asset requires the reconfiguration
//                 or if reconfiguration was done in 'inconsistent' state
//                 and we MUST reconfigure one more time
COMPOSITE_METRICS_EXPORT bool
    data_is_reconfig_needed (data_t *self);

//  Update list of metrics produced by composite_metrics
COMPOSITE_METRICS_EXPORT void
    data_set_produced_metrics (data_t *self,const std::set <std::string> &metrics);

//  Get list of metrics produced by composite_metrics
COMPOSITE_METRICS_EXPORT std::set <std::string>
    data_get_produced_metrics (data_t *self);

//  Get asset names
//  The caller is responsible for destroying the return value when finished with it
COMPOSITE_METRICS_EXPORT zlistx_t *
    data_asset_names (data_t *self);

//  Get information for any given asset name if it is known or NULL otherwise
//  Ownership is NOT transferred
COMPOSITE_METRICS_EXPORT bios_proto_t *
    data_asset (data_t *self, const char *name);

//  Save data to disk
//  0 - success, -1 - error
COMPOSITE_METRICS_EXPORT int
    data_save (data_t *self, const char *filename);

//  Load nut from disk
//  0 - success, -1 - error
COMPOSITE_METRICS_EXPORT data_t *
    data_load (const char *filename);

//  Destroy the data
COMPOSITE_METRICS_EXPORT void
    data_destroy (data_t **self_p);

//  Self test of this class
COMPOSITE_METRICS_EXPORT void
    data_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif

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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _data_t data_t;

//  @interface
//  Create a new data
COMPOSITE_METRICS_EXPORT data_t *
    data_new (void);

//  Store asset
COMPOSITE_METRICS_EXPORT void
    data_asset_put (data_t *self, bios_proto_t **message_p);

//  Get asset names
COMPOSITE_METRICS_EXPORT zlistx_t *
    data_asset_names (data_t *self);

//  Get asset of specified name or NULL. Ownership is NOT transferred
COMPOSITE_METRICS_EXPORT bios_proto_t *
    data_asset (data_t *self, const char *name);

//  Get state file fullpath or empty string if not set
COMPOSITE_METRICS_EXPORT const char *
    data_statefile (data_t *self);

//  Set state file fullpath
//  0 - success, -1 - error
COMPOSITE_METRICS_EXPORT int 
    data_set_statefile (data_t *self, const char *fullpath);

//  Get path to configuration directory
COMPOSITE_METRICS_EXPORT const char *
    data_cfgdir (data_t *self);

//  Set configuration directory path
//  0 - success, -1 - error 
COMPOSITE_METRICS_EXPORT int
    data_set_cfgdir (data_t *self, const char *path);

//  Save nut to disk
//  0 - success, -1 - error
COMPOSITE_METRICS_EXPORT int
    data_save (data_t *self);

//  Load nut from disk
//  0 - success, -1 - error
COMPOSITE_METRICS_EXPORT int
    data_load (data_t *self);

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

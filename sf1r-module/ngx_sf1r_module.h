/* 
 * File:   ngx_sf1r_module.h
 * Author: Paolo D'Apice
 *
 * Created on January 17, 2012, 10:44 AM
 */

#ifndef NGX_SF1R_MODULE_H
#define	NGX_SF1R_MODULE_H

extern "C" {
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}


// Forward declaration
namespace izenelib { namespace net { namespace sf1r {
class Sf1Driver;
}}}


/// Module declaration.
extern ngx_module_t ngx_sf1r_module;


/// Location configuration structure.
typedef struct {
    ngx_str_t address;
    ngx_uint_t port;
    ngx_flag_t enabled;
    izenelib::net::sf1r::Sf1Driver* driver;
} ngx_sf1r_loc_conf_t;


#endif	/* NGX_SF1R_MODULE_H */

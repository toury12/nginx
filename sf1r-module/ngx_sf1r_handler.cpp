/* 
 * File:   ngx_sf1r_handler.cpp
 * Author: Paolo D'Apice
 * 
 * Created on January 17, 2012, 11:23 AM
 */

extern "C" {
#include "ngx_sf1r_ddebug.h"
#include "ngx_sf1r_handler.h"
#include "ngx_sf1r_module.h"
#include "ngx_sf1r_utils.h"
}
#include <net/sf1r/Sf1Driver.hpp>
#include <string>

using izenelib::net::sf1r::ClientError;
using izenelib::net::sf1r::ServerError;
using izenelib::net::sf1r::Sf1Driver;
using std::string;


/// Callback called after getting the request body.
static void ngx_sf1r_request_body_handler(ngx_http_request_t*);

static ngx_int_t ngx_sf1r_send_response(ngx_http_request_t*, ngx_uint_t, string&);
static ngx_flag_t ngx_sf1r_check_request_body(ngx_http_request_t*);


ngx_int_t
ngx_sf1r_handler(ngx_http_request_t* r) {
    // response to 'GET' and 'POST' requests only
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_POST))) {
        ddebug("HTTP method not allowed, discarding");
        return NGX_HTTP_NOT_ALLOWED;
    }
    
    // discard header only requests
    if (r->header_only) {
        ddebug("header only request, discarding");
        return NGX_HTTP_BAD_REQUEST;
    }
    
    // we have the request header but no the body!!!
    // http://forum.nginx.org/read.php?2,31312,173389
    ngx_int_t rc = ngx_http_read_client_request_body(r, ngx_sf1r_request_body_handler);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ddebug("got special response: %d", (int) rc);
        return rc;
    }
    
    return NGX_DONE;
}


static void 
ngx_sf1r_request_body_handler(ngx_http_request_t* r) {
    if (ngx_sf1r_check_request_body(r) != NGX_OK) {
        ddebug("no body in request, discarding");
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return;
    }
    
    /* do actual processing */
    
    ddebug("reading request body ...");
    
    ngx_chain_t* cl = r->request_body->bufs;   
    ngx_buf_t* buf = cl->buf;
    
    string* body;
    
    if (cl->next == NULL) {
        ddebug("read from this buffer");
        
        size_t len = buf->last - buf->pos;
        body = new string(rcast(char*, buf->pos), len);
    } else {
        ddebug("read from next buffer");

        ngx_buf_t* next = cl->next->buf;
        size_t len = (buf->last - buf->pos) + (next->last - next->pos);
        
        u_char* p = scast(u_char*, ngx_pnalloc(r->pool, len));
        if (p == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate request body buffer.");
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }

        body = new string(rcast(char*, p), len);
        
        p = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
        ngx_memcpy(p, next->pos, next->last - next->pos);
    }
    
    ddebug("body: [%s]", body->c_str());
        
    string uri((char*)r->uri.data, r->uri.len);
    ddebug("uri: [%s]", uri.c_str());
    
    string tokens = ""; // TODO
    ddebug("tokens: [%s]", tokens.c_str());
    
    ngx_sf1r_loc_conf_t* conf = scast(ngx_sf1r_loc_conf_t*, ngx_http_get_module_loc_conf(r, ngx_sf1r_module));
        
    try {
        ddebug("sending request and getting response to SF1 ...");
        Sf1Driver* driver = scast(Sf1Driver*, conf->driver);
        string response = driver->call(uri, tokens, *body);
        
        ddebug("got response: %s", response.c_str());
        
        /* send response */
        ngx_int_t rc = ngx_sf1r_send_response(r, NGX_HTTP_OK, response);
        ngx_http_finalize_request(r, rc);
    } catch (ClientError& e) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, e.what());
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return;
    } catch (ServerError& e) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, e.what());
        ngx_http_finalize_request(r, NGX_HTTP_BAD_GATEWAY);
        return;
    } catch (std::exception& e) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, e.what());
        ngx_http_finalize_request(r, NGX_HTTP_SERVICE_UNAVAILABLE);
        return;
    }
}
    

static ngx_int_t 
ngx_sf1r_send_response(ngx_http_request_t* r, ngx_uint_t status, string& body) {
    ddebug("sending response ...");
    
    /* set response header */
    
    // set the status line
    r->headers_out.status = status;
    
    r->headers_out.content_length_n = body.length();
    r->headers_out.content_type_len = sizeof(APPLICATION_JSON) - 1;
    r->headers_out.content_type.len = sizeof(APPLICATION_JSON) - 1;
    r->headers_out.content_type.data = (u_char*) APPLICATION_JSON;
    ddebug("set response header: %zu - %s", r->headers_out.status = status, r->headers_out.content_type.data);
    
    /* set response body */
    
    // allocate a buffer
    ngx_buf_t* buffer = scast(ngx_buf_t*, ngx_pcalloc(r->pool, sizeof(ngx_buf_t)));
    if (buffer == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate response buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    // adjust the pointers of the buffer
    buffer->pos = (u_char*) body.c_str();
    buffer->last = (u_char*) (body.c_str() + body.length());
    buffer->memory = 1;
    buffer->last_buf = 1;

    // attach this buffer to the buffer chain
    ngx_chain_t out;
    out.buf = buffer;
    out.next = NULL;
    ddebug("response buffer set");
    
    /* send the header */
    ngx_int_t rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
    
    return ngx_http_output_filter(r, &out);
}


static ngx_flag_t 
ngx_sf1r_check_request_body(ngx_http_request_t* r) {
    ddebug("request body content length: %d", (int) r->headers_in.content_length_n);
    if (r->request_body == NULL) {
        ddebug("request body is NULL");
        return NGX_ERROR;
    }
    if (r->request_body->bufs == NULL) {
        ddebug("request body buffer is NULL");
        return NGX_ERROR;
    }
    if (r->request_body->temp_file) {
        ddebug("request body temp file is NOT NULL");
        return NGX_ERROR;
    }
    return NGX_OK;
}
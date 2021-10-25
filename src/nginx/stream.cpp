#include "stream.h"

#include "header.h"
#include "util.h"

namespace weserv {
namespace nginx {

ngx_str_t application_json = ngx_string("application/json");

// 1 year by default.
// See: https://github.com/weserv/images/issues/186
const time_t MAX_AGE_DEFAULT = 60 * 60 * 24 * 365;

int64_t NgxSource::read(void *data, size_t length) {
    size_t bytes_read = 0;

    for (/* void */; in_; in_ = in_->next) {
        ngx_buf_t *b = in_->buf;
        size_t size = ngx_min((size_t)(b->last - b->pos), length);

        data = ngx_cpymem(data, b->pos, size);
        b->pos += size;
        bytes_read += size;

        length -= size;

        if (length == 0 || b->last_buf) {
            break;
        }
    }

    return bytes_read;
}

void NgxTarget::setup(const std::string &extension) {
    extension_ = extension;
}

int64_t NgxTarget::write(const void *data, size_t length) {
    ngx_buf_t *b = ngx_create_temp_buf(r_->pool, length);
    if (b == nullptr) {
        return -1;
    }

    b->last = ngx_cpymem(b->last, data, length);
    b->last_buf = 1;

    ngx_chain_t *cl = ngx_alloc_chain_link(r_->pool);
    if (cl == nullptr) {
        return -1;
    }

    cl->buf = b;
    cl->next = nullptr;

    *ll_ = cl;
    ll_ = &cl->next;

    content_length_ += length;

    return length;
}

void NgxTarget::finish() {
    ngx_str_t mime_type = extension_to_mime_type(extension_);

    r_->headers_out.status = NGX_HTTP_OK;
    r_->headers_out.content_type = mime_type;
    r_->headers_out.content_type_len = mime_type.len;
    r_->headers_out.content_type_lowcase = nullptr;
    r_->headers_out.content_length_n = content_length_;

    if (r_->headers_out.content_length) {
        r_->headers_out.content_length->hash = 0;
    }

    r_->headers_out.content_length = nullptr;

    // Only set the Content-Disposition header on images
    if (!is_base64_needed(r_) &&
        !ngx_string_equal(mime_type, application_json)) {
        (void)set_content_disposition_header(r_, extension_);
    }

    // Only set the Link header if there's an upstream context available
    if (upstream_ctx_ != nullptr) {
        (void)set_link_header(r_, upstream_ctx_->request->url());
    }

    time_t max_age = MAX_AGE_DEFAULT;

    ngx_str_t max_age_str;
    if (ngx_http_arg(r_, (u_char *)"maxage", 6, &max_age_str) == NGX_OK) {
        max_age = parse_max_age(max_age_str);
        if (max_age == static_cast<time_t>(NGX_ERROR)) {
            max_age = MAX_AGE_DEFAULT;
        }
    }

    // Only set Cache-Control and Expires headers on non-error responses
    (void)set_expires_header(r_, max_age);

    *ll_ = nullptr;
}

}  // namespace nginx
}  // namespace weserv

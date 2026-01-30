/**
 * Escape string for JSON
 */
static const char* ws_json_escape_string(const char *input, apr_pool_t *pool)
{
    char *output;
    const char *p;
    char *q;
    apr_size_t len = 0;
    
    if (!input) return "";
    
    /* Calculate required length */
    for (p = input; *p; p++) {
        if (*p == '\"' || *p == '\\' || *p == '\b' || *p == '\f' || 
            *p == '\n' || *p == '\r' || *p == '\t') {
            len += 2;
        } else if ((unsigned char)*p < 32) {
            len += 6; /* \uXXXX */
        } else {
            len++;
        }
    }
    
    output = apr_palloc(pool, len + 1);
    q = output;
    
    for (p = input; *p; p++) {
        switch (*p) {
            case '\"': *q++ = '\\'; *q++ = '\"'; break;
            case '\\': *q++ = '\\'; *q++ = '\\'; break;
            case '\b': *q++ = '\\'; *q++ = 'b'; break;
            case '\f': *q++ = '\\'; *q++ = 'f'; break;
            case '\n': *q++ = '\\'; *q++ = 'n'; break;
            case '\r': *q++ = '\\'; *q++ = 'r'; break;
            case '\t': *q++ = '\\'; *q++ = 't'; break;
            default:
                if ((unsigned char)*p < 32) {
                    q += apr_snprintf(q, 7, "\\u%04x", (unsigned char)*p);
                } else {
                    *q++ = *p;
                }
                break;
        }
    }
    *q = '\0';
    
    return output;
}

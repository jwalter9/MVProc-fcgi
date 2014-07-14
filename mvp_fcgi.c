/*
   Copyright 2014 Jeff Walter

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "mvproc.h"	

mvp_pool *get_new_pool(){
    mvp_pool *p = (mvp_pool *)malloc(sizeof(mvp_pool));
    if(p == NULL) return NULL;
    p->page = NULL;
    p->curpos = 0;
    p->next = NULL;
    return p;
}

mvp_in_param *get_new_param(mvp_pool *p){
    mvp_in_param *pm = (mvp_in_param *)mvp_alloc(p, sizeof(mvp_in_param));
    if(pm == NULL) return NULL;
    pm->name = NULL;
    pm->val = NULL;
    pm->next = NULL;
    return pm;
}

void *mvp_alloc(mvp_pool *p, size_t s){
    mvp_pool *iter = p;
    if(s > MEM_PAGE_SIZE){
        while(iter->next != NULL) iter = iter->next;
        iter->next = get_new_pool();
        if(iter->next == NULL) return NULL;
        iter->page = malloc(s);
        if(iter->page == NULL) return NULL;
        iter->curpos = s;
        return iter->page;
    };
    while(iter->page && iter->curpos > (MEM_PAGE_SIZE - s))
        iter = iter->next;
    if(iter->next == NULL){
        iter->next = get_new_pool();
        if(iter->next == NULL) return NULL;
    };
    if(iter->page == NULL){
        iter->page = malloc(MEM_PAGE_SIZE + 1);
        if(iter->page == NULL) return NULL;
        iter->curpos = 0;
    };
    iter->curpos += s;
    return iter->page + iter->curpos - s;
}

void mvp_free(mvp_pool *p){
    mvp_pool *iter = p;
    while(iter != NULL){
        if(iter->page) free(iter->page);
        iter = iter->next;
        free(p);
        p = iter;
    };
}

void perror_f(const char *fmt, const char *ins){
    char err[strlen(fmt) + ins? strlen(ins) : 6 + 1];
    sprintf(err, fmt, ins? ins : "(null)");
    perror(err);
}

mvproc_table *error_out(const mvproc_config *cfg, mvp_pool *p, const char *err){
    mvproc_table *ret = (mvproc_table *)mvp_alloc(p, sizeof(mvproc_table));
    if(ret == NULL) OUT_OF_MEMORY;
    ret->next = NULL;
    ret->name = (char *)mvp_alloc(p, 7);
    if(ret->name == NULL) OUT_OF_MEMORY;
    strcpy(ret->name, "status");
    ret->num_rows = 1;
    ret->num_fields = 1;
    ret->cols = (db_col_t *)mvp_alloc(p, sizeof(db_col_t));
    if(ret->cols == NULL) OUT_OF_MEMORY;
    ret->cols[0].name = (char *)mvp_alloc(p, 7);
    if(ret->cols[0].name == NULL) OUT_OF_MEMORY;
    strcpy(ret->cols[0].name, "error");
    ret->cols[0].vals = (db_val_t *)mvp_alloc(p, sizeof(db_val_t));
    if(ret->cols[0].vals == NULL) OUT_OF_MEMORY;
    ret->cols[0].vals[0].size = strlen(err);
    ret->cols[0].vals[0].val = 
        (char *)mvp_alloc(p, ret->cols[0].vals[0].size + 1);
    if(ret->cols[0].vals[0].val == NULL) OUT_OF_MEMORY;
    strcpy(ret->cols[0].vals[0].val, err);
    ret->cols[0].vals[0].type = _BLOB;

    if(cfg->template_dir != NULL && cfg->error_tpl != NULL){
        ret->next =
        (mvproc_table *)mvp_alloc(p, sizeof(mvproc_table));
        if(ret->next == NULL) OUT_OF_MEMORY;
        ret->next->next = NULL;
        ret->next->name = (char *)mvp_alloc(p, 9);
        if(ret->next->name == NULL) OUT_OF_MEMORY;
        strcpy(ret->next->name, "PROC_OUT");
        ret->next->num_rows = 1;
        ret->next->num_fields = 1;
        ret->next->cols = (db_col_t *)mvp_alloc(p, sizeof(db_col_t));
        if(ret->next->cols == NULL) OUT_OF_MEMORY;
        ret->next->cols[0].name = 
            (char *)mvp_alloc(p, 13);
        if(ret->next->cols[0].name == NULL) OUT_OF_MEMORY;
        strcpy(ret->next->cols[0].name, "mvp_template");
        ret->next->cols[0].vals = 
            (db_val_t *)mvp_alloc(p, sizeof(db_val_t));
        if(ret->next->cols[0].vals == NULL) OUT_OF_MEMORY;
        ret->next->cols[0].vals[0].size = strlen(cfg->error_tpl);
        ret->next->cols[0].vals[0].val = 
            (char *)mvp_alloc(p, ret->next->cols[0].vals[0].size + 1);
        if(ret->next->cols[0].vals[0].val == NULL) OUT_OF_MEMORY;
        strcpy(ret->next->cols[0].vals[0].val, cfg->error_tpl);
        ret->next->cols[0].vals[0].type = _BLOB;
    };
    return ret;
}

char *hash_cookie(mvp_pool *p, const char *str, size_t length) {
    char *out = (char *)mvp_alloc(p, 33);
    if(out == NULL) return NULL;
    int n;
    MD5_CTX c;
    unsigned char digest[16];
    MD5_Init(&c);
    MD5_Update(&c, str, length);
    MD5_Final(digest, &c);
    for (n = 0; n < 16; ++n)
        snprintf(&out[n*2], 32, "%02x", (unsigned int)digest[n]);
    return out;
}

char *scrub(mvp_pool *p, const char *str, size_t length){
    char *uncode = (char *)mvp_alloc(p, length + 1);
    if(uncode == NULL) return NULL;
    size_t i, n = 0;
    size_t nlen;
    char hex[2];
    char *unused;
    /* first unencode the urlencoding */
    for(i = 0; i < length; i++){
        switch(str[i]){
        case '+':
            uncode[n++] = ' ';
            break;
        case '%':
            if(length - i > 1){
                hex[0] = str[i + 1];
                hex[1] = str[i + 2];
                uncode[n++] = strtol(hex, &unused, 16);
                i += 2;
                break;
            };
        default:
            uncode[n++] = str[i];
        };
    };
    uncode[n] = '\0';
    /* now sql injection protection --
       must escape single quotes and escape characters --
       not using mysql_real_escape_string because it's
       slower and not necessary against sql injection (per documentation) */
    nlen = strlen(uncode);
    n = 0;
    char *esc = (char *)mvp_alloc(p, strlen(uncode) * 2 + 1);
    if(esc == NULL) return NULL;
    for(i = 0; i < nlen; i++){
        switch(uncode[i]){
        case '\\':
            esc[n++] = '\\';
            esc[n++] = '\\';
            break;
        case '\'':
            esc[n++] = '\\';
            esc[n++] = '\'';
            break;
        default:
            esc[n++] = uncode[i];
        };
    };
    esc[n] = '\0';
    return esc;
}

multipart_headers *get_part_headers(mvp_req_rec *r, char *input, 
                                    size_t *hdr_size){
    size_t len;
    char *elem = strstr(input, "\r\n\r\n");
    char *end = elem;
    if(elem == NULL) return NULL; // Header never ends??
    elem += 4;
    *hdr_size = elem - input;
    
    multipart_headers *hdrs = 
        (multipart_headers *)mvp_alloc(r->pool, sizeof(multipart_headers));
    if(hdrs == NULL) return NULL;
    hdrs->disposition = NULL; // don't
    hdrs->type = NULL;        // you
    hdrs->encoding = NULL;    // just
    hdrs->boundary = NULL;    // love
    hdrs->name = NULL;        // malloc?
    hdrs->filename = NULL;    // The things we do for speed (elsewhere I guess)
    hdrs->size = 0;

    elem = strstr(input, "Content-Disposition:");
    if(elem && elem < end){
        len = strspn(&elem[20], " \t");
        elem += 20 + len;
        len = strcspn(elem, "; \t\r\n");
        hdrs->disposition = (char *)mvp_alloc(r->pool, len + 1);
        if(hdrs->disposition == NULL) return NULL;
        strncpy(hdrs->disposition, elem, len);
        hdrs->disposition[len] = '\0';
    };
    
    elem = strstr(input, " name=");
    if(elem && elem < end){
        elem += 6;
        if(elem[0] == '"'){
            elem++;
            len = strcspn(elem, "\"");
        }else{
            len = strcspn(elem, "; \t\r\n");
        };
        hdrs->name = (char *)mvp_alloc(r->pool, len + 1);
        if(hdrs->name == NULL) return NULL;
        strncpy(hdrs->name, elem, len);
        hdrs->name[len] = '\0';
    };

    elem = strstr(input, "filename=");
    if(elem && elem < end){
        elem += 9;
        if(elem[0] == '"'){
            elem++;
            len = strcspn(elem, "\"");
        }else{
            len = strcspn(elem, "; \t\r\n");
        };
        hdrs->filename = (char *)mvp_alloc(r->pool, len + 1);
        if(hdrs->filename == NULL) return NULL;
        strncpy(hdrs->filename, elem, len);
        hdrs->filename[len] = '\0';
    };

    elem = strstr(input, "size=");
    if(elem && elem < end){
        elem += 5;
        if(elem[0] == '"'){
            elem++;
            len = strcspn(elem, "\"");
        }else{
            len = strcspn(elem, "; \t\r\n");
        };
        hdrs->size = atol(elem);
    };

    elem = strstr(input, "Content-Type:");
    if(elem && elem < end){
        len = strspn(&elem[13], " \t");
        elem += 13 + len;
        len = strcspn(elem, "; \t\r\n");
        hdrs->type = (char *)mvp_alloc(r->pool, len + 1);
        if(hdrs->type == NULL) return NULL;
        strncpy(hdrs->type, elem, len);
        hdrs->type[len] = '\0';
    };

    elem = strstr(input, "boundary=");
    if(elem && elem < end){
        elem += 9;
        if(elem[0] == '"'){
            elem++;
            len = strcspn(elem, "\"");
        }else{
            len = strcspn(elem, "; \t\r\n");
        };
        hdrs->boundary = (char *)mvp_alloc(r->pool, len + 1);
        if(hdrs->boundary == NULL) return NULL;
        strncpy(hdrs->boundary, elem, len);
        hdrs->boundary[len] = '\0';
    };

    elem = strstr(input, "Content-Transfer-Encoding:");
    if(elem && elem < end){
        len = strspn(&elem[26], " \t");
        elem += 26 + len;
        len = strcspn(elem, "; \t\r\n");
        hdrs->encoding = (char *)mvp_alloc(r->pool, len + 1);
        if(hdrs->encoding == NULL) return NULL;
        strncpy(hdrs->encoding, elem, len);
        hdrs->encoding[len] = '\0';
    };
        
    return hdrs;
}

const char *recursive_multi_parse(const mvproc_config *cfg, mvp_req_rec *r, 
                                  mvp_in_param *parm, multipart_headers *hdrs,
                                  char *input, char *last){
    multipart_headers *phdr;
    size_t size;
    const char *err;
    FILE *upl;
    int td;
    char *pt, *end, *val;
    char *inpt = input;
    while((pt = strstr(inpt, hdrs->boundary))){
        if(pt == NULL) return "Malformed multipart - no boundary\n";
        pt += strlen(hdrs->boundary);
        if(pt[0] == '-' && pt[1] == '-') return NULL; // done.
        phdr = get_part_headers(r, pt, &size);
        if(phdr == NULL) return "Failed to retieve part headers\n";
        pt += size;

        if(phdr->filename == NULL || strlen(phdr->filename) == 0){

            if(phdr->boundary){
                err = recursive_multi_parse(cfg, r, parm, phdr, pt, last);
                if(err) return err;

                end = strstr(pt, hdrs->boundary);
                if(end == NULL){ /* prolly a binary file with some \0s */
                    for(val = pt; val < last; val++){
                        if(*val == '\r'){
                            end = strstr(val, hdrs->boundary);
                            if(end) break;
                        };
                    };
                };
                if(end == NULL) return "Failed to find ending boundary\n";
            }else if(phdr->name){
                end = strstr(pt, hdrs->boundary);
                if(end == NULL) return "Non-text entry in non-file part\n";
                if(end - pt > 4){ /* the 4: "\r\n--" */
                    end -= 4;
                    val = (char *)mvp_alloc(r->pool, end - pt + 1);
                    if(val == NULL)
                        return "Failed to alloc for param value multipart\n";
                    strncpy(val, pt, end - pt);
                    val[end - pt] = '\0';
                    parm->name = phdr->name;
                    parm->val = scrub(r->pool, val, strlen(val));
                    parm->next = get_new_param(r->pool);
                    if(parm->next == NULL) 
                        return "Failed to alloc new param in multi-part\n";
                    parm = parm->next;
                };
            };

        }else{ /* It's a file, and thanks to all the lazy browsers out there 
                  there is no other way of reliably determining this */

            end = strstr(pt, hdrs->boundary);
            if(end == NULL){ /* prolly a binary file with some \0s */
                for(val = pt; val < last; val++){
                    if(*val == '\r'){
                        end = strstr(val, hdrs->boundary);
                        if(end) break;
                    };
                };
            };
            if(end == NULL) return "Failed to find ending boundary\n";
            size = end - pt - 4;

            char *ext = strrchr(phdr->filename, '.');
            val = (char *)mvp_alloc(r->pool, 
                strlen(cfg->upload_dir) + (ext ? strlen(ext) : 0) + 11);
            if(val == NULL) return "Failed to alloc for mkstemp template\n";
            if(ext){
                sprintf(val, "%s/uplXXXXXX%s", cfg->upload_dir, ext);
                td = mkstemps(val, strlen(ext));
            }else{
                sprintf(val, "%s/uplXXXXXX", cfg->upload_dir);
                td = mkstemp(val);
            };
            upl = fdopen(td, "wb");
            if(ferror(upl)) return "Failed to open upload file for writing\n";
            fwrite(pt, size, 1, upl);
            if(ferror(upl)) return "Failed to write upload file\n";
            fclose(upl);

            if(val){
                parm->name = phdr->name;
                parm->val = val;
                parm->next = get_new_param(r->pool);
                if(parm->next == NULL) 
                    return "Failed to alloc new param in multi-part\n";
                parm = parm->next;
            };
        };
        inpt = end;
    };
    return NULL;
}

mvp_req_rec *get_request(const mvproc_config *cfg){
    /* mvp_req_rec gets a non-pool alloc because it contains a pool */
    mvp_req_rec *r = (mvp_req_rec *)calloc(sizeof(mvp_req_rec), 1);
    if(r == NULL){
        perror("Failed to alloc for mvp_req_rec\n");
        return NULL;
    };
    r->pool = get_new_pool();
    if(r->pool == NULL){
        perror("Failed to alloc for request pool\n");
        free(r);
        return NULL;
    };
    size_t endpos;
    char *elem, *attr;
    
    elem = getenv("REQUEST_URI");
    /* And why does CGI leave the QUERY_STRING attached to the uri? */
    endpos = strcspn(elem, "?");
    r->uri = scrub(r->pool, elem, endpos);
    elem = getenv("SERVER_NAME");
    r->server_hostname = scrub(r->pool, elem, strlen(elem));
    elem = getenv("REQUEST_METHOD");
    r->method = scrub(r->pool, elem, strlen(elem));
    elem = getenv("REMOTE_ADDR");
    r->useragent_ip = scrub(r->pool, elem, strlen(elem));
    elem = getenv("HTTP_USER_AGENT");
    r->agent = scrub(r->pool, elem, strlen(elem));

    /* Start session? */
    if(cfg->session == 'Y' || cfg->session == 'y'){
        elem = getenv("HTTP_COOKIE");
        if(elem){
            elem = strstr(elem, "MVPSESSION=");
            if(elem){
                elem += strlen("MVPSESSION=");
                endpos = strspn(elem, "0123456789ABCDEFabcdef");
                r->session = (char *)mvp_alloc(r->pool, endpos + 1);
                strncpy(r->session, elem, endpos);
                r->session[endpos] = '\0';
            };
        };
        if(r->session == NULL || strlen(r->session) < 1){
            elem = (char *)mvp_alloc(r->pool, 65);
            if(r->useragent_ip != NULL)
                sprintf(elem,"%s",r->useragent_ip);
            sprintf(&elem[strlen(elem)],"%d",rand());
            r->session = hash_cookie(r->pool, elem, strlen(elem));
        };
    }else{
        r->session = (char *)mvp_alloc(r->pool, 1);
        if(r->session == NULL){
            perror("Failed to allocate one byte for empty session\n");
            mvp_free(r->pool);
            free(r);
            return NULL;
        };
        r->session[0] = '\0';
    };
    
    mvp_in_param *parm = get_new_param(r->pool);
    if(!parm){
        perror("Failed to allocate for storing input parsing\n");
        mvp_free(r->pool);
        free(r);
        return NULL;
    };
    r->parsed_params = parm;

    /* Query string - parse it even if there's a POST: browsers can do both */
    elem = getenv("QUERY_STRING");
    if(!elem){
        elem = strchr(getenv("REQUEST_URI"), '?');
        if(elem) elem++;
    };
    if(elem)
    while(strlen(elem)){
        endpos = strcspn(elem, "=");
        parm->name = scrub(r->pool, elem, endpos);
        if(!parm->name) return NULL;
        elem += endpos + 1;
        endpos = strcspn(elem, "&");
        parm->val = scrub(r->pool, elem, endpos);
        if(!parm->val) return NULL;
        parm->next = get_new_param(r->pool);
        if(!parm->next){
            perror("Failed to allocate for GET input parsing\n");
            mvp_free(r->pool);
            free(r);
            return NULL;
        };
        parm = parm->next;
        elem += endpos;
        if(elem[0] == '&') elem++;
    };
    if(strcmp(getenv("REQUEST_METHOD"), "GET") == 0 ||
        strcmp(getenv("REQUEST_METHOD"), "HEAD") == 0) return r;
    
    long clen = atol(getenv("CONTENT_LENGTH"));
    if(clen > cfg->max_content_length){
        mvp_free(r->pool);
        free(r);
        return NULL;
    };
    elem = (char *)malloc(clen + 1);
    if(!elem){
        perror("Failed to allocate for POST input parsing\n");
        mvp_free(r->pool);
        free(r);
        return NULL;
    };
    fread(elem, clen, 1, stdin);
    elem[clen] = '\0';

    /* POST CONTENT */
    if(strncmp(getenv("CONTENT_TYPE"), "application/x-www-form-urlencoded",
               strlen("application/x-www-form-urlencoded")) == 0){
        attr = elem;
        while(strlen(attr)){
            endpos = strcspn(attr, "=");
            parm->name = scrub(r->pool, attr, endpos);
            if(!parm->name) break;
            attr += endpos + 1;
            endpos = strcspn(attr, "&");
            parm->val = scrub(r->pool, attr, endpos);
            if(!parm->val) break;
            parm->next = get_new_param(r->pool);
            if(!parm->next){
                perror("Failed to allocate for input parsing\n");
                mvp_free(r->pool);
                free(elem);
                free(r);
                return NULL;
            };
            parm = parm->next;
            attr += endpos;
            if(attr[0] == '&') attr++;
        };
    }else if(strncmp(getenv("CONTENT_TYPE"), "multipart/form-data",
                     strlen("multipart/form-data")) == 0){
        multipart_headers *headers = 
            (multipart_headers *)mvp_alloc(r->pool, sizeof(multipart_headers));
        if(headers == NULL){
            perror("Failed alloc for multipart\n");
            mvp_free(r->pool);
            free(elem);
            free(r);
            return NULL;
        };
        
        /* see what we have in the main headers */
        attr = strstr(getenv("CONTENT_TYPE"), "boundary=");
        if(attr){
            attr += 9;
            if(attr[0] == '"'){
                attr++;
                endpos = strcspn(attr, "\"");
            }else{
                endpos = strcspn(attr, "; \t\r\n");
            };
            headers->boundary = (char *)mvp_alloc(r->pool, endpos + 1);
            if(headers->boundary == NULL){
                perror("Failed alloc for multipart\n");
                mvp_free(r->pool);
                free(elem);
                free(r);
                return NULL;
            };
            strncpy(headers->boundary, attr, endpos);
            headers->boundary[endpos] = '\0';
        }else{
            /* content-type is not correctly populated. try to continue? */
            free(elem);
            return r;
        };
        
        const char *err = 
            recursive_multi_parse(cfg, r, parm, headers, elem, elem + clen);
        if(err){
            perror(err);
            mvp_free(r->pool);
            free(elem);
            free(r);
            return NULL;
        };
    };
    
    free(elem);
    return r;
}

int main(void){
    mvp_pool *configPool = get_new_pool();
    mvproc_config *first = populate_config(configPool);
    if(first == NULL) return 1;
    mvproc_config *cfg;
    
    while(FCGI_Accept() >= 0){
        cfg = first;
        char *srv = getenv("SERVER_NAME");
        while(srv && cfg->next){
            if(strcmp(cfg->server_name, srv) == 0) break;
            else cfg = cfg->next;
        };
        
        mvp_req_rec *r = get_request(cfg);

        if(r == NULL){
            printf("Status: 500\r\n\r\n");
            continue;
        };
        
        int err = 500;
        mvproc_table *tables = getDBResult(cfg, r, &err);
        if(tables == NULL){
            printf("Status: %d\r\n\r\n", err);
            char *rpt = (char *)mvp_alloc(r->pool, 30);
            if(rpt){
                sprintf(rpt, "MVProc FCGI error: %d\n", err);
                perror(rpt);
            };
        }else{
            generate_output(r, cfg, tables);
        };

        mvp_free(r->pool);
        free(r);
    };
    return 0;
}


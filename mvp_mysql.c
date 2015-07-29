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

void fill_proc_struct(mvp_pool *p, const char *pname, const char *paramList, 
                      modmvproc_cache *cache_entry){
    cache_entry->procname = (char *)mvp_alloc(p, strlen(pname) + 1);
    cache_entry->param_list = NULL;
    strcpy(cache_entry->procname, pname);
    db_param_t *param = NULL, *next_param = NULL;
    inout_t inout = IN;
    size_t pos = 0, len = strlen(paramList), i, num = 0;
    pos += strspn(paramList, " \t\r\n\0");
    while(pos < len){
        if(strncmp(&paramList[pos], "IN ", 3) == 0 || 
           strncmp(&paramList[pos], "in ", 3) == 0){
            inout = IN;
            pos += 2;
        }else if(strncmp(&paramList[pos], "INOUT ", 6) == 0 || 
                 strncmp(&paramList[pos], "inout ", 6) == 0){
            inout = INOUT;
            pos += 5;
        }else if(strncmp(&paramList[pos], "OUT ", 4) == 0 || 
                 strncmp(&paramList[pos], "out ", 4) == 0){
            inout = OUT;
            pos += 3;
        }else{
            inout = IN;
        };
        pos += strspn(&paramList[pos], " \t\r\n\0");
        if(pos >= len) break;
        next_param = (db_param_t *)mvp_alloc(p, sizeof(db_param_t));
        i = strcspn(&paramList[pos], " \t\r\n");
        if(pos + i >= len || i < 1) break;
        next_param->name = (char *)mvp_alloc(p, i + 1);
        strncpy(next_param->name, &paramList[pos], i);
        next_param->name[i] = '\0';
        pos += i;
        next_param->in_or_out = inout;
        next_param->next = NULL;
        num++;
        if(param != NULL){
            param->next = next_param;
        }else{
            cache_entry->param_list = next_param;
        };
        param = next_param;
        pos += strcspn(&paramList[pos], ",") + 1;
        if(pos >= len) break;
        pos += strspn(&paramList[pos], " \t\r\n");
        next_param = NULL;
    };
    cache_entry->num_params = num;
}

const char *build_cache(mvp_pool *p, mvproc_config *cfg){
    MYSQL mysql;
    if(NULL == mysql_init(&mysql))
        return "Failed init";
    if(mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, cfg->group) != 0)
        return "Failed Option";
    if(mysql_real_connect(&mysql, NULL, NULL, NULL, NULL, 0, NULL, 
                            CLIENT_MULTI_STATEMENTS) == NULL)
        return (char *)mysql_error(&mysql);
    char query[1024];
    sprintf(query, 
        "SELECT name, param_list FROM mysql.proc WHERE db='%s' AND type='PROCEDURE'",
        mysql.db);
    if(mysql_real_query(&mysql,query,strlen(query)) != 0)
        return (char *)mysql_error(&mysql);
    MYSQL_RES *result = mysql_store_result(&mysql);
    modmvproc_cache *ncache, *last = NULL;
    MYSQL_ROW row;
    while(NULL != (row = mysql_fetch_row(result))){
        ncache = (modmvproc_cache *)mvp_alloc(p, sizeof(modmvproc_cache));
        ncache->next = NULL;
        fill_proc_struct(p, (char *)row[0], (char *)row[1], ncache);
        if(last != NULL) last->next = ncache;
        else cfg->cache = ncache;
        last = ncache;
    };
    mysql_free_result(result);
    mysql_close(&mysql);
    return NULL;
}

size_t setUserVar(const char *n, const char *p, char *q){
    if(p == NULL)
        sprintf(q, "SET @%s = ''; ", n);
    else
        sprintf(q, "SET @%s = '%s'; ", n, p);
    return strlen(q);
}

mvproc_table *getDBResult(const mvproc_config *cfg, mvp_req_rec *r, int *errback){

    MYSQL *mysql = cfg->mysql_connect;
    MYSQL_RES *result;
    MYSQL_ROW row;
	modmvproc_cache *cache_entry = NULL;
	size_t qsize = 0, pos = 0;
    char *procname = (char *)mvp_alloc(r->pool, strlen(r->uri) + 1);
    if(r->uri[0] == '/')
        strcpy(procname, r->uri + 1);
    else
        strcpy(procname, r->uri);

    if(cfg->cache != NULL){
        cache_entry = cfg->cache;
        while(cache_entry != NULL){
            if(strcmp(cache_entry->procname,procname) == 0) break;
            cache_entry = cache_entry->next;
        };
        if(cache_entry == NULL){
            if(cfg->default_proc == NULL){
              	procname = (char *)mvp_alloc(r->pool, 8);
               	strcpy(procname, "landing");
            }else{
               	procname = (char *)mvp_alloc(r->pool, 
               		strlen(cfg->default_proc) + 1);
               	strcpy(procname, cfg->default_proc);
            };
            cache_entry = cfg->cache;
            while(cache_entry != NULL){
               	if(strcmp(cache_entry->procname,procname) == 0) break;
               	cache_entry = cache_entry->next;
            };
            if(cache_entry == NULL){
                perror_f("Request for unknown content: %s\n", procname);
                return error_out(cfg, r->pool, "Request for unknown content");
            };
        };
    }else{
        /* Development mode - NO CACHE */
        if(mysql_ping(mysql) != 0){
            mysql_close(mysql);
            mysql_init(mysql);
            if(mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, cfg->group) != 0){
                perror("MYSQL Options Failed\n");
                return error_out(cfg, r->pool, mysql_error(mysql));
            };
            if(mysql_real_connect(mysql, NULL, NULL, NULL, NULL, 0, NULL, 
                                  CLIENT_MULTI_STATEMENTS) == NULL){
                perror("MYSQL Connection Failed\n");
                return error_out(cfg, r->pool, mysql_error(mysql));
            };
        };

        qsize = 85 + strlen(mysql->db) + strlen(procname);
        char *proc_query = (char *)mvp_alloc(r->pool, qsize);
        sprintf(proc_query, 
            "SELECT name, param_list FROM mysql.proc WHERE db='%s' AND type='PROCEDURE' AND name='%s'",
            mysql->db, procname);
        if(mysql_real_query(mysql,proc_query,strlen(proc_query)) != 0){
            return error_out(cfg, r->pool, mysql_error(mysql));
        };
        result = mysql_store_result(mysql);
        if(mysql_num_rows(result) < 1){
            mysql_free_result(result);
            if(cfg->default_proc == NULL){
                procname = (char *)mvp_alloc(r->pool, 8);
                strcpy(procname, "landing");
            }else{
                procname = (char *)mvp_alloc(r->pool, strlen(cfg->default_proc) + 1);
                strcpy(procname, cfg->default_proc);
            };
            qsize = 85 + strlen(mysql->db) + strlen(procname);
            proc_query = (char *)mvp_alloc(r->pool, qsize);
            sprintf(proc_query, 
                "SELECT name, param_list FROM mysql.proc WHERE db='%s' AND type='PROCEDURE' AND name='%s'",
                mysql->db, procname);
            if(mysql_real_query(mysql,proc_query,strlen(proc_query)) != 0){
                return error_out(cfg, r->pool, mysql_error(mysql));
            };
            result = mysql_store_result(mysql);
            if(mysql_num_rows(result) < 1){
                mysql_free_result(result);
                return error_out(cfg, r->pool, "Configuration error: No default proc?");
            };
        };
        row = mysql_fetch_row(result);
        if(row == NULL){
            return error_out(cfg, r->pool, mysql_error(mysql));
        };
        cache_entry = 
            (modmvproc_cache *)mvp_alloc(r->pool, (sizeof(modmvproc_cache)));
        if(cache_entry == NULL) OUT_OF_MEMORY;
        fill_proc_struct(r->pool, (char *)row[0], (char *)row[1], cache_entry);
    };

    /* large starting size for headroom and changes */
    qsize = 1024 + strlen(procname) + (
        strlen(r->session) * 2 + 
        strlen(r->server_hostname) * 2 +
        strlen(r->method) * 2 +
        strlen(r->uri) * 2 +
        strlen(r->agent) * 2 +
        strlen(r->useragent_ip)
        ) * 2;

    db_param_t *param = cache_entry->param_list;
    db_call_param inparms[cache_entry->num_params];
    mvp_in_param *cgi_parm = NULL;
    mvulong parm_ind = 0;
    while(param != NULL){
        inparms[parm_ind].val = NULL;
        cgi_parm = r->parsed_params;
        while(cgi_parm && cgi_parm->name){
            if(strcmp(cgi_parm->name, param->name) == 0){
                inparms[parm_ind].val = cgi_parm->val;
                break;
            };
            cgi_parm = cgi_parm->next;
        };
        switch(param->in_or_out){
        case IN:
            if(inparms[parm_ind].val == NULL){
                qsize += 6;
            }else{
                qsize += strlen(inparms[parm_ind].val) + 4;
            };
            break;
        case INOUT:
            if(inparms[parm_ind].val == NULL){
                qsize += strlen(param->name) * 2 + 19;
            }else{
                qsize += strlen(param->name) * 2 + 
                         strlen(inparms[parm_ind].val) + 17;
            };
            break;
        case OUT:
            qsize += strlen(param->name) * 2 + 17;
            break;
        default:
            break;
        };
        inparms[parm_ind].param = param;
        parm_ind++;
        param = param->next;
    };
    
    user_var_t *uvar = cfg->user_vars;
    while(uvar != NULL){
    	    qsize += strlen(uvar->varname) * 2 + 21;
    	    uvar = uvar->next;
    };
    
    pos = 0;
    char query[qsize];
    for(parm_ind = 0; parm_ind < cache_entry->num_params; parm_ind++){
        switch(inparms[parm_ind].param->in_or_out){
        case INOUT:
            if(inparms[parm_ind].val == NULL){
                sprintf(&query[pos],"SET @%s = NULL; ", 
                        inparms[parm_ind].param->name);
            }else{
                sprintf(&query[pos],"SET @%s = '%s'; ",
                        inparms[parm_ind].param->name,
                        inparms[parm_ind].val);
            };
            pos = strlen(query);
            break;
        case OUT:
            sprintf(&query[pos],"SET @%s = ''; ",
                    inparms[parm_ind].param->name);
            pos = strlen(query);
            break;
        default:
            break;
        };
    };
    
    if(cfg->session == 'Y' || cfg->session == 'y')
        pos += setUserVar("mvp_session", r->session, &query[pos]);
    if(cfg->template_dir != NULL && strlen(cfg->template_dir) > 0){
        pos += setUserVar("mvp_template", procname, &query[pos]);
        pos += setUserVar("mvp_layout", cfg->default_layout, &query[pos]);
    };
    if(cfg->allow_setcontent != NULL)
        pos += setUserVar("mvp_content_type", "", &query[pos]);
    pos += setUserVar("mvp_servername", r->server_hostname, &query[pos]);
    pos += setUserVar("mvp_requestmethod", r->method, &query[pos]);
    pos += setUserVar("mvp_uri", r->uri, &query[pos]);
    pos += setUserVar("mvp_agent_id", r->agent, &query[pos]);
    pos += setUserVar("mvp_remoteip", r->useragent_ip, &query[pos]);
    uvar = cfg->user_vars;
    while(uvar != NULL){
    	pos += setUserVar(uvar->varname, NULL, &query[pos]);
    	uvar = uvar->next;
    };
    
    sprintf(&query[pos], "CALL %s(",cache_entry->procname);
    pos = strlen(query);
    param = cache_entry->param_list;
    for(parm_ind = 0; parm_ind < cache_entry->num_params; parm_ind++){
        switch(inparms[parm_ind].param->in_or_out){
        case IN:
            if(inparms[parm_ind].val == NULL){
                strcpy(&query[pos], "NULL");
            }else{
                sprintf(&query[pos],"'%s'", inparms[parm_ind].val);
            };
            pos = strlen(query);
            break;
        case INOUT:
        case OUT:
            sprintf(&query[pos],"@%s",inparms[parm_ind].param->name);
            pos = strlen(query);
            break;
        };
        if(inparms[parm_ind].param->next != NULL){
            query[pos] = ',';
            pos++;
        };
    };
    sprintf(&query[pos],");");
    pos += 2;

    qsize = 0;
    for(parm_ind = 0; parm_ind < cache_entry->num_params; parm_ind++){
        switch(inparms[parm_ind].param->in_or_out){
        case INOUT:
        case OUT:
            sprintf(&query[pos],"%s@%s", qsize > 0 ? ", " : " SELECT ",
                inparms[parm_ind].param->name);
            pos = strlen(query);
            qsize++;
            break;
        default:
            break;
        };
    };

    if(cfg->session == 'Y' || cfg->session == 'y'){
        sprintf(&query[pos],
                "%s@%s", qsize > 0 ? ", ":" SELECT ","mvp_session");
        pos = strlen(query);
        qsize++;
    };

    if(cfg->template_dir != NULL && strlen(cfg->template_dir) > 0){
        sprintf(&query[pos],"%s@%s, @%s, @%s",qsize > 0 ? ", ":" SELECT ",
        	"mvp_template","mvp_layout","mvp_servername");
        pos = strlen(query);
        qsize += 3;
    };

    if(cfg->allow_setcontent != NULL){
        sprintf(&query[pos],
                "%s@%s",qsize > 0 ? ", ":" SELECT ","mvp_content_type");
        pos = strlen(query);
        qsize++;
    };

    uvar = cfg->user_vars;
    while(uvar != NULL){
        sprintf(&query[pos],"%s@%s",qsize > 0 ? ", ":" SELECT ",uvar->varname);
        pos = strlen(query);
        qsize++;
        uvar = uvar->next;
    };
    
    if(qsize > 0) sprintf(&query[pos],";");

    /* Make sure the connection to the DB is still live */
    if(mysql_ping(mysql) != 0){
        mysql_close(mysql);
        mysql_init(mysql);
        if(mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, cfg->group) != 0){
            perror("MYSQL Options Failed\n");
            return error_out(cfg, r->pool, mysql_error(mysql));
        };
        if(mysql_real_connect(mysql, NULL, NULL, NULL, NULL, 0, NULL, 
            CLIENT_MULTI_STATEMENTS) == NULL){
            perror("MYSQL Connection Failed\n");
            return error_out(cfg, r->pool, mysql_error(mysql));
        };
    };

    if(mysql_real_query(mysql,query,strlen(query)) != 0){
        return error_out(cfg, r->pool, mysql_error(mysql));
    };

    int status = 0;
    mvulong f, ro, c, *lens;
    db_col_type *coltypes;
    MYSQL_FIELD *fields;
    
    mvproc_table *next = 
    (mvproc_table *)mvp_alloc(r->pool, sizeof(mvproc_table));
    if(next == NULL) OUT_OF_MEMORY;
    next->next = NULL;
    next->name = NULL;
    mvproc_table *tables = next;
    mvproc_table *last = NULL;
    
    do{
        result = mysql_store_result(mysql);
        if(result){
            next->num_rows = mysql_num_rows(result);
            if(next->num_rows < 1)continue;
            next->num_fields = mysql_num_fields(result);
            next->cols = (db_col_t *)mvp_alloc(r->pool, 
                next->num_fields * sizeof(db_col_t));
            if(next->cols == NULL) OUT_OF_MEMORY;
            coltypes = (db_col_type *)mvp_alloc(r->pool,
                next->num_fields * sizeof(db_col_type));
            for(c = 0; c < next->num_fields; c++)
                next->cols[c].name = NULL;

            fields = mysql_fetch_fields(result);
            next->name = NULL;
            for(f = 0; f < next->num_fields; f++){
                switch(fields[f].type){
                case MYSQL_TYPE_BLOB:
                    coltypes[f] = _BLOB;
                    break;
                case MYSQL_TYPE_BIT:
                case MYSQL_TYPE_TINY:
                case MYSQL_TYPE_SHORT:
                case MYSQL_TYPE_LONG:
                case MYSQL_TYPE_INT24:
                case MYSQL_TYPE_LONGLONG:
                    coltypes[f] = _LONG;
                    break;
                case MYSQL_TYPE_DECIMAL:
                case MYSQL_TYPE_NEWDECIMAL:
                case MYSQL_TYPE_FLOAT:
                case MYSQL_TYPE_DOUBLE:
                    coltypes[f] = _DOUBLE;
                    break;
                default:
                    coltypes[f] = _STRING;
                    break;
                };
                if(fields[f].length > 32) coltypes[f] = _BLOB;
                next->cols[f].name = 
                    (char *)mvp_alloc(r->pool, strlen(fields[f].name) + 1);
                if(next->cols[f].name == NULL) OUT_OF_MEMORY;
                if(fields[f].name[0] == '@')
                    strcpy(next->cols[f].name, &fields[f].name[1]);
                else
                    strcpy(next->cols[f].name, fields[f].name);

                next->cols[f].vals = (db_val_t *)mvp_alloc(r->pool,
                    next->num_rows * sizeof(db_val_t));
                if(next->cols[f].vals == NULL) OUT_OF_MEMORY;
                if(next->name == NULL && strlen(fields[f].table) > 0){
                    next->name = 
                        (char *)mvp_alloc(r->pool, strlen(fields[f].table) + 1);
                    if(next->name == NULL) OUT_OF_MEMORY;
                    strcpy(next->name, fields[f].table);
                };
            };
            for(ro = 0; ro < next->num_rows; ro++){
                row = mysql_fetch_row(result);
                if(row == NULL) break;
                lens = mysql_fetch_lengths(result);
                for(f = 0; f < next->num_fields; f++){
                    next->cols[f].vals[ro].type = coltypes[f];
                    next->cols[f].vals[ro].size = lens[f];
                    next->cols[f].vals[ro].val = 
                    (char *)mvp_alloc(r->pool, lens[f] + 1);
                    if(next->cols[f].vals[ro].val == NULL) OUT_OF_MEMORY;
                    memcpy(next->cols[f].vals[ro].val, row[f], lens[f]);
                    next->cols[f].vals[ro].val[lens[f]] = '\0';
                };
            };
            
            if(!mysql_more_results(mysql) && qsize > 0){ 
                /* This means we're looking at the last result - 
                    The INOUTs, OUTs, and session vars */
                next->name = (char *)mvp_alloc(r->pool, 9);
                if(next->name == NULL) OUT_OF_MEMORY;
                strcpy(next->name, "PROC_OUT");
                if(last != NULL)
                    last->next = next;
            }else{
                if(next->name == NULL){
                    next->name = (char *)mvp_alloc(r->pool, 7);
                    if(next->name == NULL) OUT_OF_MEMORY;
                    strcpy(next->name, "status");
                };
                if(last != NULL)
                    last->next = next;
                last = next;
                next = (mvproc_table *)mvp_alloc(r->pool, sizeof(mvproc_table));
                if(next == NULL) OUT_OF_MEMORY;
                next->next = NULL;
            };
            mysql_free_result(result);
        };
        status = mysql_next_result(mysql);
        if(status > 0){
            return error_out(cfg, r->pool, mysql_error(mysql));
        };
    }while(status == 0);
    
    if(tables->name == NULL){
        tables->name = (char *)mvp_alloc(r->pool, 10);
        if(tables->name == NULL) OUT_OF_MEMORY;
        strcpy(tables->name, "no_result");
    };
    return tables;
}



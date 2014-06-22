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

db_val_t *lookup(mvp_pool *p, 
                 mvproc_table *tables, 
                 const char *tableName, 
                 const char *colName, 
                 mvulong rowNum){
    if(strcmp(colName, "CURRENT_ROW") == 0){
        db_val_t *ret_val = (db_val_t *)mvp_alloc(p, (sizeof(db_val_t)));
        if(ret_val == NULL) return NULL;
        ret_val->val = (char *)mvp_alloc(p, 20);
        if(ret_val->val == NULL) return NULL;
        sprintf(ret_val->val, "%lu", rowNum);
        ret_val->type = _LONG;
        return ret_val;
    };
    if(strcmp(colName, "NUM_ROWS") == 0){
        db_val_t *ret_val = (db_val_t *)mvp_alloc(p, (sizeof(db_val_t)));
        if(ret_val == NULL) return NULL;
        ret_val->val = (char *)mvp_alloc(p, 20);
        if(ret_val->val == NULL) return NULL;
        sprintf(ret_val->val, "%lu", rowNum);
        ret_val->type = _LONG;
        while(tables != NULL){
            if(tables->name != NULL && strcmp(tables->name, tableName) == 0){
                sprintf(ret_val->val, "%lu", tables->num_rows);
                return ret_val;
            };
            tables = tables->next;
        };
        sprintf(ret_val->val, "%i", 0);
        return ret_val;
    };
    mvulong cind = 0;
    while(tables != NULL){
        if(tables->name != NULL && 
            strcmp(tables->name, tableName) == 0 && 
            rowNum < tables->num_rows)
            for(cind = 0; cind < tables->num_fields; cind++)
            if(strcmp(tables->cols[cind].name, colName) == 0)
                return &tables->cols[cind].vals[rowNum];
        tables = tables->next;
    };
    return NULL;
}

void set_user_val(mvp_pool *p, 
                  mvproc_table *tables, 
                  char *tag, 
                  user_val_t *val){
    mvulong cind = 0;
    mvproc_table *ntable;
    db_col_t *ncol;
    while(tables != NULL){
        if(strcmp(tables->name, "@") == 0){
            for(cind = 0; cind < tables->num_fields; cind++){
                if(strcmp(tables->cols[cind].name, tag) == 0){
                    tables->cols[cind].vals[0].val = val->tag;
                    tables->cols[cind].vals[0].type = val->type;
                    tables->cols[cind].vals[0].size = strlen(val->tag);
                    return;
                };
            };
            tables->num_fields++;
            ncol = (db_col_t *)mvp_alloc(p, 
                tables->num_fields * sizeof(db_col_t));
            for(cind = 0; cind < tables->num_fields - 1; cind++){
                ncol[cind].name = tables->cols[cind].name;
                ncol[cind].vals = tables->cols[cind].vals;
            };
            ncol[cind].name = 
                (char *)mvp_alloc(p, strlen(tag) + 1);
            strcpy(ncol[cind].name, tag);
            ncol[cind].vals = (db_val_t *)mvp_alloc(p, (sizeof(db_val_t)));
            if(ncol[cind].vals == NULL) return;
            ncol[cind].vals[0].type = val->type;
            ncol[cind].vals[0].val = val->tag;
            ncol[cind].vals[0].size = strlen(val->tag);
            tables->cols = ncol;
            return;
        };
        if(tables->next == NULL){
            ntable = (mvproc_table *)mvp_alloc(p, sizeof(mvproc_table));
            if(ntable == NULL) return;
            ntable->name = (char *)mvp_alloc(p, 2);
            if(ntable->name == NULL) return;
            strcpy(ntable->name, "@");
            ntable->num_rows = 1;
            ntable->num_fields = 1;
            ntable->cols = (db_col_t *)mvp_alloc(p, sizeof(db_col_t));
            if(ntable->cols == NULL) return;
            ntable->cols[0].name = 
                (char *)mvp_alloc(p, strlen(tag) + 1);
            if(ntable->cols[0].name == NULL) return;
            strcpy(ntable->cols[0].name, tag);
            ntable->cols[0].vals = (db_val_t *)mvp_alloc(p, (sizeof(db_val_t)));
            if(ntable->cols[0].vals == NULL) return;
            ntable->cols[0].vals[0].type = val->type;
            ntable->cols[0].vals[0].val = val->tag;
            ntable->cols[0].vals[0].size = strlen(val->tag);
            ntable->next = NULL;
            tables->next = ntable;
            return;
        };
        tables = tables->next;
    };
}

char *mvp_escape_quotes(mvp_pool *p, 
                               const char *val){
    size_t pos = 0;
    size_t epos = 0;
    size_t end = strlen(val);
    char *esc = (char *)mvp_alloc(p, end * 2 + 1);
    if(esc == NULL) return NULL;
    for(pos = 0; pos < end; pos++, epos++){
        if(val[pos] == '\\'){
            esc[epos] = '\\';
            esc[++epos] = val[++pos];
            continue;
        };
        if(val[pos] == '"'){
            esc[epos] = '\\';
            esc[++epos] = '"';
            continue;
        };
        esc[epos] = val[pos];
    };
    esc[epos] = '\0';
    return esc;
}

void xml_out(mvp_req_rec *r, const mvproc_config *cfg, mvproc_table *tables){
	char tchr[21];
	mvulong rind, cind, blobcount;
	time_t tim = time(NULL);
	strftime(tchr,20,"%Y-%m-%d %H:%M:%S",localtime(&tim));
	printf("<?xml version='1.0' encoding='UTF-8'?>");
	printf("<results server_datetime='%s'>",tchr);
	while(tables != NULL){
		printf("<table name='%s'>", tables->name);
		for(rind = 0; rind < tables->num_rows; rind++){
			blobcount = 0;
			printf("%s", "<row ");
			for(cind = 0; cind < tables->num_fields; cind++){
				if(tables->cols[cind].vals[rind].type == _BLOB){
					blobcount++;
				}else{
					printf("%s=\"%s\" ",tables->cols[cind].name,
					    mvp_escape_quotes(r->pool, 
					        tables->cols[cind].vals[rind].val));
				};
			};
			if(blobcount == 0){
				printf("%s", "/>");
			}else{
				printf("%s", ">");
				for(cind = 0; cind < tables->num_fields; cind++){
					if(tables->cols[cind].vals[rind].type == _BLOB){
						printf("<%s><![CDATA[",tables->cols[cind].name);
						fwrite(tables->cols[cind].vals[rind].val, 1,
						    tables->cols[cind].vals[rind].size, stdout);
						printf("]]></%s>",tables->cols[cind].name);
					};
				};
				printf("%s", "</row>");
			};
		};
		printf("%s", "</table>");
		tables = tables->next;
	};
	printf("%s", "</results>\r\n");
	return;
}

void xml_plain(mvp_req_rec *r, const mvproc_config *cfg, mvproc_table *tables){
	char tchr[21];
	mvulong rind, cind;
	time_t tim = time(NULL);
	strftime(tchr,20,"%Y-%m-%d %H:%M:%S",localtime(&tim));
	printf("<?xml version='1.0' encoding='UTF-8'?>");
	printf("<results server_datetime='%s'>", tchr);
	while(tables != NULL){
		printf("<table name='%s'>", tables->name);
		for(rind = 0; rind < tables->num_rows; rind++){
			printf("%s", "<row>");
			for(cind = 0; cind < tables->num_fields; cind++){
				printf("<%s><![CDATA[",tables->cols[cind].name);
				fwrite(tables->cols[cind].vals[rind].val, 1,
					tables->cols[cind].vals[rind].size, stdout);
				printf("]]></%s>",tables->cols[cind].name);
			};
			printf("%s", "</row>");
		};
		printf("%s", "</table>");
		tables = tables->next;
	};
	printf("%s", "</results>\r\n");
	return;
}

void xml_easy(mvp_req_rec *r, const mvproc_config *cfg, mvproc_table *tables){
	char tchr[21];
	mvulong rind, cind;
	time_t tim = time(NULL);
	strftime(tchr,20,"%Y-%m-%d %H:%M:%S",localtime(&tim));
	printf("<?xml version='1.0' encoding='UTF-8'?>");
	printf("<results server_datetime='%s'>", tchr);
	while(tables != NULL){
		printf("<%s>", tables->name);
		for(rind = 0; rind < tables->num_rows; rind++){
			printf("%s", "<row>");
			for(cind = 0; cind < tables->num_fields; cind++){
				printf("<%s><![CDATA[",tables->cols[cind].name);
				fwrite(tables->cols[cind].vals[rind].val, 1,
					tables->cols[cind].vals[rind].size, stdout);
				printf("]]></%s>",tables->cols[cind].name);
			};
			printf("%s", "</row>");
		};
		printf("</%s>", tables->name);
		tables = tables->next;
	};
	printf("%s", "</results>\r\n");
	return;
}

void json_out(mvp_req_rec *r, const mvproc_config *cfg, mvproc_table *tables){
	char tchr[21];
	mvulong rind, cind;
	time_t tim = time(NULL);
	strftime(tchr,20,"%Y-%m-%d %H:%M:%S",localtime(&tim));
	printf("{\"server_datetime\":\"%s\",\"table\":[", tchr);
	while(tables != NULL){
		printf("{\"name\":\"%s\"%s", tables->name,
			tables->num_rows > 0 ? ",\"row\":[": "");
		for(rind = 0; rind < tables->num_rows; rind++){
			printf("%s", "{");
			for(cind = 0; cind < tables->num_fields; cind++){
				if(tables->num_fields - cind > 1)
				printf("\"%s\":\"%s\",",tables->cols[cind].name,
				    mvp_escape_quotes(r->pool, 
				        tables->cols[cind].vals[rind].val));
				else
				printf("\"%s\":\"%s\"",tables->cols[cind].name,
				    mvp_escape_quotes(r->pool, 
				        tables->cols[cind].vals[rind].val));
			};
			if(tables->num_rows - rind > 1) printf("%s", "},");
			else printf("%s", "}]");
		};
		printf("%s", "}");
		if(tables->next != NULL) printf("%s", ",");
		tables = tables->next;
	};
	printf("%s", "]}\r\n");
	return;
}

void easier_json_out(mvp_req_rec *r, const mvproc_config *cfg, mvproc_table *tables){
	char tchr[21];
	mvulong rind, cind;
	time_t tim = time(NULL);
	strftime(tchr,20,"%Y-%m-%d %H:%M:%S",localtime(&tim));
	printf("{\"server_datetime\":\"%s\"", tchr);
	while(tables != NULL){
		if(tables->num_rows >0 ){
			printf(",\"%s\":[", tables->name);
			for(rind = 0; rind < tables->num_rows; rind++){
				printf("%s", "{");
				for(cind = 0; cind < tables->num_fields; cind++){
					printf("\"%s\":\"%s\"%s",tables->cols[cind].name,
					    mvp_escape_quotes(r->pool, 
					        tables->cols[cind].vals[rind].val),
						tables->num_fields - cind > 1 ? "," : "");
				};
				printf("}%s", tables->num_rows - rind > 1 ? "," : "]");
			};
		};
		tables = tables->next;
	};
	printf("%s", "}\r\n");
	return;
}

void generate_output(mvp_req_rec *r, const mvproc_config *cfg, mvproc_table *tables){

    template_cache_t *tpl = NULL;
    template_cache_t *layout = NULL;
    db_val_t *tval = NULL;
    if(cfg->template_dir != NULL && strlen(cfg->template_dir) > 0){
        tval = lookup(r->pool, tables, "PROC_OUT", "mvp_template", 0);
        if(tval != NULL && tval->val != NULL && strlen(tval->val) > 0){
            tpl = get_template(r->pool, cfg, tval->val);
            if(tpl != NULL){
                tval = lookup(r->pool, tables, "PROC_OUT", "mvp_layout", 0);
                if(tval != NULL && tval->val != NULL && strlen(tval->val) > 0)
                    layout = get_template(r->pool, cfg, tval->val);
                if(layout != NULL)
                    tpl = layout;
            };
        };
        tval = NULL;
    };
    printf("Status: 200 OK\r\n");

    db_val_t *scv = lookup(r->pool, tables, "PROC_OUT", "mvp_session", 0);
    if(scv != NULL && (cfg->session == 'Y' || cfg->session == 'y'))
        printf("Set-Cookie: MVPSESSION=%s\r\n",scv->val);
    
    if(cfg->allow_setcontent != NULL)
        tval = lookup(r->pool, tables, "PROC_OUT", "mvp_content_type", 0);
    if(tval != NULL && tval->val != NULL && strlen(tval->val) > 0)
        printf("Content-type: %s\r\n\r\n", tval->val);
    else if(tpl != NULL)
        printf("Content-type: %s\r\n\r\n", "text/html");
    else if(cfg->output == _JSON || cfg->output == _JSON_EASY)
        printf("Content-type: %s\r\n\r\n", "application/json");
    else
        printf("Content-type: %s\r\n\r\n", "text/xml");
    
    if(tpl == NULL){
        switch(cfg->output){
        case _XML_EASY:
            xml_easy(r, cfg, tables);
            break;
        case _XML_NO_ATTR:
            xml_plain(r, cfg, tables);
            break;
        case _JSON:
            json_out(r, cfg, tables);
            break;
        case _JSON_EASY:
            easier_json_out(r, cfg, tables);
            break;
        default:
            xml_out(r, cfg, tables);
        };
    }else{
        fill_template(r, cfg, tpl, tables, "PROC_OUT", 0);
        printf("%s", "\r\n");
    };
}
	


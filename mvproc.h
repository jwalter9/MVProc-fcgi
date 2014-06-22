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
#ifndef _MVPROC_H
#define _MVPROC_H

#define CONFIG_LOCATION "/etc/mvproc.conf"
#define INIT_ERR_PATH "/tmp/mvproc_init.err"
#define MAX_NEST 64
#define DECLINED 403
#define DEFAULT_MAX_CONTENT_LENGTH 26214400 /* 25 MB, just like Gmail */
#define MEM_PAGE_SIZE 10240 /* 10K should be a good default */

#include "fcgi_stdio.h"
#include "stdlib.h"
#include "dirent.h"
#include "string.h"
#include "time.h"
#include "math.h"
#include "unistd.h"
#include "sys/stat.h" 
#include "mysql/mysql.h"
#include "openssl/md5.h"

typedef unsigned long mvulong;


/*  DB typedefs  */
typedef enum { IN, INOUT, OUT } inout_t;

typedef struct db_param_t {
    char *name;
    inout_t in_or_out;
    struct db_param_t *next;
} db_param_t;

typedef struct {
    db_param_t *param;
    char *val;
} db_call_param;

typedef struct modmvproc_cache {
    char *procname;
    db_param_t *param_list;
    struct modmvproc_cache *next;
    size_t num_params;
} modmvproc_cache;

typedef enum {
    _BLOB,
    _STRING,
    _DOUBLE,
    _LONG,
    _DATETIME
} db_col_type;

typedef struct {
    char *val;
    mvulong size;
    db_col_type type;
} db_val_t;

typedef struct {
    char *name;
    db_val_t *vals;
} db_col_t;

typedef struct mvproc_table {
    char *name;
    mvulong num_rows;
    mvulong num_fields;
    db_col_t *cols;
    struct mvproc_table *next;
} mvproc_table;


/*  Template typedefs  */
typedef enum {
    _NOTAG,
    _VALUE,
    _IF,
    _ELSIF,
    _ELSE,
    _ENDIF,
    _LOOP,
    _ENDLOOP,
    _INCLUDE,
    _TEMPLATE,
    _SET
} tag_type;

typedef enum {
    _EQ,
    _NE,
    _GT,
    _GTE,
    _LT,
    _LTE,
    _NOTNULL,
    _NULL
} oper_t;

typedef enum {
    _SETVAL,
    _ADD,
    _SUBTRACT,
    _MULTIPLY,
    _DIVIDE,
    _MOD,
    _ALSO,
    _NOOP
} mvmath_t;

typedef enum {
    _XML_MIXED,
    _XML_NO_ATTR,
    _XML_EASY,
    _JSON_EASY,
    _JSON
} out_type;

typedef struct {
    char *left;
    unsigned short cons_left;
    char *right;
    unsigned short cons_right;
    oper_t oper;
    db_col_type type;
} expression_t;

typedef struct cond_t {
    expression_t *exp;
    struct cond_t *deeper;
    struct cond_t *orc;
    struct cond_t *andc;
} cond_t;

typedef struct user_val_t {
    char *tag;
    unsigned short cons;
    db_col_type type;
    mvmath_t oper;
    struct user_val_t *deeper;
    struct user_val_t *next;
} user_val_t;

typedef struct template_segment_t {
    char *tag; /* NULL for first section of a template */
    char *follow_text;
    tag_type type;
    cond_t *ifs; /* NULL unless type _IF or _ELSIF */
    user_val_t *sets;
    struct template_segment_t *next;
} template_segment_t;

typedef struct template_cache_t {
    char *filename;
    char *file_content;
    template_segment_t *pieces;
    struct template_cache_t *next;
} template_cache_t;

typedef struct {
	char session;
	char *group;
	char *template_dir;
	modmvproc_cache *cache;
	template_cache_t *template_cache;
	out_type output;
	char *error_tpl;
	char *default_layout;
	char *allow_setcontent;
	char allow_html_chars;
	char *upload_dir;
	char *default_proc;
	MYSQL *mysql_connect;
	long max_content_length;
} mvproc_config;

typedef struct {
    const char *table;
    mvulong cur_row;
    mvulong num_rows;
    template_segment_t *start_piece;
} fornest_tracker;

typedef struct mvp_pool {
    void *page;
    size_t curpos;
    struct mvp_pool *next;
} mvp_pool;

typedef struct {
    char *disposition;
    char *type;
    char *encoding;
    char *boundary;
    char *name;
    char *filename;
    size_t size;
} multipart_headers;

typedef struct mvp_in_param {
    char *name;
    char *val;
    struct mvp_in_param *next;
} mvp_in_param;

typedef struct {
    char *uri;
    char *server_hostname;
    char *method;
    char *useragent_ip;
    char *agent;
    char *session;
    mvp_pool *pool;
    mvp_in_param *parsed_params;
} mvp_req_rec;

#define OUT_OF_MEMORY \
    { perror("Out of memory: mvp_alloc returned NULL\n"); \
      return NULL; }

void *mvp_alloc(mvp_pool *p, size_t s);
void mvp_free(mvp_pool *p);
mvp_in_param *get_new_param(mvp_pool *p);
void perror_f(const char *fmt, const char *ins);
mvproc_config *populate_config(mvp_pool *configPool);
db_val_t *lookup(mvp_pool *p, mvproc_table *tables, const char *tableName, 
                 const char *colName, mvulong rowNum);
void set_user_val(mvp_pool *p, mvproc_table *tables, char *tag, user_val_t *val);
template_cache_t *get_template(mvp_pool *p, const mvproc_config *cfg, char *fname);
void fill_template(mvp_req_rec *r, const mvproc_config *cfg, template_cache_t *tpl, 
                   mvproc_table *tables, const char *cur_table, mvulong cur_row);
const char *build_cache(mvp_pool *p, mvproc_config *cfg);
mvproc_table *getDBResult(const mvproc_config *cfg, mvp_req_rec *r, int *errback);
void generate_output(mvp_req_rec *r, const mvproc_config *cfg, mvproc_table *tables);
template_cache_t *parse_template(mvp_pool *p, char *tplstr);
const char *build_template_cache(mvp_pool *p, mvproc_config *cfg);
char *get_file_chars(mvp_pool *p, char *dir, const char *tpl);
mvp_in_param *get_new_param(mvp_pool *p);
char *scrub(mvp_pool *p, const char *str, size_t length);
mvproc_table *error_out(const mvproc_config *cfg, mvp_pool *p, const char *err);

#endif


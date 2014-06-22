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

void init_error(const char *fmt, const char *err){
    FILE *fp = fopen(INIT_ERR_PATH, "a");
    fprintf(fp, fmt, err);
    fclose(fp);
}

const char *set_template_dir(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL) return NULL;
    cfg->template_dir = (char *)mvp_alloc(p, strlen(arg) + 2);
    if(cfg->template_dir == NULL) return "OUT OF MEMORY";
    strcpy(cfg->template_dir, arg);
    size_t pos = strlen(cfg->template_dir);
    if(cfg->template_dir[pos-1] != '/')
        strcpy(&cfg->template_dir[pos], "/");
    return NULL;
}

const char *set_db_group(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL) return "dbGroup is required";
    cfg->group = (char *)mvp_alloc(p, strlen(arg) + 1);
    if(cfg->group == NULL) return "OUT OF MEMORY";
    strcpy(cfg->group, arg);
    return NULL;
}

const char *set_default_proc(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL) return NULL;
    cfg->default_proc = (char *)mvp_alloc(p, strlen(arg) + 1);
    if(cfg->default_proc == NULL) return "OUT OF MEMORY";
    strcpy(cfg->default_proc, arg);
    return NULL;
}

const char *set_session(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL)
        cfg->session = 'N';
    else
        cfg->session = arg[0];
    return NULL;
}

const char *maybe_build_cache(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL) return NULL;
    if(arg[0] == 'N' || arg[0] == 'n') return NULL;
    const char *cv = NULL;
    if(arg[0] == 'Y' || arg[0] == 'y' || arg[0] == 'D' || arg[0] == 'd'){
        cv = build_cache(p, cfg);
    };
    if(cv == NULL && cfg->template_dir != NULL &&
        (arg[0] == 'Y' || arg[0] == 'y' || arg[0] == 'T' || arg[0] == 't')){
        cv = build_template_cache(p, cfg);
	};
	return cv;
}

const char *set_out_type(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL)
        cfg->output = _XML_MIXED;
    else if(strcmp(arg, "PLAIN") == 0 || strcmp(arg,"plain") == 0)
        cfg->output = _XML_NO_ATTR;
    else if(strcmp(arg, "EASY_XML") == 0 || strcmp(arg,"easy_xml") == 0)
        cfg->output = _XML_EASY;
    else if(strcmp(arg, "JSON") == 0 || strcmp(arg,"json") == 0)
        cfg->output = _JSON;
    else if(strcmp(arg, "EASY_JSON") == 0 || strcmp(arg,"easy_json") == 0)
        cfg->output = _JSON_EASY;
    else
        cfg->output = _XML_MIXED;
    return NULL;
}

const char *set_error_tpl(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL) return NULL;
    cfg->error_tpl = (char *)mvp_alloc(p, strlen(arg) + 1);
    if(cfg->error_tpl == NULL) return "OUT OF MEMORY";
    strcpy(cfg->error_tpl, arg);
    return NULL;
}

const char *set_default_layout(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL) return NULL;
    cfg->default_layout = (char *)mvp_alloc(p, strlen(arg) + 1);
    if(cfg->default_layout == NULL) return "OUT OF MEMORY";
    strcpy(cfg->default_layout, arg);
    return NULL;
}

const char *set_allow_setcontent(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL || (arg[0] != 'Y' && arg[0] != 'y')) return NULL;
    cfg->allow_setcontent = (char *)mvp_alloc(p, strlen(arg) + 1);
    if(cfg->allow_setcontent == NULL) return "OUT OF MEMORY";
    strcpy(cfg->allow_setcontent, arg);
    return NULL;
}

const char *set_allow_html_chars(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL || (arg[0] != 'Y' && arg[0] != 'y')){
        cfg->allow_html_chars = 'N';
    }else{
        cfg->allow_html_chars = 'Y';
    };
    return NULL;
}

const char *set_upload_dir(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL || strlen(arg) < 1){
        cfg->upload_dir = (char *) mvp_alloc(p, 5);
        strcpy(cfg->upload_dir, "/tmp");
        return NULL;
    };
    if(strlen(arg) > 1000) 
        return "Upload directory path must be less than 1000 chars."
               " Try a symbolic link.";
    DIR *d = opendir(arg);
    if(!d){
        char *err = (char *)mvp_alloc(p, strlen(arg) + 27);
        if(err == NULL) return "OUT OF MEMORY";
        sprintf(err, "Could not open directory: %s", arg);
    };
    closedir(d);
    cfg->upload_dir = (char *)mvp_alloc(p, strlen(arg) + 1);
    if(cfg->upload_dir == NULL) return "OUT OF MEMORY";
    strcpy(cfg->upload_dir, arg);
    return NULL;
}

const char *set_max_content(mvp_pool *p, mvproc_config *cfg, char *arg){
    if(arg == NULL)
        cfg->max_content_length = DEFAULT_MAX_CONTENT_LENGTH;
    else
        cfg->max_content_length = atol(arg);
    return NULL;
}

char *get_arg(char *conf, const char *directive, mvp_pool *p){
    char *arg = strstr(conf, directive);
    if(arg == NULL) return NULL;
    arg += strlen(directive);
    arg += strspn(arg, " \t\r\n");
    size_t sz = strcspn(arg, " \t\r\n");
    char *out = (char *)mvp_alloc(p, sz + 1); 
    if(out == NULL) return NULL;
    strncpy(out, arg, sz);
    out[sz] = '\0';
    return out;
}

mvproc_config *populate_config(mvp_pool *configPool){
    mvproc_config *cfg = 
        (mvproc_config *)mvp_alloc(configPool, sizeof(mvproc_config));
    if(cfg == NULL){
        init_error("%s","Failed to allocate memory for configuration.\n");
        return NULL;
    };
    char *content = (char *)calloc(10240, 1);
    FILE *cf = fopen(CONFIG_LOCATION, "r");
    fread(content, 10240, 1, cf);
    fclose(cf);

    const char *error;
    error = set_db_group(configPool, cfg, 
        get_arg(content, "dbGroup", configPool));
    if(!error)
        error = set_default_proc(configPool, cfg, 
            get_arg(content, "defaultProc", configPool));
    if(!error)
        error = set_session(configPool, cfg, 
            get_arg(content, "session", configPool));
    if(!error)
        error = set_template_dir(configPool, cfg, 
            get_arg(content, "templateDir", configPool));
    if(!error)
        error = set_default_layout(configPool, cfg, 
            get_arg(content, "defaultLayout", configPool));
    if(!error)
        error = set_error_tpl(configPool, cfg, 
            get_arg(content, "errTemplate", configPool));
    if(!error)
        error = maybe_build_cache(configPool, cfg, 
            get_arg(content, "cache", configPool));
    if(!error)
        error = set_out_type(configPool, cfg, 
            get_arg(content, "outputStyle", configPool));
    if(!error)
        error = set_allow_setcontent(configPool, cfg, 
            get_arg(content, "allowSetContent", configPool));
    if(!error)
        error = set_allow_html_chars(configPool, cfg, 
            get_arg(content, "allowHTMLfromDB", configPool));
    if(!error)
        error = set_upload_dir(configPool, cfg, 
            get_arg(content, "uploadDirectory", configPool));
    if(!error)
        error = set_max_content(configPool, cfg, 
            get_arg(content, "maxPostSize", configPool));
    if(error){
        init_error("Configuration error: %s\n", error);
        return NULL;
    };
    free(content);

    cfg->mysql_connect = (MYSQL *)mvp_alloc(configPool, sizeof(MYSQL));
    if(cfg->mysql_connect == NULL){
        init_error("%s","Failed alloc for mysql\n");
        return NULL;
    };
    mysql_init(cfg->mysql_connect);
    if(mysql_options(cfg->mysql_connect, 
        MYSQL_READ_DEFAULT_GROUP, cfg->group) != 0){
        init_error("%s","MYSQL Options Failed\n");
        return NULL;
    };
    if(mysql_real_connect(cfg->mysql_connect, 
        NULL, NULL, NULL, NULL, 0, NULL, CLIENT_MULTI_STATEMENTS) == NULL){
        init_error("MYSQL Connect Error: %s\n", 
            mysql_error(cfg->mysql_connect));
        return NULL;
    };
    
    return cfg;
}

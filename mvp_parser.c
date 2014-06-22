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

char *get_file_chars(mvp_pool *p, char *dir, const char *tpl){
    char template_path[FILENAME_MAX];
    strcpy(template_path, dir);
    size_t pos = strlen(dir);
    strcpy(&template_path[pos], tpl);
    pos = strlen(template_path);
    strcpy(&template_path[pos], ".tpl");
    FILE *tf;
    long sz;
    tf = fopen(template_path, "rb");
    if(tf == NULL) return NULL;
    if(fseek(tf, 0L, SEEK_END)){
        fclose(tf);
        return NULL;
    };
    sz = ftell(tf);
    fclose(tf);
    if(sz == -1L) return NULL;
    tf = fopen(template_path, "r");
    if(tf == NULL) return NULL;
    char *content = (char *)mvp_alloc(p, sz + 1);
    fread(content, sz, 1, tf);
    fclose(tf);
    content[sz] = '\0';
    return content;
}

size_t parse_expression(mvp_pool *p, char *tag, expression_t *expr){
    expr->type = _STRING;
    expr->cons_left = 0;
    expr->cons_right = 0;
    expr->right = NULL;
    expr->oper = _NOTNULL;
    size_t i, f, pos;
    pos = strspn(tag, " \t\r\n");
    if(tag[pos] == '!'){
        expr->oper = _NULL;
        pos++;
        pos += strspn(&tag[pos], " \t\r\n");
        expr->left = &tag[pos];
        pos += strcspn(&tag[pos], " #)\t\r\n");
        if(tag[pos] != '#'){
            tag[pos] = '\0';
            pos++;
        };
        return pos;
    };
    if(tag[pos] == '\''){
        pos++;
        expr->left = &tag[pos];
        pos += strcspn(&tag[pos], "'");
        while(tag[pos - 1] == '\\'){
            pos++;
            pos += strcspn(&tag[pos], "'");
        };
        tag[pos] = '\0';
        pos++;
        pos += strspn(&tag[pos], " \t\r\n");
        expr->type = _STRING;
        expr->cons_left = 1;
    }else{
        expr->left = &tag[pos];
        i = strspn(expr->left, "0123456789");
        f = strspn(expr->left, ".0123456789");
        if(f > i){
            expr->type = _DOUBLE;
            expr->cons_left = 1;
            pos += i;
            if(strspn(&tag[pos], " \t\r\n") > 0)
                tag[pos] = '\0';
        }else if(i > 0){
            expr->type = _LONG;
            expr->cons_left = 1;
            pos += f;
            if(strspn(&tag[pos], " \t\r\n") > 0)
                tag[pos] = '\0';
        }else{
            i = strcspn(expr->left, "#)<=>!");
            f = strcspn(expr->left, " \t\r\n");
            if(i > f){
                pos += f;
                tag[pos] = '\0';
                pos++;
                pos += strspn(&tag[pos], " \t\r\n");
            }else{
                pos += i;
            };
        };
    };
    if(strncmp(&tag[pos], "<>", 2) == 0 || 
        strncmp(&tag[pos], "!=", 2) == 0){
        tag[pos] = '\0';
        pos += 2;
        expr->oper = _NE;
    }else if(strncmp(&tag[pos], "<=", 2) == 0){
        tag[pos] = '\0';
        pos += 2;
        expr->oper = _LTE;
    }else if(strncmp(&tag[pos], ">=", 2) == 0){
        tag[pos] = '\0';
        pos += 2;
        expr->oper = _GTE;
    }else if(strncmp(&tag[pos], "==", 2) == 0){
        tag[pos] = '\0';
        pos += 2;
        expr->oper = _EQ;
    }else if(tag[pos] == '='){
        tag[pos] = '\0';
        pos++;
        expr->oper = _EQ;
    }else if(tag[pos] == '<'){
        tag[pos] = '\0';
        pos++;
        expr->oper = _LT;
    }else if(tag[pos] == '>'){
        tag[pos] = '\0';
        pos++;
        expr->oper = _GT;
    }else{ /* Treat it as a singular expression */
        expr->oper = _NOTNULL;
        if(tag[pos] != '#'){
            tag[pos] = '\0';
            pos++;
        };
        return pos;
    };
    pos += strspn(&tag[pos], " \t\r\n");
    if(tag[pos] == '\''){
        pos++;
        expr->right = &tag[pos];
        pos += strcspn(&tag[pos], "'");
        while(tag[pos - 1] == '\\'){
            pos++;
            pos += strcspn(&tag[pos], "'");
        };
        tag[pos] = '\0';
        pos++;
        expr->type = _STRING;
        expr->cons_right = 1;
    }else{
        expr->right = &tag[pos];
        i = strspn(expr->right, "0123456789");
        f = strspn(expr->right, ".0123456789");
        if(f > i){
            expr->type = _DOUBLE;
            expr->cons_right = 1;
            pos += f;
            if(strspn(&tag[pos], " \t\r\n") > 0){
                tag[pos] = '\0';
                pos++;
            };
        }else if(i > 0){
            expr->type = _LONG;
            expr->cons_right = 1;
            pos += i;
            if(strspn(&tag[pos], " \t\r\n") > 0){
                tag[pos] = '\0';
                pos++;
            };
        }else{
            i = strcspn(expr->right, "#)");
            f = strcspn(expr->right, " \t\r\n");
            if(i > f){
                pos += f;
                tag[pos] = '\0';
                pos++;
            }else{
                pos += i;
            };
        };
    };
    pos += strspn(&tag[pos], " \t\r\n");
    return pos;
}

size_t parse_cond(mvp_pool *p, char *tag, cond_t *ifs){
    size_t pos = strspn(tag, " \t\r\n");
    if(strncmp(&tag[pos], "#>", 2) == 0)
        return pos;

    ifs->exp = NULL;
    ifs->deeper = NULL;
    ifs->andc = NULL;
    ifs->orc = NULL;
    if(tag[pos] == '('){
        tag[pos] = '\0';
        pos++;
        ifs->deeper = (cond_t *)mvp_alloc(p, (sizeof(cond_t)));
        if(ifs->deeper == NULL) return strcspn(tag, "#");
        pos += parse_cond(p, &tag[pos], ifs->deeper);
        pos += strcspn(&tag[pos], "#)");
        if(tag[pos] == ')'){
            tag[pos] = '\0';
            pos++;
        };
    }else{
        ifs->exp = (expression_t *)mvp_alloc(p, (sizeof(expression_t)));
        if(ifs->exp == NULL) return strcspn(tag, "#");
        pos += parse_expression(p, &tag[pos], ifs->exp);
    };
    pos += strspn(&tag[pos], " \t\r\n");
    if(strncmp(&tag[pos], "OR ", 3) == 0 
        || strncmp(&tag[pos], "or ", 3) == 0 
        || strncmp(&tag[pos], "|| ", 3) == 0){
        pos += 3;
        ifs->orc = (cond_t *)mvp_alloc(p, (sizeof(cond_t)));
        if(ifs->orc == NULL) return strcspn(tag, "#");
        pos += parse_cond(p, &tag[pos], ifs->orc);
        return pos;
    };
    if(strncmp(&tag[pos], "AND ", 4) == 0 
        || strncmp(&tag[pos], "and ", 4) == 0){
        pos += 4;
        ifs->andc = (cond_t *)mvp_alloc(p, (sizeof(cond_t)));
        if(ifs->andc == NULL) return strcspn(tag, "#");
        pos += parse_cond(p, &tag[pos], ifs->andc);
        return pos;
    };
    if(strncmp(&tag[pos], "&& ", 3) == 0){
        pos += 3;
        ifs->andc = (cond_t *)mvp_alloc(p, (sizeof(cond_t)));
        if(ifs->andc == NULL) return strcspn(tag, "#");
        pos += parse_cond(p, &tag[pos], ifs->andc);
        return pos;
    };
    
    return pos;
}

size_t parse_set(mvp_pool *p, char *tag, user_val_t *setv){
    size_t pos = strspn(tag, " \t\r\n)"), i, f;
    if(strncmp(&tag[pos], "#>", 2) == 0)
        return pos;

    setv->tag = NULL;
    setv->cons = 0;
    setv->type = _DOUBLE;
    setv->next = NULL;
    setv->deeper = NULL;
    setv->oper = _NOOP;
    if(tag[pos] == '('){
        pos ++;
        setv->deeper = (user_val_t *)mvp_alloc(p, (sizeof(user_val_t)));
        if(setv->deeper == NULL) return strcspn(tag, "#");
        pos += parse_set(p, &tag[pos], setv->deeper);
        pos += strcspn(&tag[pos], ")");
        tag[pos] = '\0';
        pos++;
    }else{
        setv->tag = &tag[pos];
        if(tag[pos] == '\''){
            pos++;
            setv->tag = &tag[pos];
            pos += strcspn(&tag[pos], "'");
            while(tag[pos - 1] == '\\'){
                pos++;
                pos += strcspn(&tag[pos], "'");
            };
            tag[pos] = '\0';
            pos++;
            pos += strspn(&tag[pos], " \t\r\n");
            setv->type = _STRING;
            setv->cons = 1;
        }else{
            i = strspn(&tag[pos], "0123456789");
            f = strspn(&tag[pos], ".0123456789");
            if(f > i){
                setv->type = _DOUBLE;
                setv->cons = 1;
                pos += f;
                if(strspn(&tag[pos], " \t\r\n") > 0){
                    tag[pos] = '\0';
                    pos++;
                };
            }else if(i > 0){
                setv->type = _LONG;
                setv->cons = 1;
                pos += i;
                if(strspn(&tag[pos], " \t\r\n") > 0){
                    tag[pos] = '\0';
                    pos++;
                };
            }else{
                i = strcspn(&tag[pos], "=+-*/%#");
                f = strcspn(&tag[pos], " \t\r\n");
                if(i > f){
                    pos += f;
                    tag[pos] = '\0';
                    pos++;
                    pos += strspn(&tag[pos], " \t\r\n");
                }else{
                    pos += i;
                };
            };
        };
    };
    pos += strcspn(&tag[pos], "=+-*/%,#)");
    if(tag[pos] == '='){ setv->oper = _SETVAL; }
    else if(tag[pos] == '+'){ setv->oper = _ADD; }
    else if(tag[pos] == '-'){ setv->oper = _SUBTRACT; }
    else if(tag[pos] == '*'){ setv->oper = _MULTIPLY; }
    else if(tag[pos] == '/'){ setv->oper = _DIVIDE; }
    else if(tag[pos] == '%'){ setv->oper = _MOD; }
    else if(tag[pos] == ','){ setv->oper = _ALSO;}
    else{ return pos; };
    tag[pos] = '\0';
    pos++;
    setv->next = (user_val_t *)mvp_alloc(p, (sizeof(user_val_t)));
    if(setv->next == NULL) return strcspn(tag, "#");
    pos += parse_set(p, &tag[pos], setv->next);
    return pos;
}

template_cache_t *parse_template(mvp_pool *p, char *tplstr){
    char *nstr = NULL;
    size_t pos;
    template_segment_t *next, *last;
    template_cache_t *tpl = 
        (template_cache_t *)mvp_alloc(p, sizeof(template_cache_t));
    if(tpl == NULL) return NULL;
    tpl->file_content = tplstr;
    tpl->next = NULL;
    tpl->pieces = 
        (template_segment_t *)mvp_alloc(p, sizeof(template_segment_t));
    if(tpl->pieces == NULL) return NULL;
    tpl->pieces->tag = NULL;
    tpl->pieces->follow_text = tplstr;
    tpl->pieces->type = _NOTAG;
    tpl->pieces->next = NULL;
    last = tpl->pieces;

    while((nstr = strstr(tplstr, "<#"))){
        if(nstr == NULL) break;
        pos = 0;
        nstr[pos] = '\0';
        pos += 2;
        next = (template_segment_t *)mvp_alloc(p, sizeof(template_segment_t));
        if(next == NULL) return NULL;
        next->next = NULL;
        next->ifs = NULL;
        pos += strspn(&nstr[pos], " \t\r\n");
        if(strncmp(&nstr[pos], "IF ", 3) == 0 
            || strncmp(&nstr[pos], "if ", 3) == 0){
            next->type = _IF;
            pos += 3;
            next->tag = &nstr[pos];
            next->ifs = (cond_t *)mvp_alloc(p, (sizeof(cond_t)));
            if(next->ifs == NULL) return NULL;
            pos += parse_cond(p, &nstr[pos], next->ifs);
        }else if(strncmp(&nstr[pos], "ELSIF ", 6) == 0 
            || strncmp(&nstr[pos], "elsif ", 6) == 0){
            next->type = _ELSIF;
            pos += 6;
            next->tag = &nstr[pos];
            next->ifs = (cond_t *)mvp_alloc(p, (sizeof(cond_t)));
            if(next->ifs == NULL) return NULL;
            pos += parse_cond(p, &nstr[pos], next->ifs);
        }else if(strncmp(&nstr[pos], "ELSE", 4) == 0 
            || strncmp(&nstr[pos], "else", 4) == 0){
            next->type = _ELSE;
        }else if(strncmp(&nstr[pos], "ENDIF", 5) == 0 
            || strncmp(&nstr[pos], "endif", 5) == 0){
            next->type = _ENDIF;
        }else if(strncmp(&nstr[pos], "LOOP ", 5) == 0 
            || strncmp(&nstr[pos], "loop ", 5) == 0){
            next->type = _LOOP;
            pos += 5;
            pos += strspn(&nstr[pos], " \t\r\n");
            next->tag = &nstr[pos];
            pos += strcspn(&nstr[pos], " \t\r\n#");
            if(nstr[pos] != '#'){
                nstr[pos] = '\0';
                pos++;
            };
        }else if(strncmp(&nstr[pos], "ENDLOOP", 7) == 0 
            || strncmp(&nstr[pos], "endloop", 7) == 0){
            next->type = _ENDLOOP;
        }else if(strncmp(&nstr[pos], "INCLUDE ", 8) == 0 
            || strncmp(&nstr[pos], "include ", 8) == 0){
            next->type = _INCLUDE;
            pos += 8;
            pos += strspn(&nstr[pos], " \t\r\n");
            next->tag = &nstr[pos];
            pos += strcspn(&nstr[pos], " \t\r\n#");
            if(nstr[pos] != '#'){
                nstr[pos] = '\0';
                pos++;
            };
        }else if(strncmp(&nstr[pos], "TEMPLATE", 8) == 0 
            || strncmp(&nstr[pos], "template", 8) == 0){
            next->type = _TEMPLATE;
        }else if(strncmp(&nstr[pos], "SET ", 4) == 0 
            || strncmp(&nstr[pos], "set ", 4) == 0){
            next->type = _SET;
            pos += 4;
            next->tag = &nstr[pos];
            next->sets = (user_val_t *)mvp_alloc(p, (sizeof(user_val_t)));
            if(next->sets == NULL) return NULL;
            pos += parse_set(p, &nstr[pos], next->sets);
        }else{
            next->type = _VALUE;
            next->tag = &nstr[pos];
            pos += strcspn(&nstr[pos], " \t\r\n#");
            if(nstr[pos] != '#'){
                nstr[pos] = '\0';
                pos++;
            };
        };
        pos += strcspn(&nstr[pos], "#");
        nstr[pos] = '\0';
        pos += 2;
        next->follow_text = &nstr[pos];
        last->next = next;
        last = next;
        tplstr = next->follow_text;
    };
    return tpl;
}

const char *scan_tpl_dir(mvp_pool *p, mvproc_config *cfg, const char *subdir){
    DIR *dp;
    struct dirent *ent;
    char mydir[1024];
    strcpy(mydir, cfg->template_dir);
    unsigned int pos = strlen(mydir);
    strcpy(&mydir[pos], subdir);
    char sbdir[1024];
    template_cache_t *tpl;
    template_cache_t *nxt;
    char *fchrs;
    
    dp = opendir(mydir);
    if(dp){
        while((ent = readdir(dp))){
            if(ent->d_name[0] == '.') continue;
            strcpy(sbdir, subdir);
            pos = strlen(sbdir);
            strcpy(&sbdir[pos], ent->d_name);
            pos = strlen(sbdir);
            if(strcmp(&sbdir[pos - 4], ".tpl") == 0){
                sbdir[pos - 4] = '\0';
                tpl = cfg->template_cache;
                while(tpl != NULL && tpl->next != NULL) tpl = tpl->next;
                fchrs = get_file_chars(p, mydir, sbdir);
                if(fchrs != NULL){
                    nxt = parse_template(p, fchrs);
                    nxt->filename = (char *)mvp_alloc(p, strlen(sbdir)+1);
                    strcpy(nxt->filename, sbdir);
                    nxt->next = NULL;
                    if(tpl == NULL){
                        tpl = nxt;
                        cfg->template_cache = tpl;
                    }else tpl->next = nxt;
                };
            }else if(ent->d_type == DT_DIR){
                strcpy(&sbdir[pos], "/");
                scan_tpl_dir(p, cfg, sbdir);
            };
        };
        closedir(dp);
        return NULL;
    }else{
        return "Parser failed mvprocTemplateDir";
    };
}

const char *build_template_cache(mvp_pool *p, mvproc_config *cfg){
    if(cfg->template_dir == NULL || strlen(cfg->template_dir) == 0)
        return "No template directory configured.";
    return scan_tpl_dir(p, cfg, "");
}




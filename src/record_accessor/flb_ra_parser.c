/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019      The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_slist.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_log.h>
#include <fluent-bit/record_accessor/flb_ra_parser.h>

#include "ra_parser.h"
#include "ra_lex.h"

void flb_ra_parser_dump(struct flb_ra_parser *rp)
{
    struct mk_list *head;
    struct flb_ra_key *key;
    struct flb_slist_entry *entry;

    key = rp->key;
    if (rp->type == FLB_RA_PARSER_STRING) {
        printf("type       : STRING\n");
        printf("string     : '%s'\n", key->name);
    }
    else if (rp->type == FLB_RA_PARSER_KEYMAP) {
        printf("type       : KEYMAP\n");
        if (rp->key) {
            printf("key name   : %s\n", key->name);
            mk_list_foreach(head, key->subkeys) {
                entry = mk_list_entry(head, struct flb_slist_entry, _head);
                printf(" - subkey  : %s\n", entry->str);
            }
        }
    }
}

int flb_ra_parser_subkey_add(struct flb_ra_parser *rp, char *key)
{
    int ret;

    ret = flb_slist_add(rp->slist, (const char *) key);
    if (ret == -1) {
        return -1;
    }
    return 0;
}

struct flb_ra_key *flb_ra_parser_key_add(struct flb_ra_parser *rp, char *key)
{
    struct flb_ra_key *k;

    k = flb_malloc(sizeof(struct flb_ra_key));
    if (!k) {
        flb_errno();
        return NULL;
    }

    k->name = flb_sds_create(key);
    if (!k->name) {
        flb_errno();
        flb_free(k);
        return NULL;
    }
    k->subkeys = NULL;

    return k;
}

struct flb_ra_key *flb_ra_parser_string_add(struct flb_ra_parser *rp,
                                            char *str, int len)
{
    struct flb_ra_key *k;

    k = flb_malloc(sizeof(struct flb_ra_key));
    if (!k) {
        flb_errno();
        return NULL;
    }

    k->name = flb_sds_create_len(str, len);
    if (!k->name) {
        flb_errno();
        flb_free(k);
        return NULL;
    }
    k->subkeys = NULL;

    return k;
}

static struct flb_ra_parser *flb_ra_parser_create()
{
    struct flb_ra_parser *rp;

    rp = flb_calloc(1, sizeof(struct flb_ra_parser));
    if (!rp) {
        flb_errno();
        return NULL;
    }
    rp->type = -1;
    rp->key = NULL;
    rp->slist = flb_malloc(sizeof(struct mk_list));
    if (!rp->slist) {
        flb_errno();
        flb_free(rp);
        return NULL;
    }
    mk_list_init(rp->slist);

    return rp;
}

struct flb_ra_parser *flb_ra_parser_string_create(char *str, int len)
{
    struct flb_ra_parser *rp;

    rp = flb_ra_parser_create();
    if (!rp) {
        flb_error("[record accessor] could not create string context");
        return NULL;
    }

    rp->type = FLB_RA_PARSER_STRING;
    rp->key = flb_malloc(sizeof(struct flb_ra_key));
    if (!rp->key) {
        flb_errno();
        flb_ra_parser_destroy(rp);
        return NULL;
    }
    rp->key->name = flb_sds_create_len(str, len);
    if (!rp->key->name) {
        flb_ra_parser_destroy(rp);
        return NULL;
    }
    rp->key->subkeys = NULL;

    return rp;
}

struct flb_ra_parser *flb_ra_parser_meta_create(char *str, int len)
{
    int ret;
    yyscan_t scanner;
    YY_BUFFER_STATE buf;
    flb_sds_t s;
    struct flb_ra_parser *rp;
    struct flb_ra_key *key;

    rp = flb_ra_parser_create();
    if (!rp) {
        flb_error("[record accessor] could not create meta context");
        return NULL;
    }

    /* Temporal buffer of string with fixed length */
    s = flb_sds_create_len(str, len);
    if (!s) {
        flb_errno();
        flb_ra_parser_destroy(rp);
        return NULL;
    }

    /* Flex/Bison work */
    yylex_init(&scanner);
    buf = yy_scan_string(s, scanner);

    ret = yyparse(rp, s, scanner);

    /* release resources */
    flb_sds_destroy(s);
    yy_delete_buffer(buf, scanner);
    yylex_destroy(scanner);

    /* Finish structure mapping */
    if (rp->type == FLB_RA_PARSER_KEYMAP) {
        if (rp->key) {
            key = rp->key;
            key->subkeys = rp->slist;
            rp->slist = NULL;
        }
    }

    if (ret != 0) {
        flb_ra_parser_destroy(rp);
        return NULL;
    }

    return rp;
}

void flb_ra_parser_destroy(struct flb_ra_parser *rp)
{
    struct flb_ra_key *key;

    key = rp->key;
    if (key) {
        flb_sds_destroy(key->name);
        if (key->subkeys) {
            flb_slist_destroy(key->subkeys);
            flb_free(key->subkeys);
        }
        flb_free(rp->key);
    }
    if (rp->slist) {
        flb_slist_destroy(rp->slist);
        flb_free(rp->slist);
    }
    flb_free(rp);
}

/* SDSLib, the C dynamic strings library
 *
 * Copyright (C) 2006,2007 Salvatore Sanfilippo, antirez@gmail.com
 * This softare is released under the following BSD license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SDS_H
#define __SDS_H

#include <sys/types.h>

typedef char *sds;

struct sdshdr {
    long len;
    long free;
    char buf[0];
};

char *sdsnewlen(void *init, size_t initlen);
char *sdsnew(char *init);
size_t sdslen(char *s);
char *sdsdup(char *s);
void sdsfree(char *s);
size_t sdsavail(char *s);
char *sdscatlen(char *s, void *t, size_t len);
char *sdscat(char *s, char *t);
char *sdscpylen(char *s, char *t, size_t len);
char *sdscpy(char *s, char *t);
char *sdscatprintf(char *s, const char *fmt, ...);
char *sdstrim(char *s, const char *cset);
char *sdsrange(char *s, long start, long end);

#endif

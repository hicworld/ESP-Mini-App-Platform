#pragma once
#include "platform.h"
#include <string.h>
#include <ctype.h>
static inline bool valid_id(const char *s) {
    if (!s || strlen(s)<5 || strlen(s)>=APP_ID_LENGTH) return false;
    unsigned dots=0; bool first=true;
    for (; *s; s++) {
        if (*s=='.') { if (first) return false; dots++; first=true; }
        else if ((*s>='a' && *s<='z') || (!first && *s>='0' && *s<='9')) first=false;
        else return false;
    }
    return !first && dots>=2;
}
static inline bool parse_version(const char *s,unsigned value[3]) {
    if (!s) return false;
    for (unsigned part=0;part<3;part++) {
        if (!isdigit((unsigned char)*s)) return false;
        value[part]=0;
        while (isdigit((unsigned char)*s)) {
            if (value[part]>6553 || (value[part]==6553 && *s>'5')) return false;
            value[part]=value[part]*10+(unsigned)(*s-'0'); s++;
        }
        if (part<2) { if (*s++!='.') return false; }
        else if (*s) return false;
    }
    return true;
}

/* Compare two validated semantic versions without relying on lexicographic
 * string order (for example 0.10.0 must be newer than 0.9.0). */
static inline bool compare_versions(const char *left,const char *right,int *result) {
    unsigned a[3],b[3];
    if (!result || !parse_version(left,a) || !parse_version(right,b)) return false;
    *result=0;
    for (unsigned i=0;i<3;i++) {
        if (a[i]<b[i]) { *result=-1; break; }
        if (a[i]>b[i]) { *result=1; break; }
    }
    return true;
}

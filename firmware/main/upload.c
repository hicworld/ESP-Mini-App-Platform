#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
static uint8_t *buffer;
static size_t length,received;
static uint64_t deadline;
static void cancel(void) { free(buffer); buffer=NULL; length=received=0; }
void upload_tick(void) {
    if (buffer && platform_now_ms()>deadline) { cancel(); puts("UPLOAD ERROR timeout"); }
}
static bool number(const char *s,size_t *value,const char **end) {
    if (!s || *s<'0' || *s>'9') return false;
    errno=0; char *tail; unsigned long parsed=strtoul(s,&tail,10);
    if (errno) return false;
    *value=parsed; *end=tail; return true;
}
static int hex(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
bool upload_command(const char *command,const char *argument) {
    if (strncmp(command,"upload_",7)) return false;
    if (!strcmp(command,"upload_abort")) { cancel(); puts("UPLOAD ABORTED"); return true; }
    if (!strcmp(command,"upload_begin")) {
        size_t size; const char *end;
        if (!number(argument,&size,&end) || *end || size<24 || size>APP_MAX_BYTES+APP_MAX_MANIFEST+96 || !apps_storage_ready() || runtime_state()==APP_RUNNING) {
            puts("UPLOAD ERROR begin"); return true;
        }
        cancel(); buffer=malloc(size);
        if (!buffer) { puts("UPLOAD ERROR memory"); return true; }
        length=size; deadline=platform_now_ms()+30000;
        printf("UPLOAD READY %u\n",(unsigned)length); return true;
    }
    if (!buffer) { puts("UPLOAD ERROR no-session"); return true; }
    if (!strcmp(command,"upload_chunk")) {
        size_t offset; const char *data;
        if (!number(argument,&offset,&data) || *data!=' ' || offset!=received) { puts("UPLOAD ERROR offset"); return true; }
        data++; size_t chars=strlen(data),size=chars/2;
        if (!chars || chars%2 || size>128 || size>length-received) { puts("UPLOAD ERROR length"); return true; }
        uint8_t chunk[128];
        for (size_t i=0;i<size;i++) {
            int high=hex(data[2*i]),low=hex(data[2*i+1]);
            if (high<0 || low<0) { puts("UPLOAD ERROR hex"); return true; }
            chunk[i]=(uint8_t)((high<<4)|low);
        }
        memcpy(buffer+received,chunk,size); received+=size; deadline=platform_now_ms()+30000;
        printf("UPLOAD ACK %u\n",(unsigned)received); return true;
    }
    if (!strcmp(command,"upload_commit")) {
        if (received!=length) { puts("UPLOAD ERROR incomplete"); return true; }
        esp_err_t result=apps_install(buffer,length); cancel();
        if (result==ESP_OK) launcher_refresh();
        printf("UPLOAD DONE %s\n",esp_err_to_name(result)); return true;
    }
    puts("UPLOAD ERROR command"); return true;
}

#include "platform.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { double lat,lon; int offset; char name[32]; } location_t;
static location_t location;
static bool configured;
static unsigned generation;
static SemaphoreHandle_t mutex;
static environment_t cached;
static uint64_t received_ms;
static time_t sunrise,sunset,weather_time;
static int daylight_at_fetch=-1;
static esp_err_t last_result=ESP_ERR_INVALID_STATE;
static bool valid_location(const location_t *c) {
    if (!isfinite(c->lat)||!isfinite(c->lon)||c->lat< -90||c->lat>90||c->lon< -180||c->lon>180||c->offset< -720||c->offset>840) return false;
    size_t n=strnlen(c->name,sizeof(c->name));
    if (!n||n>=sizeof(c->name)) return false;
    for (size_t i=0;i<n;i++) if (!((c->name[i]>='A'&&c->name[i]<='Z')||(c->name[i]>='a'&&c->name[i]<='z')||c->name[i]=='-'||c->name[i]=='_')) return false;
    return true;
}
esp_err_t environment_configure(const char *arg) {
    location_t next={0}; char tail;
    if (!mutex || !arg || sscanf(arg,"%lf %lf %d %31s %c",&next.lat,&next.lon,&next.offset,next.name,&tail)!=4 || !valid_location(&next)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t err=nvs_open("environment",NVS_READWRITE,&h);
    if (err!=ESP_OK) return err;
    err=nvs_set_blob(h,"location",&next,sizeof(next)); if (err==ESP_OK) err=nvs_commit(h); nvs_close(h);
    if (err!=ESP_OK) return err;
    xSemaphoreTake(mutex,portMAX_DELAY);
    location=next; configured=true; generation++; memset(&cached,0,sizeof(cached)); received_ms=0; daylight_at_fetch=-1;
    xSemaphoreGive(mutex); return ESP_OK;
}
void environment_snapshot(environment_t *out) {
    memset(out,0,sizeof(*out)); out->daylight=-1;
    if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    *out=cached; out->daylight=-1;
    time_t now=time(NULL);
    /* The user's device starts in Asia/Shanghai (UTC+8). A configured
     * location can supply another explicit offset; no IP geolocation. */
    if (now>=1735689600) {
        out->flags|=1;
        out->seconds=(uint32_t)(((int64_t)now+(configured?location.offset:480)*60)%86400);
    }
    if (configured) {
        out->flags|=8; memcpy(out->location,location.name,sizeof(out->location));
        if (now>=1735689600) {
            if ((out->flags&2) && now>=sunrise-21600 && now<sunset+43200)
                out->daylight=(now>=sunrise && now<sunset)?1:0;
        }
        if (out->flags&2) {
            uint64_t age=(platform_now_ms()-received_ms)/1000;
            if (now>=weather_time && now-weather_time>(time_t)age) age=(uint64_t)(now-weather_time);
            out->weather_age=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
            if (age>3600) out->flags|=4;
            if (age>86400) out->flags&=~2u;
            if (out->daylight<0 && age<1800) out->daylight=daylight_at_fetch;
        }
    }
    xSemaphoreGive(mutex);
    if (garden_input_ready()) out->flags|=16;
}
static bool number(const cJSON *parent,const char *key,double lo,double hi,double *out) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(parent,key);
    if (!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<lo||v->valuedouble>hi) return false;
    *out=v->valuedouble; return true;
}
static esp_err_t fetch_weather(location_t where,unsigned version) {
    char url[384];
    snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,is_day&daily=sunrise,sunset&timezone=auto&timeformat=unixtime&forecast_days=1",where.lat,where.lon);
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=6000,.disable_auto_redirect=true};
    esp_http_client_handle_t h=esp_http_client_init(&cfg); if (!h) return ESP_ERR_NO_MEM;
    char *body=malloc(8193); esp_err_t err=body?esp_http_client_open(h,0):ESP_ERR_NO_MEM;
    int total=0; uint64_t start=platform_now_ms();
    if (err==ESP_OK) {
        int64_t length=esp_http_client_fetch_headers(h);
        if (length<0 || length>8192 || esp_http_client_get_status_code(h)!=200) err=ESP_FAIL;
        while (err==ESP_OK && !esp_http_client_is_complete_data_received(h)) {
            if (total==8192 || platform_now_ms()-start>12000) { err=ESP_ERR_INVALID_SIZE; break; }
            int n=esp_http_client_read(h,body+total,8192-total);
            if (n<0) { err=ESP_FAIL; break; }
            if (!n) { if (!esp_http_client_is_complete_data_received(h)) err=ESP_FAIL; break; }
            total+=n;
        }
    }
    esp_http_client_close(h); esp_http_client_cleanup(h);
    cJSON *json=NULL;
    if (err==ESP_OK) { body[total]=0; json=cJSON_ParseWithLength(body,(size_t)total); if (!json) err=ESP_FAIL; }
    if (err==ESP_OK) {
        cJSON *current=cJSON_GetObjectItemCaseSensitive(json,"current");
        cJSON *daily=cJSON_GetObjectItemCaseSensitive(json,"daily");
        cJSON *rise=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily,"sunrise"),0);
        cJSON *set=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily,"sunset"),0);
        double temperature,code,day,stamp;
        time_t now=time(NULL);
        bool ok=number(current,"temperature_2m",-100,70,&temperature)&&number(current,"weather_code",0,99,&code)&&
            number(current,"is_day",0,1,&day)&&number(current,"time",1735689600,4102444800,&stamp)&&
            code==floor(code)&&day==floor(day)&&fabs((double)now-stamp)<7200&&
            cJSON_IsNumber(rise)&&cJSON_IsNumber(set)&&isfinite(rise->valuedouble)&&isfinite(set->valuedouble)&&
            rise->valuedouble>=1735689600&&set->valuedouble<=4102444800&&rise->valuedouble<set->valuedouble&&
            fabs(rise->valuedouble-(double)now)<172800&&fabs(set->valuedouble-(double)now)<172800;
        if (!ok) err=ESP_ERR_INVALID_RESPONSE;
        else {
            xSemaphoreTake(mutex,portMAX_DELAY);
            if (version==generation) {
                cached.flags=2; cached.temperature=(int32_t)lround(temperature*10); cached.weather=(int32_t)code;
                received_ms=platform_now_ms(); sunrise=(time_t)rise->valuedouble; sunset=(time_t)set->valuedouble;
                weather_time=(time_t)stamp; daylight_at_fetch=(int)day;
            }
            xSemaphoreGive(mutex);
        }
    }
    cJSON_Delete(json); free(body); return err;
}
static void weather_worker(void *unused) {
    (void)unused; uint64_t next=0; unsigned seen=0;
    for (;;) {
        location_t where; unsigned version; bool active;
        xSemaphoreTake(mutex,portMAX_DELAY); where=location; version=generation; active=configured; xSemaphoreGive(mutex);
        uint64_t now=platform_now_ms();
        if (active && network_ready() && time(NULL)>=1735689600 && (version!=seen||now>=next)) {
            esp_err_t result=fetch_weather(where,version); seen=version;
            xSemaphoreTake(mutex,portMAX_DELAY); last_result=result; xSemaphoreGive(mutex);
            next=platform_now_ms()+(result==ESP_OK?900000:120000);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void environment_init(void) {
    mutex=xSemaphoreCreateMutex(); if (!mutex) return;
    nvs_handle_t h;
    if (nvs_open("environment",NVS_READONLY,&h)==ESP_OK) {
        size_t size=sizeof(location);
        configured=nvs_get_blob(h,"location",&location,&size)==ESP_OK && size==sizeof(location)&&valid_location(&location);
        nvs_close(h);
    }
    if (xTaskCreate(weather_worker,"weather",8192,NULL,1,NULL)!=pdPASS) last_result=ESP_ERR_NO_MEM;
}
void environment_status(void) {
    environment_t s; environment_snapshot(&s);
    esp_err_t result=ESP_ERR_INVALID_STATE;
    if (mutex) { xSemaphoreTake(mutex,portMAX_DELAY); result=last_result; xSemaphoreGive(mutex); }
    printf("ENV flags=%lu location=%s seconds=%lu temp_x10=%ld wmo=%ld daylight=%ld age=%lu fetch=%s wifi=%d\n",
        (unsigned long)s.flags,s.location,(unsigned long)s.seconds,(long)s.temperature,(long)s.weather,
        (long)s.daylight,(unsigned long)s.weather_age,esp_err_to_name(result),network_ready());
}

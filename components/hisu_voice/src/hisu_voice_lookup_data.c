#include "hisu_voice_lookup_data.h"
#include "hisu_voice_lookup_json.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <strings.h>

static const cJSON *field(const cJSON *o,const char *key) { return cJSON_GetObjectItemCaseSensitive(o,key); }
static bool number(const cJSON *n,double low,double high) {
    return cJSON_IsNumber(n) && isfinite(n->valuedouble) && n->valuedouble>=low && n->valuedouble<=high;
}
static bool text(const cJSON *n,size_t max) {
    if (!cJSON_IsString(n) || !n->valuestring[0] || strlen(n->valuestring)>max) return false;
    for (const unsigned char *p=(const unsigned char *)n->valuestring;*p;++p) if (*p<32 || *p==127) return false;
    return true;
}
cJSON *lookup_error(const char *code) {
    cJSON *o=cJSON_CreateObject();
    const char *message="Hi Su chưa lấy được thông tin lúc này.";
    if (!strcmp(code,"place_required")) message="Bạn muốn xem thời tiết ở khu vực nào?";
    else if (!strcmp(code,"place_not_found")) message="Hi Su chưa tìm được địa điểm đó. Bạn cho thêm tên thành phố hoặc tỉnh nha.";
    else if (!strcmp(code,"place_ambiguous")) message="Có nhiều nơi trùng tên. Bạn chọn giúp Hi Su địa điểm trong danh sách nha.";
    else if (!strcmp(code,"clock_unsynced")) message="Đồng hồ chưa cập nhật nên Hi Su chưa kiểm tra được thông tin đúng ngày.";
    else if (!strcmp(code,"stale_data")) message="Dữ liệu đang có đã cũ, Hi Su chưa có số liệu mới để trả lời chính xác.";
    else if (!strcmp(code,"rate_limited")) message="Hi Su vừa tra cứu xong, chờ một chút rồi thử lại nha.";
    if (!o || !cJSON_AddBoolToObject(o,"ok",false) || !cJSON_AddStringToObject(o,"error",code) ||
        !cJSON_AddStringToObject(o,"message",message) ||
        !cJSON_AddStringToObject(o,"speech_hint","Chỉ diễn đạt message bằng tiếng Việt tự nhiên. Không đọc JSON, tên trường, mã lỗi hoặc nguồn. Không gọi lại cùng yêu cầu đang lỗi trong lượt này. Không bịa kết quả.")) {
        cJSON_Delete(o); return NULL;
    }
    return o;
}
bool lookup_query_parse(const cJSON *args,lookup_query_t *q) {
    memset(q,0,sizeof(*q));
    const cJSON *topic=field(args,"topic"),*place=field(args,"place"),*day=field(args,"day_offset"),
        *id=field(args,"location_id"),*type=field(args,"gold_type");
    if (!cJSON_IsObject(args) || !text(topic,7)) return false;
    q->weather=!strcmp(topic->valuestring,"weather");
    q->news=!strcmp(topic->valuestring,"news");
    if (q->news) return cJSON_GetArraySize(args)==1;
    if (!q->weather && strcmp(topic->valuestring,"gold")) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item,args) {
        if (!item->string || (strcmp(item->string,"topic") && strcmp(item->string,"place") &&
            strcmp(item->string,"day_offset") && strcmp(item->string,"location_id") && strcmp(item->string,"gold_type"))) return false;
    }
    if (q->weather) {
        if (type || (place && !text(place,96)) || (day && (!number(day,0,2) || day->valuedouble!=day->valueint)) ||
            (id && (!number(id,1,2147483647) || id->valuedouble!=id->valueint))) return false;
        if (place) strcpy(q->place,place->valuestring);
        q->day=day ? day->valueint : 0; q->location_id=id ? id->valueint : 0;
    } else {
        if (place || day || id || (type && !text(type,16))) return false;
        strcpy(q->type,type ? type->valuestring : "summary");
        static const char *types[]={"summary","all","SJL1L10","SJ9999","DOHNL","DOHCML","DOJINHTV",
            "BTSJC","BT9999NTT","PQHNVM","PQHN24NTT","VNGSJC","VIETTINMSJC","XAUUSD"};
        bool found=false;
        for (unsigned i=0;i<sizeof(types)/sizeof(types[0]);++i) if (!strcmp(q->type,types[i])) found=true;
        if (!found) return false;
    }
    return true;
}
static cJSON *data_result(const char *source,int64_t now) {
    cJSON *o=cJSON_CreateObject(); cJSON_AddBoolToObject(o,"ok",true);
    cJSON_AddStringToObject(o,"source",source); cJSON_AddNumberToObject(o,"fetched_at_utc",(double)now);
    return o;
}
cJSON *lookup_gold(const cJSON *root,const lookup_query_t *q,int64_t now) {
    const cJSON *stamp=field(root,"timestamp"),*prices=field(root,"prices");
    if (!cJSON_IsTrue(field(root,"success")) || !number(stamp,1700000000,(double)now+300) ||
        !cJSON_IsObject(prices)) return lookup_error("invalid_source_data");
    if (now-stamp->valuedouble>172800) return lookup_error("stale_data");
    static const struct { const char *code,*name; } types[]={
        {"SJL1L10","SJC 9999"},{"SJ9999","Nhẫn SJC"},{"DOHCML","DOJI HCM"},{"DOHNL","DOJI Hà Nội"},
        {"DOJINHTV","DOJI nữ trang"},{"BTSJC","Bảo Tín SJC"},{"BT9999NTT","Bảo Tín 9999"},
        {"PQHNVM","PNJ Hà Nội"},{"PQHN24NTT","PNJ 24K"},{"VNGSJC","VN Gold SJC"},
        {"VIETTINMSJC","Viettin SJC"},{"XAUUSD","Vàng thế giới"}};
    cJSON *o=data_result("https://www.vang.today/vi/",now),*list=cJSON_AddArrayToObject(o,"prices");
    cJSON_AddNumberToObject(o,"updated_at_utc",stamp->valuedouble);
    cJSON_AddBoolToObject(o,"stale",now-stamp->valuedouble>21600);
    cJSON_AddStringToObject(o,"speech_hint","Giá tham khảo, không phải báo giá giao dịch. Nói rõ thời điểm cập nhật, mua vào/bán ra và đơn vị; stale=true thì nói là dữ liệu cũ, không gọi là giá hiện tại. Không dự đoán giá. Không nhắc tên nguồn trừ khi được hỏi.");
    for (unsigned i=0;i<sizeof(types)/sizeof(types[0]);++i) {
        bool summary=!strcmp(q->type,"summary");
        if (summary ? (i!=0 && i!=1 && i!=7) : (strcmp(q->type,"all") && strcmp(q->type,types[i].code))) continue;
        const cJSON *p=field(prices,types[i].code),*buy=field(p,"buy"),*sell=field(p,"sell"),*currency=field(p,"currency");
        bool world=!strcmp(types[i].code,"XAUUSD");
        if (!text(currency,3) || strcmp(currency->valuestring,world ? "USD" : "VND") ||
            !number(buy,world ? 1 : 1000000,world ? 1000000 : 10000000000.0) ||
            (!world && (!number(sell,buy->valuedouble,10000000000.0)))) {
            cJSON_Delete(o); return lookup_error("invalid_source_data");
        }
        cJSON *row=cJSON_CreateObject();
        if (!row || !cJSON_AddItemToArray(list,row)) { cJSON_Delete(row); cJSON_Delete(o); return NULL; }
        cJSON_AddStringToObject(row,"code",types[i].code); cJSON_AddStringToObject(row,"name",types[i].name);
        cJSON_AddNumberToObject(row,world ? "spot_reference" : "buy",buy->valuedouble);
        if (!world) cJSON_AddNumberToObject(row,"sell",sell->valuedouble);
        cJSON_AddStringToObject(row,"unit",world ? "USD/troy ounce" : "VND/lượng");
        if (cJSON_GetArraySize(row)!=(world ? 4 : 5)) { cJSON_Delete(o); return NULL; }
    }
    int expected=!strcmp(q->type,"all") ? 12 : !strcmp(q->type,"summary") ? 3 : 1;
    if (cJSON_GetArraySize(o)!=7 || cJSON_GetArraySize(list)!=expected) { cJSON_Delete(o); return NULL; }
    return o;
}
const cJSON *lookup_location(const cJSON *root,const lookup_query_t *q,cJSON **error) {
    const cJSON *results=field(root,"results"),*selected=NULL;
    const cJSON *exact=NULL;
    cJSON *choices=cJSON_CreateArray(); unsigned count=0,exact_count=0;
    const cJSON *p;
    cJSON_ArrayForEach(p,results) {
        const cJSON *id=field(p,"id"),*country=field(p,"country_code"),*name=field(p,"name"),*admin=field(p,"admin1");
        if (!number(id,1,2147483647) || id->valuedouble!=id->valueint || !text(country,2) || strcmp(country->valuestring,"VN") ||
            !text(name,96) || !number(field(p,"latitude"),-90,90) || !number(field(p,"longitude"),-180,180)) continue;
        if (++count>3) break;
        if (!strcasecmp(q->place,name->valuestring)) { exact=p; ++exact_count; }
        if ((!q->location_id && count==1) || q->location_id==(int64_t)id->valuedouble) selected=p;
        cJSON *choice=cJSON_CreateObject(); cJSON_AddItemToArray(choices,choice);
        cJSON_AddNumberToObject(choice,"location_id",id->valuedouble); cJSON_AddStringToObject(choice,"name",name->valuestring);
        if (text(admin,96)) cJSON_AddStringToObject(choice,"region",admin->valuestring);
    }
    if (!q->location_id && exact_count==1) selected=exact;
    if (!count || !selected || (!q->location_id && count!=1 && exact_count!=1)) {
        *error=lookup_error(count ? "place_ambiguous" : "place_not_found");
        cJSON_AddItemToObject(*error,"choices",choices); return NULL;
    }
    cJSON_Delete(choices); return selected;
}
static const char *weather_name(int code) {
    switch(code) {
        case 0:return "Trời quang"; case 1:case 2:return "Ít mây"; case 3:return "Nhiều mây";
        case 45:case 48:return "Sương mù"; case 51:case 53:case 55:return "Mưa phùn";
        case 56:case 57:return "Mưa phùn đóng băng"; case 61:case 63:case 65:return "Mưa";
        case 66:case 67:return "Mưa đóng băng"; case 71:case 73:case 75:case 77:return "Tuyết";
        case 80:case 81:case 82:return "Mưa rào"; case 85:case 86:return "Mưa tuyết";
        case 95:return "Dông"; case 96:case 99:return "Dông kèm mưa đá"; default:return NULL;
    }
}
cJSON *lookup_weather(const cJSON *root,const cJSON *location,const lookup_query_t *q,int64_t now) {
    const cJSON *cur=field(root,"current"),*hour=field(root,"hourly"),*units=field(root,"hourly_units");
    const cJSON *times=field(hour,"time"),*temps=field(hour,"temperature_2m"),
        *rain=field(hour,"precipitation"),*prob=field(hour,"precipitation_probability");
    const cJSON *code=field(cur,"weather_code"),*temp=field(cur,"temperature_2m"),*at=field(cur,"time");
    const cJSON *current_units=field(root,"current_units");
    const char *name=number(code,0,99) && code->valuedouble==code->valueint ? weather_name(code->valueint) : NULL;
    if (!name || !number(temp,-90,65) || !text(at,19) || !cJSON_IsArray(times) || !cJSON_IsArray(temps) ||
        !cJSON_IsArray(rain) || !cJSON_IsArray(prob) || cJSON_GetArraySize(times)!=72 ||
        cJSON_GetArraySize(temps)!=72 || cJSON_GetArraySize(rain)!=72 || cJSON_GetArraySize(prob)!=72 ||
        !text(field(units,"precipitation"),4) || strcmp(field(units,"precipitation")->valuestring,"mm") ||
        !text(field(units,"precipitation_probability"),3) || strcmp(field(units,"precipitation_probability")->valuestring,"%") ||
        !text(field(units,"temperature_2m"),4) || strcmp(field(units,"temperature_2m")->valuestring,"°C") ||
        !text(field(current_units,"temperature_2m"),4) || strcmp(field(current_units,"temperature_2m")->valuestring,"°C") ||
        !number(field(root,"utc_offset_seconds"),25200,25200)) return lookup_error("invalid_source_data");
    time_t local=(time_t)(now+25200); struct tm date; char today[11],day[11];
    if (!gmtime_r(&local,&date)) return lookup_error("clock_unsynced");
    strftime(today,sizeof(today),"%Y-%m-%d",&date);
    if (strncmp(at->valuestring,today,10)) return lookup_error("stale_data");
    local+=(time_t)q->day*86400; gmtime_r(&local,&date); strftime(day,sizeof(day),"%Y-%m-%d",&date);
    cJSON *o=data_result("https://open-meteo.com/",now);
    cJSON_AddStringToObject(o,"place",field(location,"name")->valuestring);
    const cJSON *region=field(location,"admin1"); if (text(region,96)) cJSON_AddStringToObject(o,"region",region->valuestring);
    cJSON_AddStringToObject(o,"timezone","Asia/Bangkok"); cJSON_AddStringToObject(o,"forecast_date",day);
    cJSON_AddStringToObject(o,"current_time",at->valuestring); cJSON_AddNumberToObject(o,"temperature_c",temp->valuedouble);
    cJSON_AddStringToObject(o,"conditions",name);
    cJSON *rows=cJSON_AddArrayToObject(o,"hourly");
    for (int i=0;i<72;++i) {
        const cJSON *t=cJSON_GetArrayItem(times,i),*v=cJSON_GetArrayItem(temps,i),*p=cJSON_GetArrayItem(prob,i),*r=cJSON_GetArrayItem(rain,i);
        char expected[17]; snprintf(expected,sizeof(expected),"%sT%02d:00",day,i%24);
        if (i/24!=q->day) continue;
        if (!text(t,16) || strcmp(t->valuestring,expected) || !number(v,-90,65) || !number(p,0,100) || !number(r,0,1000)) {
            cJSON_Delete(o); return lookup_error("invalid_source_data");
        }
        cJSON *row=cJSON_CreateObject();
        if (!row || !cJSON_AddItemToArray(rows,row)) { cJSON_Delete(row); cJSON_Delete(o); return NULL; }
        cJSON_AddNumberToObject(row,"hour",i%24); cJSON_AddNumberToObject(row,"temperature_c",v->valuedouble);
        cJSON_AddNumberToObject(row,"rain_probability_percent",p->valuedouble); cJSON_AddNumberToObject(row,"precipitation_mm",r->valuedouble);
        if (cJSON_GetArraySize(row)!=4) { cJSON_Delete(o); return NULL; }
    }
    cJSON_AddStringToObject(o,"speech_hint","Dự báo theo giờ Việt Nam, không phải chắc chắn. Chỉ tóm tắt khung giờ người dùng hỏi; không dùng thời tiết hiện tại thay dự báo buổi chiều/ngày mai. Nói xác suất mưa, không khẳng định chắc chắn có hoặc không mưa. Không nhắc tên nguồn trừ khi được hỏi.");
    if (cJSON_GetArraySize(o)!=(text(region,96) ? 12 : 11) || cJSON_GetArraySize(rows)!=24) { cJSON_Delete(o); return NULL; }
    return o;
}

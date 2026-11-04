#pragma once
#include "hisu_voice_lookup_data.h"

#define HISU_TECH_QBIT_URL "https://www.qbitai.com/wp-json/wp/v2/posts?per_page=12&_fields=id,date_gmt,link,title,excerpt"
#define HISU_TECH_ITHOME_URL "https://api.ithome.com/json/newslist/news"
#define HISU_TECH_GENK_URL "https://genk.vn/ajax-home/page-1.chn"
#define HISU_TECH_TECHCRUNCH_URL "https://techcrunch.com/wp-json/wp/v2/posts?per_page=6&_fields=id,date_gmt,link,title,excerpt"
#define HISU_TECH_SUMMARY_URL "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent"

cJSON *tech_news_feed(int64_t now,bool repeat);
cJSON *tech_news_candidates(const cJSON *pool,int64_t now,bool repeat);
cJSON *tech_news_candidates_for(const cJSON *pool,int64_t now,bool repeat,const char *source,const cJSON *excluded);
void tech_news_prune(cJSON *pool,int64_t now,bool repeat);
void tech_news_stage(const cJSON *result,uint32_t epoch,uint32_t generation);
void tech_news_spoken(uint32_t epoch,uint32_t generation);
#define HISU_TECH_HISTORY_BYTES (16U+128U*12U)
size_t tech_news_history_export(uint32_t *words,size_t capacity,int64_t now,uint32_t *revision);
bool tech_news_history_import(const uint32_t *words,size_t bytes);
uint32_t tech_news_history_revision(void);
/* provider: 0=IT Home, 1=QbitAI, 2=TechCrunch. */
bool tech_news_append(cJSON *feed,const cJSON *data,unsigned provider,int64_t now);
bool tech_news_append_genk(cJSON *feed,const char *html,int64_t now);
bool tech_news_article_url(const cJSON *item,char *url,size_t capacity);
char *tech_news_article_text(const cJSON *item,const char *body,size_t bytes,bool *truncated);
char *tech_news_request(const lookup_query_t *query,const cJSON *feed);
cJSON *tech_news_result(const cJSON *root,const cJSON *feed);

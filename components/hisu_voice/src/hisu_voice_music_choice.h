#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* A conservative evidence grammar, not an intent classifier. Unknown phrasing
 * asks for clarification instead of granting playback. UTF-8 accents stay intact. */
static inline bool music_choice_normalize(const char *text,char *out,size_t capacity)
{
    size_t used=0; bool space=false;
    for (const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if (isspace(*p)) { space=used!=0; continue; }
        if (used+(space ? 1U : 0U)+1>=capacity) return false;
        if (space) out[used++]=' ';
        space=false; out[used++]=(char)(*p<128 ? tolower(*p) : *p);
    }
    while (used && (out[used-1]=='.' || out[used-1]=='!')) --used;
    out[used]=0; return used!=0;
}
static inline bool music_choice_phrase(const char *transcript,const char *identifier)
{
    char input[160],name[160];
    if (!music_choice_normalize(transcript,input,sizeof(input)) ||
        !music_choice_normalize(identifier,name,sizeof(name))) return false;
    static const char *const prefixes[]={"","chọn ","chon ","mình chọn ","minh chon ","mở ","mo ","phát ","phat ","cho mình nghe ","cho minh nghe "};
    static const char *const suffixes[]={""," đi"," di"," nha"," nhé"," nhe"," giúp mình"," giup minh"};
    for (unsigned p=0;p<sizeof(prefixes)/sizeof(*prefixes);++p) {
        size_t prefix=strlen(prefixes[p]),length=strlen(name);
        if (strncmp(input,prefixes[p],prefix) || strncmp(input+prefix,name,length)) continue;
        for (unsigned s=0;s<sizeof(suffixes)/sizeof(*suffixes);++s)
            if (!strcmp(input+prefix+length,suffixes[s])) return true;
    }
    return false;
}
static inline unsigned music_choice_ordinal(const char *text)
{
    static const char *const labels[3][16]={
        {"bài 1","bài thứ 1","bài thứ nhất","bai 1","bai thu 1","bai thu nhat",
         "bài số 1","bài số một","bài một","bai so 1","bai so mot","bai mot","1","một","số 1","so 1"},
        {"bài 2","bài thứ 2","bài thứ hai","bai 2","bai thu 2","bai thu hai",
         "bài số 2","bài số hai","bài hai","bai so 2","bai so hai","bai hai","2","hai","số 2","so 2"},
        {"bài 3","bài thứ 3","bài thứ ba","bai 3","bai thu 3","bai thu ba",
         "bài số 3","bài số ba","bài ba","bai so 3","bai so ba","bai ba","3","ba","số 3","so 3"}
    };
    for (unsigned i=0;i<3;++i) for (unsigned n=0;n<16;++n)
        if (music_choice_phrase(text,labels[i][n])) return i+1;
    return 0;
}

/* Explicit delegation authorizes any member of the current spoken offer.
 * Match complete phrases so negated/conditional requests cannot grant consent. */
static inline bool music_choice_delegated(const char *text)
{
    static const char *const phrases[]={
        "chọn đại 1 bài", "chọn đại một bài", "chọn đại một bài trong 3 bài đó",
        "chọn đại 1 bài trong 3 bài đó", "chọn đại đi", "bạn chọn đi",
        "chọn giúp mình", "bài nào cũng được", "mở bài nào cũng được",
        "phát bài nào cũng được", "chon dai 1 bai", "chon dai mot bai",
        "chon dai di", "ban chon di", "bai nao cung duoc"
    };
    char input[160],phrase[160];
    if (!music_choice_normalize(text,input,sizeof(input))) return false;
    static const char *const prefixes[]={"", "mình muốn ", "bạn ", "bạn hãy ", "hãy "};
    static const char *const suffixes[]={"", " đi", " nha", " nhé", " giúp mình"};
    for (unsigned i=0;i<sizeof(phrases)/sizeof(*phrases);++i)
        for (unsigned p=0;p<sizeof(prefixes)/sizeof(*prefixes);++p)
            for (unsigned s=0;s<sizeof(suffixes)/sizeof(*suffixes);++s) {
                size_t length=strlen(prefixes[p])+strlen(phrases[i])+strlen(suffixes[s]);
                if (length>=sizeof(phrase)) continue;
                strcpy(phrase,prefixes[p]); strcat(phrase,phrases[i]); strcat(phrase,suffixes[s]);
                if (!strcmp(input,phrase)) return true;
            }
    return false;
}

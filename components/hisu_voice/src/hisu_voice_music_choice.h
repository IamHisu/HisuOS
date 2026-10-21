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
    static const char *const prefixes[]={"","bài ","bai ","chọn ","chon ","mình chọn ","minh chon ","mở ","mo ","phát ","phat ","cho mình nghe ","cho minh nghe "};
    static const char *const suffixes[]={""," đi"," di"," nha"," nhé"," nhe"," giúp mình"," giup minh"," đi bạn"," di ban"," nha bạn"," nha ban"," dùm mình"," dum minh"};
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
    static const char *const labels[3][6]={
        {"bài 1","bài thứ 1","bài thứ nhất","bai 1","bai thu 1","bai thu nhat"},
        {"bài 2","bài thứ 2","bài thứ hai","bai 2","bai thu 2","bai thu hai"},
        {"bài 3","bài thứ 3","bài thứ ba","bai 3","bai thu 3","bai thu ba"}
    };
    for (unsigned i=0;i<3;++i) for (unsigned n=0;n<6;++n)
        if (music_choice_phrase(text,labels[i][n])) return i+1;
    return 0;
}

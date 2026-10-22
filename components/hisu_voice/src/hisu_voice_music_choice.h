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
    static const char *const prefixes[]={"","bài ","bai ","chọn ","chon ","mình chọn ","minh chon ","tui chọn ","tôi chọn ","mở ","mo ","phát ","phat ","cho mình nghe ","cho minh nghe ","cho tui nghe "};
    static const char *const suffixes[]={""," đi"," di"," nha"," nhé"," nhe"," giúp mình"," giup minh"," đi bạn"," di ban"," nha bạn"," nha ban"," dùm mình"," dum minh",
        " á"," á bạn"," á bạn ơi"," đó"," đó bạn"," đó bạn ơi"," nha bạn ơi"," đi bạn ơi"," nè"," nè bạn"," nghen"," nghen bạn"};
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

/* Recover only an explicit numbered, uniquely identified list actually spoken
 * by the player. Search order and the model's tool arguments are not evidence. */
static inline unsigned music_list_marker(const char *p,size_t *bytes)
{
    static const char *const words[]={"một là ","hai là ","ba là "};
    for (unsigned i=0;i<3;++i) if (!strncmp(p,words[i],strlen(words[i]))) {
        *bytes=strlen(words[i]); return i+1;
    }
    const char *start=p;
    if (!strncmp(p,"số ",strlen("số "))) p+=strlen("số ");
    if (p[0]>='1' && p[0]<='9' && (p[1]==':' || (p[1]=='.' && p[2]==' '))) {
        *bytes=(size_t)(p-start)+2; return (unsigned)(p[0]-'0');
    }
    return 0;
}

static inline unsigned music_presented_list(char *text,const char *const *titles,unsigned count,unsigned indices[3])
{
    if (!text || !music_choice_normalize(text,text,2048)) return 0;
    const char *parts[3],*ends[3]; unsigned found=0;
    for (const char *p=text;*p;++p) {
        if (p!=text && p[-1]!=' ' && p[-1]!=';' && p[-1]!='\n') continue;
        size_t bytes=0; unsigned position=music_list_marker(p,&bytes);
        if (!position) continue;
        if (found==3 || position!=found+1) return 0;
        if (found) ends[found-1]=p;
        parts[found++]=p+bytes;
        p+=bytes-1;
    }
    if (found<2) return 0;
    ends[found-1]=text+strlen(text);
    for (unsigned n=0;n<found;++n) {
        const char *p=parts[n];
        while (p<ends[n] && (*p==' ' || *p=='\"')) ++p;
        unsigned matches=0;
        for (unsigned i=0;i<count;++i) {
            char title[160];
            if (!music_choice_normalize(titles[i],title,sizeof(title))) continue;
            size_t length=strlen(title);
            if ((size_t)(ends[n]-p)<length || strncmp(p,title,length)) continue;
            const char *tail=p+length;
            if (*tail=='\"') ++tail;
            if (strncmp(tail," của ",strlen(" của ")) && strncmp(tail," - ",3)) continue;
            ++matches; indices[n]=i;
        }
        if (matches!=1) return 0;
        for (unsigned i=0;i<n;++i) if (indices[i]==indices[n]) return 0;
    }
    return found;
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

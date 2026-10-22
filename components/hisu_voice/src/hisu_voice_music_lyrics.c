#include "hisu_voice_music_lyrics.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

/* IDF 5.5.5's existing WPA crypto routine, also exercised by host known vectors.
 * Only the NCT API-provided lyric key is used; TLS crypto settings are unchanged. */
extern int rc4_skip(const uint8_t *key,size_t keylen,size_t skip,uint8_t *data,size_t bytes);

static int lyric_hex(unsigned char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}

bool hisu_music_lyrics_decode(char *hex,size_t *bytes,const char *key)
{
    if (!hex || !bytes || !key || !*key || strlen(key)>64 || *bytes>2*HISU_MUSIC_LRC_MAX_BYTES) return false;
    size_t out=0; int high=-1;
    for (size_t i=0;i<*bytes;++i) {
        unsigned char c=(unsigned char)hex[i];
        if (c==' ' || c=='\r' || c=='\n' || c=='\t') continue;
        int digit=lyric_hex(c);
        if (digit<0) return false;
        if (high<0) high=digit;
        else { hex[out++]=(char)((high<<4)|digit); high=-1; }
    }
    if (high>=0 || !out || out>HISU_MUSIC_LRC_MAX_BYTES ||
        rc4_skip((const uint8_t *)key,strlen(key),0,(uint8_t *)hex,out)) return false;
    if (memchr(hex,0,out)) return false;
    hex[out]=0; *bytes=out;
    return true;
}

void hisu_music_lyrics_free(hisu_music_lyrics_t *lyrics)
{
    if (!lyrics) return;
    free(lyrics->lines); free(lyrics->text); free(lyrics);
}

static const char *lyric_stamp(const char *p,const char *end,uint32_t *ms)
{
    if (p==end || *p++!='[') return NULL;
    unsigned minutes=0, digits=0, seconds=0, fraction=0, scale=100;
    while (p<end && *p>='0' && *p<='9' && digits<3) { minutes=minutes*10+(unsigned)(*p++-'0'); ++digits; }
    if (!digits || end-p<4 || *p++!=':' || p[0]<'0' || p[0]>'5' || p[1]<'0' || p[1]>'9') return NULL;
    seconds=(unsigned)(p[0]-'0')*10+(unsigned)(p[1]-'0'); p+=2;
    if (p<end && *p=='.') {
        ++p; digits=0;
        while (p<end && *p>='0' && *p<='9' && digits<3) {
            fraction+=(unsigned)(*p++-'0')*scale; scale/=10; ++digits;
        }
        if (!digits) return NULL;
    }
    if (p==end || *p++!=']') return NULL;
    *ms=(minutes*60+seconds)*1000+fraction;
    return p;
}

/* Validate and truncate at a complete UTF-8 character, never split Vietnamese. */
static bool lyric_copy(char *out,const char *p,const char *end,size_t *written)
{
    size_t used=0; bool full=false;
    while (p<end) {
        unsigned char c=(unsigned char)*p;
        unsigned n=c<0x80 ? 1 : c>=0xc2 && c<=0xdf ? 2 : c>=0xe0 && c<=0xef ? 3 : c>=0xf0 && c<=0xf4 ? 4 : 0;
        if (!n || (size_t)(end-p)<n || (c<32 && c!='\t')) return false;
        for (unsigned i=1;i<n;++i) if (((unsigned char)p[i]&0xc0)!=0x80) return false;
        if (n>=3 && ((c==0xe0 && (unsigned char)p[1]<0xa0) || (c==0xed && (unsigned char)p[1]>=0xa0) ||
            (c==0xf0 && (unsigned char)p[1]<0x90) || (c==0xf4 && (unsigned char)p[1]>=0x90))) return false;
        if (used+n>255) full=true;
        if (!full) { if (c=='\t') out[used]=' '; else memcpy(out+used,p,n); used+=n; }
        p+=n;
    }
    out[used]=0; *written=used+1;
    return true;
}

static int lyric_order(const void *a,const void *b)
{
    const music_lyric_line_t *x=a,*y=b;
    if (x->ms!=y->ms) return x->ms<y->ms ? -1 : 1;
    return (int)x->order-(int)y->order;
}

hisu_music_lyrics_t *hisu_music_lyrics_parse(const char *lrc,size_t bytes)
{
    if (!lrc || !bytes || bytes>HISU_MUSIC_LRC_MAX_BYTES || memchr(lrc,0,bytes)) return NULL;
    hisu_music_lyrics_t *out=heap_caps_calloc(1,sizeof(*out),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!out) return NULL;
    out->lines=heap_caps_malloc(HISU_MUSIC_LRC_MAX_LINES*sizeof(*out->lines),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    out->text=heap_caps_malloc(bytes+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!out->lines || !out->text) { hisu_music_lyrics_free(out); return NULL; }
    const char *p=lrc,*end=lrc+bytes;
    if (bytes>=3 && !memcmp(p,"\xef\xbb\xbf",3)) p+=3;
    size_t used=0; long offset=0;
    while (p<end) {
        const char *stop=memchr(p,'\n',(size_t)(end-p)); if (!stop) stop=end;
        const char *trim=stop;
        while (trim>p && (trim[-1]=='\r' || trim[-1]==' ' || trim[-1]=='\t')) --trim;
        while (p<trim && (*p==' ' || *p=='\t')) ++p;
        if (trim-p>9 && !memcmp(p,"[offset:",8)) {
            char number[16]; size_t n=(size_t)(trim-p)-9;
            if (n>=sizeof(number) || trim[-1]!=']') goto invalid;
            memcpy(number,p+8,n); number[n]=0; char *tail;
            offset=strtol(number,&tail,10);
            if (tail==number || *tail || offset<-60000 || offset>60000) goto invalid;
        } else {
            unsigned first=out->count; uint32_t stamp;
            const char *text=p,*next;
            while ((next=lyric_stamp(text,trim,&stamp))!=NULL) {
                if (out->count==HISU_MUSIC_LRC_MAX_LINES) goto invalid;
                out->lines[out->count]=(music_lyric_line_t){stamp,(uint16_t)used,(uint16_t)out->count};
                ++out->count; text=next;
            }
            if (out->count>first) {
                while (text<trim && (*text==' ' || *text=='\t')) ++text;
                size_t copied;
                if (!lyric_copy(out->text+used,text,trim,&copied)) goto invalid;
                used+=copied;
            }
        }
        p=stop<end ? stop+1 : end;
    }
    if (!out->count) goto invalid;
    /* LRC positive offset advances lyric display. Apply after all metadata. */
    for (unsigned i=0;i<out->count;++i) {
        int64_t shifted=(int64_t)out->lines[i].ms-offset;
        out->lines[i].ms=shifted<0 ? 0 : (uint32_t)shifted;
    }
    qsort(out->lines,out->count,sizeof(*out->lines),lyric_order);
    return out;
invalid:
    hisu_music_lyrics_free(out); return NULL;
}

int hisu_music_lyrics_index(const hisu_music_lyrics_t *lyrics,uint64_t position_ms)
{
    if (!lyrics) return -1;
    unsigned low=0,high=lyrics->count;
    while (low<high) {
        unsigned mid=low+(high-low)/2;
        if (lyrics->lines[mid].ms<=position_ms) low=mid+1; else high=mid;
    }
    return (int)low-1;
}

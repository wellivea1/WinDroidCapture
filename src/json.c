#include "json.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
static void ws(const char *s,int *p){while(s[*p]&&isspace((unsigned char)s[*p]))(*p)++;}
static int value(Json *j,int *pos,int depth){
    if(depth>64||j->count>=4096)return -1;ws(j->text,pos);int t=j->count++;JsonToken *tok=&j->tokens[t];tok->start=*pos;char ch=j->text[*pos];tok->type=ch;
    if(ch=='{'||ch=='['){(*pos)++;ws(j->text,pos);char close=ch=='{'?'}':']';if(j->text[*pos]!=close)for(;;){
            if(ch=='{'){if(j->text[*pos]!='"'||value(j,pos,depth+1)<0)return -1;ws(j->text,pos);if(j->text[(*pos)++]!=':')return -1;}
            if(value(j,pos,depth+1)<0)return -1;ws(j->text,pos);if(j->text[*pos]==close)break;if(j->text[(*pos)++]!=',')return -1;ws(j->text,pos);
        }(*pos)++;
    } else if(ch=='"'){(*pos)++;while(j->text[*pos]&&j->text[*pos]!='"'){unsigned char c=(unsigned char)j->text[(*pos)++];if(c<32)return -1;if(c=='\\'){char e=j->text[(*pos)++];if(!e)return -1;if(e=='u'){for(int k=0;k<4;k++)if(!isxdigit((unsigned char)j->text[(*pos)++]))return -1;}else if(!strchr("\"\\/bfnrt",e))return -1;}}if(j->text[(*pos)++]!='"')return -1;
    } else if(ch=='t'||ch=='f'||ch=='n'){const char *lit=ch=='t'?"true":ch=='f'?"false":"null";size_t n=strlen(lit);if(strncmp(j->text+*pos,lit,n))return -1;*pos+=(int)n;
    }else{
        int p=*pos;if(j->text[p]=='-')p++;if(j->text[p]=='0')p++;else{if(j->text[p]<'1'||j->text[p]>'9')return -1;while(isdigit((unsigned char)j->text[p]))p++;}
        if(j->text[p]=='.'){p++;if(!isdigit((unsigned char)j->text[p]))return -1;while(isdigit((unsigned char)j->text[p]))p++;}
        if(j->text[p]=='e'||j->text[p]=='E'){p++;if(j->text[p]=='+'||j->text[p]=='-')p++;if(!isdigit((unsigned char)j->text[p]))return -1;while(isdigit((unsigned char)j->text[p]))p++;}*pos=p;tok->type='0';
    }
    tok->end=*pos;tok->next=j->count;return t;
}
bool json_parse(Json *j,const char *s){j->text=s;j->count=0;int p=0;if(value(j,&p,0)<0)return false;ws(s,&p);return !s[p];}
bool json_equal(const Json *j,int t,const char *s){if(t<0||t>=j->count||j->tokens[t].type!='"')return false;int a=j->tokens[t].start+1,n=j->tokens[t].end-a-1;return (int)strlen(s)==n&&!memcmp(j->text+a,s,(size_t)n);}
int json_get(const Json *j,int o,const char *key){if(o<0||o>=j->count||j->tokens[o].type!='{')return -1;for(int t=o+1;t<j->tokens[o].next;){int v=t+1;if(json_equal(j,t,key))return v;t=j->tokens[v].next;}return -1;}
int json_int(const Json *j,int t,int f){if(t<0||t>=j->count||j->tokens[t].type!='0')return f;double d=json_number(j,t,(double)f);return d>=-2147483647.0&&d<=2147483647.0?(int)d:f;}
double json_number(const Json *j,int t,double f){if(t<0||t>=j->count||j->tokens[t].type!='0')return f;double v=strtod(j->text+j->tokens[t].start,NULL);return isfinite(v)?v:f;}
bool json_bool(const Json *j,int t,bool f){if(t<0||t>=j->count)return f;return j->tokens[t].type=='t'?true:j->tokens[t].type=='f'?false:f;}
static unsigned hex4(const char *p){unsigned v=0;for(int i=0;i<4;i++){char c=p[i];v=v*16+(unsigned)(c<='9'?c-'0':tolower((unsigned char)c)-'a'+10);}return v;}
bool json_string(const Json *j,int t,char *out,size_t cap){
    if(!cap||t<0||t>=j->count||j->tokens[t].type!='"')return false;int p=j->tokens[t].start+1,end=j->tokens[t].end-1;size_t n=0;
#define JP(c) do{if(n+1>=cap)return false;out[n++]=(char)(c);}while(0)
    while(p<end){char c=j->text[p++];if(c!='\\'){JP(c);continue;}c=j->text[p++];
        if(c=='u'){unsigned u=hex4(j->text+p);p+=4;if(u>=0xD800&&u<=0xDBFF){if(p+6>end||j->text[p]!='\\'||j->text[p+1]!='u')return false;unsigned lo=hex4(j->text+p+2);if(lo<0xDC00||lo>0xDFFF)return false;u=0x10000+((u-0xD800)<<10)+lo-0xDC00;p+=6;}else if(u>=0xDC00&&u<=0xDFFF)return false;
            if(u<128){JP(u);}else if(u<2048){JP(0xC0|(u>>6));JP(0x80|(u&63));}else if(u<65536){JP(0xE0|(u>>12));JP(0x80|((u>>6)&63));JP(0x80|(u&63));}else{JP(0xF0|(u>>18));JP(0x80|((u>>12)&63));JP(0x80|((u>>6)&63));JP(0x80|(u&63));}
        }else{switch(c){case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;case 'b':c='\b';break;case 'f':c='\f';break;}JP(c);}}
    out[n]=0;return true;
#undef JP
}
bool json_escape(const char *in,char *out,size_t cap){size_t n=0;if(cap<3)return false;out[n++]='"';for(const unsigned char *p=(const unsigned char *)in;*p;p++){if(n+7>=cap)return false;if(*p=='"'||*p=='\\'){out[n++]='\\';out[n++]=(char)*p;}else if(*p<32){int k=snprintf(out+n,cap-n,"\\u%04x",*p);n+=(size_t)k;}else out[n++]=(char)*p;}out[n++]='"';out[n]=0;return true;}
bool json_raw(const Json *j,int t,char *out,size_t cap){if(t<0||t>=j->count)return false;size_t n=(size_t)(j->tokens[t].end-j->tokens[t].start);if(n>=cap)return false;memcpy(out,j->text+j->tokens[t].start,n);out[n]=0;return true;}

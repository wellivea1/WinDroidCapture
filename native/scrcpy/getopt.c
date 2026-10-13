/* Small exact-name option parser for the managed upstream client. No shell. */
#include "getopt.h"
#include <string.h>
#include <stdio.h>
char *optarg;
int optind=1,opterr=1,optopt;
static const char *next;
int getopt_long(int argc,char *const argv[],const char *shorts,const struct option *opts,int *index){
    optarg=NULL;if(optind==0){optind=1;next=NULL;}
    if(!next||!*next){if(optind>=argc)return -1;const char *arg=argv[optind];
        if(arg[0]!='-'||!arg[1])return -1;if(!strcmp(arg,"--")){optind++;return -1;}
        if(arg[1]=='-'){optind++;const char *name=arg+2,*eq=strchr(name,'=');size_t len=eq?(size_t)(eq-name):strlen(name);int found=-1;
            for(int i=0;opts[i].name;i++)if(strlen(opts[i].name)==len&&!strncmp(opts[i].name,name,len)){found=i;break;}
            if(found<0){if(opterr)fprintf(stderr,"Unknown option: %s\n",arg);return '?';}const struct option *o=&opts[found];if(index)*index=found;
            if(o->has_arg==no_argument&&eq)return '?';if(eq)optarg=(char *)eq+1;
            else if(o->has_arg==required_argument){if(optind>=argc)return ':';optarg=argv[optind++];}
            if(o->flag){*o->flag=o->val;return 0;}return o->val;
        }next=arg+1;optind++;
    }
    int ch=(unsigned char)*next++;const char *spec=strchr(shorts,ch);optopt=ch;if(!spec)return '?';
    if(spec[1]==':'){if(*next){optarg=(char *)next;next=NULL;}else if(spec[2]!=':'){if(optind>=argc)return ':';optarg=argv[optind++];}}
    return ch;
}

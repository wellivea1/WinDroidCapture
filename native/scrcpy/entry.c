#include <stdio.h>
#include <stdbool.h>
#include <string.h>
int awm_upstream_main(int argc,char **argv);
int main(int argc,char **argv){
    bool query=false,pipe=false,headless=false,playback=false;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--version")||!strcmp(argv[i],"--help")||!strcmp(argv[i],"--list-displays")||!strcmp(argv[i],"--list-cameras")||!strcmp(argv[i],"--list-camera-sizes")||!strcmp(argv[i],"--list-encoders"))query=true;
        if(!strncmp(argv[i],"--record=awm-pipe:",18))pipe=true;
        if(!strcmp(argv[i],"--no-window"))headless=true;
        if(!strcmp(argv[i],"--no-playback"))playback=true;
    }
    if(!query&&(!pipe||!headless||!playback)){fprintf(stderr,"AWM bridge refuses capture without --no-window --no-playback and an owned awm-pipe export target.\n");return 2;}
    printf("AWM export protocol 3 / upstream client 5.0.1 / AWM camera API 1 server asset\n");return awm_upstream_main(argc,argv);
}

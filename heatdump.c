#define main bot_main
#include "ai_minimax.c"
#undef main
/* Replay rig moves; score legal alternatives immediately after each candidate. */
int main(int argc,char **argv) {
    if(argc<2 || argc>6) { fprintf(stderr,"Usage: heatdump moves.csv [game=0] [uscale=10] [count-scale=0] [plies=6]\n");return 1; }
    int game=argc>2?atoi(argv[2]):0;
    uscale=argc>3?atoi(argv[3]):10;count_scale=argc>4?atof(argv[4]):0;
    if(uscale<0 || uscale>100 || !(count_scale>=0 && count_scale<=10)) return 1;
    int plies=argc>5?atoi(argv[5]):6;
    if(plies<1 || plies>6) return 1;
    initBoardCaches();initScoringCache();map=createHM(128000,512000);
    FILE *in=fopen(argv[1],"r");if(!in){perror(argv[1]);return 1;}
    char line[512];fgets(line,sizeof line,in);
    Board9 board={.winner=-1};int target=-1,found=0;
    puts("ply,player,row,col,chosen,mark,closed,legal,score,top,local,count,outcome,uscale,count_scale,plies,immediate_score");
    while(fgets(line,sizeof line,in)) {
        int g,ply,p,row,col;
        if(sscanf(line,"%d,%d,%d,%d,%d",&g,&ply,&p,&row,&col)!=5) return 1;
        if(g!=game) continue;
        found=1;
        if(board.winner>=0) {fprintf(stderr,"Moves after terminal board\n");return 1;}
        Moves2 moves={0};
        if(target<0) { for(int y=0;y<9;y++) for(int x=0;x<9;x++) pushMove(&moves,(Pos){x,y}); }
        else validMoves2(&board,&moves,target%3,target/3);
        if(!isLegalMove(col,row,&moves)){fprintf(stderr,"Illegal replay move at ply %d\n",ply);return 1;}
        clearHM(map);
        if(p==0) fprintf(stderr,"Scoring ply %d at %d plies\n",ply,plies);
        if(p==0) for(int y=0;y<9;y++) for(int x=0;x<9;x++) {
            int ci=(y/3)*3+x/3,bit=(y%3)*3+x%3;
            Board3 b=B3(board.cell[ci]);int mark=(b.p[0]&(1<<bit))?1:(b.p[1]&(1<<bit))?2:0;
            int legal=isLegalMove(x,y,&moves),top=0,local=0,count=0,value=0,immediate=0;
            const char *outcome="heuristic";
            if(legal) {
                Board9 after=board;set9(&after,x,y,p);
                int proved=after.winner>=0?after.winner:countProof(after,possibleMasterLines(after));
                int parts[2][3]={{0}};
                for(int q=0;q<2;q++) {
                    int main=f1(after.overall,after.overall_free,q);
                    parts[q][0]=main*uscale;
                    if(!main) parts[q][2]=(int)(fc(after.overall,q)*COUNT_UNIT*count_scale+.5);
                    for(int i=0;i<9;i++) if(!(after.overall_free&(1<<i)))
                        parts[q][1]+=f1(after.cell[i],0,q)*f2(after.overall,after.overall_free,i,q);
                }
                top=parts[p][0]-parts[1-p][0];local=parts[p][1]-parts[1-p][1];count=parts[p][2]-parts[1-p][2];
                value=top+local+count;
                if(proved>=0) {value=proved==2?0:proved==p?TERMINAL_SCORE:-TERMINAL_SCORE;outcome=proved==2?"draw":proved==p?"proven win":"proven loss";}
                immediate=value;
                u16 search_cell,search_bit;pos2cell((Pos){x,y},&search_cell,&search_bit);
                Score searched=evaluateShallow(board,p,search_cell,search_bit,plies-1,0);
                value=(int)searched.p0-searched.p1;
                outcome=value==TERMINAL_SCORE?"proven win":value==-TERMINAL_SCORE?"proven loss":"searched estimate";
            }
            printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%.6g,%d,%d\n",ply,p,y,x,x==col&&y==row,mark,!!(board.overall_free&(1<<ci)),legal,value,top,local,count,outcome,uscale,count_scale,plies,immediate);
        }
        set9(&board,col,row,p);target=(row%3)*3+col%3;
    }
    fclose(in);if(!found){fprintf(stderr,"Game not found\n");return 1;}return 0;
}

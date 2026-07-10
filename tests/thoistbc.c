#include <stdio.h>
#define W 8
static int sliding_max(const int a[],int n,int k,int out[]){int dq[W],head=0,tail=0,count=0,i;for(i=0;i<n;++i){int limit=i-k+1;while(head<tail&&dq[head]<limit)++head;while(head<tail&&a[dq[tail-1]]<=a[i])--tail;dq[tail++]=i;if(i>=k-1)out[count++]=a[dq[head]];}return count;}
int main(void){int a[8]={1,3,-1,-3,5,3,6,7},out[6];int n=sliding_max(a,8,3,out);printf("thoistbc slmax=%d,%d,%d,%d\n",n,out[0],out[2],out[5]);return !(n==6&&out[0]==3&&out[2]==5&&out[5]==7);}
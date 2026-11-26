/* Shared row offsets across global arrays. Includes a skipped loop,
 * repeated calls, differing row indices, volatile indices and a call
 * between row calculations. Initialized and BSS arrays share the module. */
#include <stdio.h>
int A[3][20] = {{1}};
int B[3][20], C[3][20];
float D[3][20] = {{1.0f}};
float E[3][20], F[3][20];
volatile int vi;
int calls;

int fetch(int i,int j) { calls++; return A[i][j]; }

void rows(int enabled)
{
    int i,j;
    if(!enabled) return;
    for(i=0;i<3;i++) for(j=0;j<20;j++) C[i][j]+=A[i][j]*B[i][j];
}
void realrows(int enabled)
{
    int i,j;
    if(!enabled) return;
    for(i=0;i<3;i++) for(j=0;j<20;j++) F[i][j]+=D[i][j]*E[i][j];
}
void shifted(void)
{
    int i,j;
    for(i=0;i<3;i++) for(j=0;j<20;j++) C[i][j]+=A[(i+1)%3][j]*B[i][j];
}
void volrows(void)
{
    int i,j;
    for(i=0;i<3;i++) for(j=0;j<20;j++) {
        vi=i;
        C[vi][j]+=A[vi][j]*B[vi][j];
    }
}
void called(void)
{
    int i,j;
    for(i=0;i<3;i++) for(j=0;j<20;j++) C[i][j]+=fetch(i,j)*B[i][j];
}
int main(void)
{
    int i,j,total;
    float realtotal;
    for(i=0;i<3;i++) for(j=0;j<20;j++) {
        A[i][j]=i+j-5; B[i][j]=2*i-j; C[i][j]=i-j;
        D[i][j]=(float)A[i][j]; E[i][j]=(float)B[i][j]; F[i][j]=(float)C[i][j];
    }
    rows(0); realrows(0);
    rows(1); rows(1); realrows(1); realrows(1);
    shifted(); volrows(); called();
    total=0; realtotal=0.0f;
    for(i=0;i<3;i++) for(j=0;j<20;j++) {
        total+=C[i][j]; realtotal+=F[i][j];
    }
    printf("integer=%d float=%.0f calls=%d\n",total,realtotal,calls);
    return 0;
}

/* incone_test.c
   C-powered InCone test for visualization
*/
#include <stdio.h>
#include <stdlib.h>

#define X 0
#define Y 1
typedef int tPointi[2];
typedef enum { FALSE, TRUE } bool;

/* ---------- Chapter 1 Predicates ---------- */
int Area2(tPointi a, tPointi b, tPointi c) {
    return (b[X]-a[X])*(c[Y]-a[Y]) - (c[X]-a[X])*(b[Y]-a[Y]);
}

bool Left(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) > 0; }
bool LeftOn(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) >= 0; }
bool Collinear(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) == 0; }

/* ---------- InCone from the book ---------- */
bool InCone(int a_idx, int b_idx, tPointi *V, int n)
{
    int a_prev = (a_idx - 1 + n) % n;
    int a_next = (a_idx + 1) % n;

    tPointi a, a0, a1, b;

    /* Copy points correctly */
    a[X]  = V[a_idx][X];  a[Y]  = V[a_idx][Y];
    a0[X] = V[a_prev][X]; a0[Y] = V[a_prev][Y];
    a1[X] = V[a_next][X]; a1[Y] = V[a_next][Y];
    b[X]  = V[b_idx][X];  b[Y]  = V[b_idx][Y];

    /* a is convex */
    if (LeftOn(a, a1, a0)) {
        return Left(a, b, a0) && Left(b, a, a1);
    }
    /* a is reflex */
    else {
        return !(LeftOn(a, b, a1) && LeftOn(b, a, a0));
    }
}

/* ---------- MAIN ---------- */
int main() {
    int n;
    scanf("%d", &n);

    tPointi *V = malloc(sizeof(tPointi) * n);

    for (int i = 0; i < n; i++) {
        scanf("%d %d", &V[i][X], &V[i][Y]);
        printf("VERTEX %d %d %d\n", i, V[i][X], V[i][Y]);
    }

    int a, b;
    scanf("%d %d", &a, &b);
    printf("CHECK %d %d\n", a, b);

    bool result = InCone(a, b, V, n);

    printf("INCONE %s\n", result ? "TRUE" : "FALSE");
    printf("DONE\n");

    free(V);
    return 0;
}

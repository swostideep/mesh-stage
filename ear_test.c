#include <stdio.h>
#include <stdlib.h>

#define X 0
#define Y 1
typedef int tPointi[2];
typedef enum { FALSE, TRUE } bool;

/* -------- Geometry predicates from book -------- */
int Area2(tPointi a, tPointi b, tPointi c) {
    return (b[X]-a[X])*(c[Y]-a[Y]) - (c[X]-a[X])*(b[Y]-a[Y]);
}
bool Left(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) > 0; }
bool LeftOn(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) >= 0; }
bool Collinear(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) == 0; }

bool Xor(bool x, bool y) { return (!x) ^ (!y); }

bool Between(tPointi a, tPointi b, tPointi c) {
    if (!Collinear(a,b,c)) return FALSE;
    if (a[X] != b[X])
        return ((a[X] <= c[X] && c[X] <= b[X]) ||
                (a[X] >= c[X] && c[X] >= b[X]));
    else
        return ((a[Y] <= c[Y] && c[Y] <= b[Y]) ||
                (a[Y] >= c[Y] && c[Y] >= b[Y]));
}

bool IntersectProp(tPointi a, tPointi b, tPointi c, tPointi d) {
    if (Collinear(a,b,c) || Collinear(a,b,d) ||
        Collinear(c,d,a) || Collinear(c,d,b))
        return FALSE;
    return Xor(Left(a,b,c),Left(a,b,d)) && Xor(Left(c,d,a),Left(c,d,b));
}

bool Intersect(tPointi a, tPointi b, tPointi c, tPointi d) {
    if (IntersectProp(a,b,c,d)) return TRUE;
    if (Between(a,b,c) || Between(a,b,d) || Between(c,d,a) || Between(c,d,b))
        return TRUE;
    return FALSE;
}

/* -------- Diagonal testing -------- */
bool Diagonalie(int ai, int bi, tPointi *V, int n) {
    for (int i=0; i<n; i++) {
        int j = (i+1) % n;
        if (i==ai || i==bi || j==ai || j==bi) continue;
        if (Intersect(V[ai], V[bi], V[i], V[j])) return FALSE;
    }
    return TRUE;
}

bool InCone(int a, int b, tPointi *V, int n) {
    int a_prev = (a - 1 + n) % n;
    int a_next = (a + 1) % n;

    tPointi A, A0, A1, B;

    A[X]  = V[a][X];  A[Y] = V[a][Y];
    A0[X] = V[a_prev][X]; A0[Y] = V[a_prev][Y];
    A1[X] = V[a_next][X]; A1[Y] = V[a_next][Y];
    B[X]  = V[b][X];  B[Y] = V[b][Y];

    if (LeftOn(A, A1, A0)) {
        return Left(A, B, A0) && Left(B, A, A1);
    } else {
        return !(LeftOn(A, B, A1) && LeftOn(B, A, A0));
    }
}

bool Diagonal(int a, int b, tPointi *V, int n) {
    return InCone(a,b,V,n) && InCone(b,a,V,n) && Diagonalie(a,b,V,n);
}

/* -------- MAIN: Ear detection -------- */
int main() {
    int n;
    scanf("%d", &n);
    tPointi *V = malloc(sizeof(tPointi) * n);

    for (int i=0; i<n; i++) {
        scanf("%d %d", &V[i][X], &V[i][Y]);
        printf("VERTEX %d %d %d\n", i, V[i][X], V[i][Y]);
    }

    printf("BEGIN_EARS\n");

    for (int i=0; i<n; i++) {
        int v0 = (i - 1 + n) % n;
        int v2 = (i + 1) % n;

        bool isEar = Diagonal(v0, v2, V, n);

        printf("EAR %d %s %d %d\n",
               i,
               isEar ? "TRUE" : "FALSE",
               v0, v2);
    }

    printf("DONE\n");
    return 0;
}

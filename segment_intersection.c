#include <stdio.h>
#include <stdlib.h>

#define X 0
#define Y 1
typedef int tPointi[2];
typedef enum { FALSE, TRUE } bool;

/* --- Same geometry predicates as triangulate.c --- */
int Area2(tPointi a, tPointi b, tPointi c) {
    return (b[X]-a[X])*(c[Y]-a[Y]) - (c[X]-a[X])*(b[Y]-a[Y]);
}

bool Left(tPointi a, tPointi b, tPointi c) {
    return Area2(a,b,c) > 0;
}

bool Collinear(tPointi a, tPointi b, tPointi c) {
    return Area2(a,b,c) == 0;
}

bool Xor(bool x, bool y) {
    return (!x) ^ (!y);
}

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

    return Xor(Left(a,b,c), Left(a,b,d)) &&
           Xor(Left(c,d,a), Left(c,d,b));
}

bool Intersect(tPointi a, tPointi b, tPointi c, tPointi d) {
    if (IntersectProp(a,b,c,d)) return TRUE;
    if (Between(a,b,c) || Between(a,b,d) ||
        Between(c,d,a) || Between(c,d,b))
        return TRUE;
    return FALSE;
}

/* ---------------------------------------------- */

int main() {
    int n;
    scanf("%d", &n);

    if (n != 4) {
        printf("ERROR Must provide exactly 4 points\n");
        return 1;
    }

    tPointi A, B, C, D;

    scanf("%d %d", &A[X], &A[Y]);
    scanf("%d %d", &B[X], &B[Y]);
    scanf("%d %d", &C[X], &C[Y]);
    scanf("%d %d", &D[X], &D[Y]);

    /* Print for Python visualiser */
    printf("POINT A %d %d\n", A[X], A[Y]);
    printf("POINT B %d %d\n", B[X], B[Y]);
    printf("POINT C %d %d\n", C[X], C[Y]);
    printf("POINT D %d %d\n", D[X], D[Y]);

    printf("SEGMENT AB A B\n");
    printf("SEGMENT CD C D\n");

    bool hit = Intersect(A, B, C, D);

    printf("INTERSECT %s\n", hit ? "TRUE" : "FALSE");
    printf("DONE\n");

    return 0;
}

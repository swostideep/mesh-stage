#include <stdio.h>
#include <stdlib.h>

#define X 0
#define Y 1
typedef int tPointi[2];
typedef enum { FALSE, TRUE } bool;

/* ---------- Predicates (same as triangulate.c) ---------- */
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

/* ---------- Diagonal and InCone from triangulate.c ---------- */
typedef struct vertex {
    int idx;
    tPointi v;
} Vertex;

bool InCone(Vertex *a, Vertex *b, Vertex *vertices, int n) {
    int a_prev = (a->idx - 1 + n) % n;
    int a_next = (a->idx + 1) % n;

    Vertex *a0 = &vertices[a_prev];
    Vertex *a1 = &vertices[a_next];

    if (LeftOn(a->v, a1->v, a0->v)) {
        return Left(a->v, b->v, a0->v) &&
               Left(b->v, a->v, a1->v);
    } else {
        return !(LeftOn(a->v, b->v, a1->v) &&
                 LeftOn(b->v, a->v, a0->v));
    }
}

bool Diagonalie(Vertex *a, Vertex *b, Vertex *vertices, int n) {
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;

        if (vertices[i].idx == a->idx || vertices[i].idx == b->idx ||
            vertices[j].idx == a->idx || vertices[j].idx == b->idx)
            continue;

        if (Intersect(a->v, b->v, vertices[i].v, vertices[j].v))
            return FALSE;
    }
    return TRUE;
}

bool Diagonal(Vertex *a, Vertex *b, Vertex *vertices, int n) {
    return InCone(a, b, vertices, n) &&
           InCone(b, a, vertices, n) &&
           Diagonalie(a, b, vertices, n);
}

/* ---------- Main Program ---------- */

int main() {
    int n;
    scanf("%d", &n);

    Vertex *V = malloc(sizeof(Vertex) * n);

    for (int i = 0; i < n; i++) {
        V[i].idx = i;
        scanf("%d %d", &V[i].v[X], &V[i].v[Y]);
        printf("VERTEX %d %d %d\n", i, V[i].v[X], V[i].v[Y]);
    }

    int a, b;
    scanf("%d %d", &a, &b);

    printf("CHECK %d %d\n", a, b);

    bool isdiag = Diagonal(&V[a], &V[b], V, n);

    printf("DIAGONAL %s\n", isdiag ? "TRUE" : "FALSE");
    printf("DONE\n");

    return 0;
}

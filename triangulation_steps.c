#include <stdio.h>
#include <stdlib.h>

#define X 0
#define Y 1
typedef int tPointi[2];
typedef enum { FALSE, TRUE } bool;

/* ========== GEOMETRY PREDICATES ========== */
int Area2(tPointi a, tPointi b, tPointi c) {
    return (b[X]-a[X])*(c[Y]-a[Y]) - (c[X]-a[X])*(b[Y]-a[Y]);
}
bool Left(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) > 0; }
bool LeftOn(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) >= 0; }
bool Collinear(tPointi a, tPointi b, tPointi c) { return Area2(a,b,c) == 0; }

bool Xor(bool x,bool y){ return (!x) ^ (!y); }

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
    if (Collinear(a,b,c)||Collinear(a,b,d)||Collinear(c,d,a)||Collinear(c,d,b))
        return FALSE;
    return Xor(Left(a,b,c),Left(a,b,d)) && Xor(Left(c,d,a),Left(c,d,b));
}
bool Intersect(tPointi a, tPointi b, tPointi c, tPointi d) {
    if (IntersectProp(a,b,c,d)) return TRUE;
    if (Between(a,b,c)||Between(a,b,d)||Between(c,d,a)||Between(c,d,b))
        return TRUE;
    return FALSE;
}

/* ========== DOUBLY LINKED LIST VERTEX STRUCT ========== */
typedef struct Vertex {
    int idx;           // original vertex id
    tPointi v;         // coordinates
    int ear;           // TRUE/FALSE
    struct Vertex *next, *prev;
} Vertex;

Vertex *vertices = NULL;

/* Add vertex to circular list */
void AddVertex(Vertex **head, Vertex *v) {
    if (*head == NULL) {
        v->next = v->prev = v;
        *head = v;
    } else {
        Vertex *last = (*head)->prev;
        last->next = v;
        v->prev = last;
        v->next = *head;
        (*head)->prev = v;
    }
}

/* ========== DIAGONAL, INCONE, ETC ========== */
bool InCone(Vertex *a, Vertex *b) {
    Vertex *a0 = a->prev;
    Vertex *a1 = a->next;
    tPointi A,B,A0,A1;

    A[X]=a->v[X]; A[Y]=a->v[Y];
    A0[X]=a0->v[X]; A0[Y]=a0->v[Y];
    A1[X]=a1->v[X]; A1[Y]=a1->v[Y];
    B[X]=b->v[X];  B[Y]=b->v[Y];

    if (LeftOn(A,A1,A0))
        return Left(A,B,A0) && Left(B,A,A1);
    else
        return !(LeftOn(A,B,A1) && LeftOn(B,A,A0));
}

bool Diagonalie(Vertex *a, Vertex *b) {
    Vertex *c = vertices;
    do {
        Vertex *c1 = c->next;
        if ( c!=a && c1!=a && c!=b && c1!=b ) {
            if (Intersect(a->v,b->v,c->v,c1->v)) return FALSE;
        }
        c = c->next;
    } while (c != vertices);
    return TRUE;
}

bool Diagonal(Vertex *a, Vertex *b) {
    return InCone(a,b) && InCone(b,a) && Diagonalie(a,b);
}

/* Mark all ears */
void EarInit() {
    Vertex *v = vertices;
    do {
        Vertex *v0 = v->prev;
        Vertex *v2 = v->next;
        v->ear = Diagonal(v0, v2);
        printf("EAR %d %s %d %d\n",
               v->idx,
               v->ear ? "TRUE" : "FALSE",
               v0->idx, v2->idx);
        v = v->next;
    } while (v != vertices);
}

/* ========== TRIANGULATION ALGORITHM ========== */
void Triangulate(int n) {
    EarInit();  
    int count = n;

    while (count > 3) {
        Vertex *v = vertices;

        do {
            if (v->ear) {
                Vertex *v1 = v->prev;
                Vertex *v3 = v->next;

                // print diagonal
                printf("DIAGONAL %d %d\n", v1->idx, v3->idx);

                // remove ear
                printf("REMOVE %d\n", v->idx);

                // unlink
                v1->next = v3;
                v3->prev = v1;

                vertices = v3;
                count--;

                // recompute earity for affected neighbors
                Vertex *v0 = v1->prev;
                Vertex *v4 = v3->next;

                v1->ear = Diagonal(v0, v3);
                v3->ear = Diagonal(v1, v4);

                free(v);
                break;
            }
            v = v->next;
        } while (v != vertices);
    }

    printf("DONE\n");
}


/* ========== MAIN PROGRAM ========== */
int main() {
    int n;
    scanf("%d",&n);

    for (int i=0;i<n;i++){
        Vertex *v = malloc(sizeof(Vertex));
        v->idx = i;
        scanf("%d %d", &v->v[X], &v->v[Y]);
        printf("VERTEX %d %d %d\n", i, v->v[X], v->v[Y]);
        AddVertex(&vertices,v);
    }

    printf("BEGIN\n");
    Triangulate(n);
    return 0;
}

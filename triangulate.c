// // point structure
// #define X 0
// #define Y 1
// typedef enum
// {
//     FALSE,
//     TRUE
// } bool;
// #define DIM 2
// typedef int tPointi[DIM];

// // vertex structure
// typedef struct tVertexStructure tsVertex;
// typedef tsVertex *tVertex;
// struct tVertexstructure
// {
//     int vnum;
//     tPointi v;
//     bool ear;
//     tVertex next, prev;
// };
// tVertex vertices = NULL;
// // loop to process all vertices
// tVertex v;
// v = vertices;
// do
// {
//     v = v->next;
// } while (v != vertices);
// // NEW and ADD macros. (The backslashes continue the lines so that the preprocessor does not treat those as command lines.)
// #define EXIT FAILURE 1
// char *malloc();

// #define NEW(p, type)                                  \
//     if (((p) = (type *)malloc(sizeof(type))) == NULL) \
//     {                                                 \
//         fprintf(stderr, "Insufficient memory\n");     \
//         exit(EXIT);                                   \
//     }
// #define ADD(head, p)                 \
//     if (head)                        \
//     {                                \
//         p->next = head;              \
//         p->prev = head->prev;        \
//         head->prev = p;              \
//         p->prev->next = P;           \
//     }                                \
//     else                             \
//     {                                \
//         head = P;                    \
//         head->next = head->prev = p; \
//     }
// #define FREE(p)          \
//     if (p)               \
//     {                    \
//         free((char *)p); \
//         p = NULL;        \
//     }

// int Area2(tPointi a, tPointi b, tPointi c)
// {
//     return (b[X] - a[X]) * (c[Y] - a[Y]) -
//            (c[X] - a[X]) * (b[Y] - a[Y]);
// }

// int AreaPoly2(void)
// {
//     int sum = 0;
//     tVertex p, a;

//     p = vertices; /* Fixed. */
//     a = p->next;  /* Moving. */

//     do
//     {
//         sum += Area2(p->v, a->v, a->next->v);
//         a = a->next;
//     } while (a->next != vertices);

//     return sum;
// }

// bool Left(tPointi a, tPointi b, tPointi c)
// {
//     return Area2(a, b, c) > 0;
// }
// bool LeftOn(tPointi a, tPointi b, tPointi c)
// {
//     return Area2(a, b, c) >= 0;
// }
// bool Collinear(tPointi a, tPointi b, tPointi c)
// {
//     return Area2(a, b, c) == 0;
// }

// bool IntersectProp(tPointi a, tPointi b, tPointi c, tPointi d)
// {
//     if (Collinear(a, b, c) || Collinear(a, b, d) ||
//         Collinear(c, d, a) || Collinear(c, d, b))
//         return FALSE;

//     return Xor(Left(a, b, c), Left(a, b, d)) &&
//            Xor(Left(c, d, a), Left(c, d, b));
// }
// bool Xor(bool x, bool y)
// {
//     return !x ^ !y;
// }
// bool Between(tPointi a, tPointi b, tPointi c)
// {
//     tPointi ba, ca;

//     if (!Collinear(a, b, c))
//         return FALSE;

//     if (a[X] != b[X])
//         return ((a[X] <= c[X]) && (c[X] <= b[X])) ||
//                ((a[X] >= c[X]) && (c[X] >= b[X]));
//     else
//         return ((a[Y] <= c[Y]) && (c[Y] <= b[Y])) ||
//                ((a[Y] >= c[Y]) && (c[Y] >= b[Y]));
// }

// // finding a diagonal od polygon
// bool Intersect(tPointi a, tPointi b, tPointi c, tPointi d)
// {
//     if (IntersectProp(a, b, c, d))
//         return TRUE;
//     else if (Between(a, b, c) ||
//              Between(a, b, d) ||
//              Between(c, d, a) ||
//              Between(c, d, b))
//         return TRUE;
//     else
//         return FALSE;
// }

// bool Diagonalie(tVertex a, tVertex b)
// {
//     tVertex c, c1;
//     /* For each edge (c,cl) ofP */
//     c = vertices;
//     do
//     {
//         c1 = c->next;
//         /* Skip edges incident to a or b */
//         if ((c != a) && (c1 != a) && (c != b) && (c1 != b) && Intersect(a->v, b->v, c->v, c1->v))
//             return FALSE;
//         c = c->next;
//     } while (c != vertices);
//     return TRUE;
// }
// bool InCone(tVertex a, tVertex b)
// {
//     tVertex a0, a1;
//     a0 = a->prev;
//     a1 = a->next;
//     if (LeftOn(a->v, a1->v, a0->v))
//         return Left(a->v, b->v, a0->v) && Left(b->v, a->v, a1->v);
//     else
//         return !(LeftOn(a->v, b->v, a1->v) && LeftOn(b->v, a->v, a0->v));
// }

// bool Diagonal(tVertex a, tVertex b)
// {
//     return InCone(a, b) && InCone(b, a) && Diagonalie(a, b);
// }
// void EarInit(void)
// {
//     tVertex v0, v1, v2;
//     v1 = vertices;
//     do
//     {
//         v2 = v1->next;
//         v0 = v1->prev;
//         v1->ear = Diagonal(v0, v2);
//         v1 = v1->next;
//     } while (v1 != vertices);
// }
// void Triangulate(void){
//     tVertex v0,v1, v2, v3, v4;
//     int n = nvertices;
//     /* five consecutive vertices */
//     /* number ofvertices; shrinks to 3. */
//     EarInit();
//     /* Each step ofouter loop removes one ear. */
//     while (n > 3)
//     {
//         /* Inner loop searches for an ear. */
//         v2 = vertices;
//         do{
//             if (v2->ear)
//             {
//                 /* Ear found. Fill variables. */
//                 v3 = v2->next;
//                 v4 = v3->next;
//                 v1 = v2->prev;
//                 v0 = v1->prev;
//                  /* (vl,v3) is a diagonal */
//                 PrintDiagonal(v1, v3);
//                 /* Update earity ofdiagonal endpoints */
//                 v1->ear = Diagonal(v0, v3);
//                 v3->ear = Diagonal(v1, v4);
//                 /* Cut off the ear v2 */
//                 v1->next = v3;
//                 v3->prev = v1;
//                 vertices = v3;
//                 n--;
//                 break;
//             } /* end ifear found */
//             v2 = v2->next;
//         } while (v2 != vertices);
//     } /* end outer while loop */
//     /* In case the head was v2. */
//     /* out ofinner loop; resume outer loop */
// }

// int main()
// {
//     ReadVertices();
//     PrintVertices();
//     Triangulate();
//     return 0;
// }

#include <stdio.h>
#include <stdlib.h>

#define X 0
#define Y 1
typedef enum
{
    FALSE,
    TRUE
} bool;
#define DIM 2
typedef int tPointi[DIM];

/*************** VERTEX STRUCTURE ****************/

typedef struct tVertexStructure tSVertex;
typedef tSVertex *tVertex;

struct tVertexStructure
{
    int vnum;
    tPointi v;
    bool ear;
    tVertex next, prev;
};

tVertex vertices = NULL;
int nvertices;

/*************** MEMORY MACROS FROM BOOK ****************/

#define EXIT_FAILURE_CODE 1

#define NEW(p, type)                                  \
    if (((p) = (type *)malloc(sizeof(type))) == NULL) \
    {                                                 \
        fprintf(stderr, "Insufficient memory\n");     \
        exit(EXIT_FAILURE_CODE);                      \
    }

#define ADD(head, p)                 \
    if (head)                        \
    {                                \
        p->next = head;              \
        p->prev = head->prev;        \
        head->prev->next = p;        \
        head->prev = p;              \
    }                                \
    else                             \
    {                                \
        head = p;                    \
        head->next = head->prev = p; \
    }

#define FREE(p)   \
    if (p)        \
    {             \
        free(p);  \
        p = NULL; \
    }

/*************** GEOMETRIC PRIMITIVES ****************/

int Area2(tPointi a, tPointi b, tPointi c)
{
    return (b[X] - a[X]) * (c[Y] - a[Y]) -
           (c[X] - a[X]) * (b[Y] - a[Y]);
}

bool Left(tPointi a, tPointi b, tPointi c)
{
    return Area2(a, b, c) > 0;
}

bool LeftOn(tPointi a, tPointi b, tPointi c)
{
    return Area2(a, b, c) >= 0;
}

bool Collinear(tPointi a, tPointi b, tPointi c)
{
    return Area2(a, b, c) == 0;
}

bool Xor(bool x, bool y)
{
    return (!x) ^ (!y);
}

bool Between(tPointi a, tPointi b, tPointi c)
{
    if (!Collinear(a, b, c))
        return FALSE;

    if (a[X] != b[X])
        return ((a[X] <= c[X] && c[X] <= b[X]) ||
                (a[X] >= c[X] && c[X] >= b[X]));
    else
        return ((a[Y] <= c[Y] && c[Y] <= b[Y]) ||
                (a[Y] >= c[Y] && c[Y] >= b[Y]));
}

/*************** SEGMENT INTERSECTION ****************/

bool IntersectProp(tPointi a, tPointi b, tPointi c, tPointi d)
{
    if (Collinear(a, b, c) || Collinear(a, b, d) ||
        Collinear(c, d, a) || Collinear(c, d, b))
        return FALSE;

    return Xor(Left(a, b, c), Left(a, b, d)) &&
           Xor(Left(c, d, a), Left(c, d, b));
}

bool Intersect(tPointi a, tPointi b, tPointi c, tPointi d)
{
    if (IntersectProp(a, b, c, d))
        return TRUE;

    if (Between(a, b, c) || Between(a, b, d) ||
        Between(c, d, a) || Between(c, d, b))
        return TRUE;

    return FALSE;
}

/************** DIAGONAL DETECTION ****************/

bool Diagonalie(tVertex a, tVertex b)
{
    tVertex c, c1;
    c = vertices;

    do
    {
        c1 = c->next;

        if ((c != a) && (c1 != a) &&
            (c != b) && (c1 != b) &&
            Intersect(a->v, b->v, c->v, c1->v))
            return FALSE;

        c = c->next;
    } while (c != vertices);

    return TRUE;
}

bool InCone(tVertex a, tVertex b)
{
    tVertex a0 = a->prev;
    tVertex a1 = a->next;

    if (LeftOn(a->v, a1->v, a0->v))
        return Left(a->v, b->v, a0->v) &&
               Left(b->v, a->v, a1->v);
    else
        return !(LeftOn(a->v, b->v, a1->v) &&
                 LeftOn(b->v, a->v, a0->v));
}

bool Diagonal(tVertex a, tVertex b)
{
    return InCone(a, b) && InCone(b, a) && Diagonalie(a, b);
}

/************** EAR CLIPPING ****************/

void EarInit(void)
{
    tVertex v0, v1, v2;
    v1 = vertices;

    do
    {
        v2 = v1->next;
        v0 = v1->prev;
        v1->ear = Diagonal(v0, v2);
        v1 = v1->next;
    } while (v1 != vertices);
}

void PrintDiagonal(tVertex a, tVertex b)
{
    printf("Diagonal: (%d,%d) -> (%d,%d)\n",
           a->v[X], a->v[Y], b->v[X], b->v[Y]);
}

void Triangulate(void)
{
    tVertex v0, v1, v2, v3, v4;
    int n = nvertices;

    EarInit();

    while (n > 3)
    {
        v2 = vertices;

        do
        {
            if (v2->ear)
            {
                v3 = v2->next;
                v4 = v3->next;
                v1 = v2->prev;
                v0 = v1->prev;

                PrintDiagonal(v1, v3);
                /* Emit event for visualiser using vertex numbers */
                printf("DIAGONAL %d %d\n", v1->vnum, v3->vnum);
                fflush(stdout);

                /* Update earity of diagonal endpoints */
                v1->ear = Diagonal(v0, v3);
                v3->ear = Diagonal(v1, v4);

                /* Cut off the ear v2 */
                printf("REMOVE %d\n", v2->vnum);
                fflush(stdout);

                v1->ear = Diagonal(v0, v3);
                v3->ear = Diagonal(v1, v4);

                v1->next = v3;
                v3->prev = v1;
                vertices = v3;

                n--;
                break;
            }
            v2 = v2->next;
        } while (v2 != vertices);
    }
}

/************** INPUT + MAIN ****************/

void ReadVertices()
{
    printf("Enter number of vertices: ");
    scanf("%d", &nvertices);

    for (int i = 0; i < nvertices; i++)
    {
        tVertex p;
        NEW(p, tSVertex);
        scanf("%d %d", &p->v[X], &p->v[Y]);
        p->vnum = i;
        ADD(vertices, p);
    }
    /* Print vertices as events for visualiser */
    tVertex t = vertices;
    int printed = 0;
    do
    {
        printf("VERTEX %d %d %d\n", t->vnum, t->v[X], t->v[Y]);
        t = t->next;
        printed++;
    } while (t != vertices && printed < nvertices);

    printf("POLY_READY\n");
    fflush(stdout);
}

void PrintVertices()
{
    tVertex v = vertices;
    printf("Vertices:\n");
    do
    {
        printf("(%d,%d)\n", v->v[X], v->v[Y]);
        v = v->next;
    } while (v != vertices);
}

int main()
{
    ReadVertices();
    PrintVertices();
    Triangulate();
    printf("DONE\n");
    fflush(stdout);

    return 0;
}

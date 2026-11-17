/* too.c - "object-oriented C" stress test for the dcc compiler.
 *
 * Wired into the runall harness (scripts/runall.py); also build/run
 * standalone with:
 *     ./ma.sh too peep   &&  ntvcm TOO
 *     ./ma.sh too nopeep &&  ntvcm TOO
 *
 * The point of this program is to lean hard on the parts of dcc that are easy
 * to get wrong on a 16-bit, single-pass, syntax-directed code generator:
 *
 *   - vtable polymorphism: structs of function pointers reached as
 *     self->vt->method(self), plus arrays/tables of function pointers.
 *   - single inheritance by first-member embedding, with up-casts
 *     (Derived* -> Shape*) and down-casts (Shape* -> Derived*).
 *   - polymorphic containers: arrays of base pointers, pointer-to-pointer
 *     iteration, and a visitor callback applied across the collection.
 *   - multidimensional data crossing call boundaries: pointer-to-2D-array
 *     parameters (Tile (*b)[COLS]), 2D arrays embedded in structs reached
 *     through a pointer (the dcc_expr.c field-array stride path), and 2D
 *     arrays nested inside an array of structs.
 *   - self-referential aggregates: an intrusive linked list and a recursive
 *     binary search tree allocated from a static object pool.
 *   - deeply nested verifiable aggregates: a Gallery -> Hall[] -> Exhibit[]
 *     hierarchy (arrays of structs embedded in structs) whose leaves hang off
 *     malloc'd memory (a malloc'd Shape, a malloc'd title string, a malloc'd
 *     ratings array).  EVERY field at EVERY level is a closed-form function of
 *     its indices (gid = hall*NEX + exhibit), so the whole tree is re-derived
 *     and checked element by element, plus per-hall polymorphic valuation.
 *   - local variable scope: block-scope shadowing, for-init declaration scope,
 *     and function-scope statics (including two sibling static locals that must
 *     resolve to distinct backing storage).
 *   - an over-engineered command machine: an opcode-indexed table of function
 *     pointers mutating an accumulator object (a tiny OO stack machine).
 *   - 32-bit fixed-point arithmetic (areas/perimeters scaled by 100) so the
 *     whole thing stays deterministic without float / -ffloatio.
 *
 * Every interesting value is checked against a hand-computed expected result;
 * the program prints a running log and finishes with either
 *     too passed with great success
 * or a non-zero exit and a FAIL line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* tiny self-checking harness                                          */
/* ------------------------------------------------------------------ */

static int g_checks;
static int g_fails;

static void check_l(const char *name, long got, long expected)
{
    g_checks++;
    if (got != expected) {
        g_fails++;
        printf("  FAIL %s: got %ld expected %ld\n", name, got, expected);
    }
}

static void check_i(const char *name, int got, int expected)
{
    check_l(name, (long)got, (long)expected);
}

static void check_s(const char *name, const char *got, const char *expected)
{
    g_checks++;
    if (strcmp(got, expected) != 0) {
        g_fails++;
        printf("  FAIL %s: got \"%s\" expected \"%s\"\n", name, got, expected);
    }
}

static void *xmalloc(unsigned int n)
{
    void *p = malloc(n);
    if (p == 0) {
        printf("  FAIL out of memory (%u bytes)\n", n);
        exit(2);
    }
    return p;
}

/* print a fixed-point (value * 100) quantity as N.NN */
static void put_fixed(long v100)
{
    long whole = v100 / 100;
    long frac = v100 % 100;
    if (frac < 0)
        frac = -frac;
    printf("%ld.%02ld", whole, frac);
}

/* integer square root of a non-negative long */
static long isqrt_l(long n)
{
    long x, y;

    if (n <= 0)
        return 0;
    x = n;
    y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* ------------------------------------------------------------------ */
/* polymorphic Shape hierarchy (vtable dispatch + embedded base)       */
/* ------------------------------------------------------------------ */

struct Shape;   /* forward declaration for the vtable signatures */

typedef long (*area_fn)(const struct Shape *self);
typedef long (*perim_fn)(const struct Shape *self);
typedef void (*scale_fn)(struct Shape *self, int pct);

typedef struct ShapeVTable {
    const char *name;
    area_fn   area;     /* fixed-point area  * 100 */
    perim_fn  perim;    /* fixed-point perim * 100 */
    scale_fn  scale;    /* scale every linear dimension by pct/100 */
} ShapeVTable;

typedef struct Shape {
    const ShapeVTable *vt;   /* virtual dispatch table (must work through ptr) */
    int id;
} Shape;

/* generic dispatchers - the only code that "knows" about Shape */
static long shape_area(const Shape *s)  { return s->vt->area(s); }
static long shape_perim(const Shape *s) { return s->vt->perim(s); }
static void shape_scale(Shape *s, int pct) { s->vt->scale(s, pct); }

static void shape_describe(const Shape *s)
{
    printf("  #%d %-9s area=", s->id, s->vt->name);
    put_fixed(shape_area(s));
    printf(" perim=");
    put_fixed(shape_perim(s));
    printf("\n");
}

/* ---- Rectangle ---- */

typedef struct Rectangle {
    Shape base;     /* MUST be first member for the up-cast to be valid */
    int w;
    int h;
} Rectangle;

static long rect_area(const struct Shape *self)
{
    const Rectangle *r = (const Rectangle *)self;   /* down-cast */
    return (long)r->w * r->h * 100L;
}

static long rect_perim(const struct Shape *self)
{
    const Rectangle *r = (const Rectangle *)self;
    return (long)(2 * (r->w + r->h)) * 100L;
}

static void rect_scale(struct Shape *self, int pct)
{
    Rectangle *r = (Rectangle *)self;
    r->w = (int)((long)r->w * pct / 100);
    r->h = (int)((long)r->h * pct / 100);
}

static const ShapeVTable RECT_VT = { "Rectangle", rect_area, rect_perim, rect_scale };

static void rect_init(Rectangle *r, int id, int w, int h)
{
    r->base.vt = &RECT_VT;
    r->base.id = id;
    r->w = w;
    r->h = h;
}

/* ---- Circle ---- */

typedef struct Circle {
    Shape base;
    int radius;
} Circle;

/* pi * 10000 == 31416; areas/perims are kept * 100 fixed point */
static long circ_area(const struct Shape *self)
{
    const Circle *c = (const Circle *)self;
    return 31416L * c->radius * c->radius / 100L;
}

static long circ_perim(const struct Shape *self)
{
    const Circle *c = (const Circle *)self;
    return 62832L * c->radius / 100L;
}

static void circ_scale(struct Shape *self, int pct)
{
    Circle *c = (Circle *)self;
    c->radius = (int)((long)c->radius * pct / 100);
}

static const ShapeVTable CIRC_VT = { "Circle", circ_area, circ_perim, circ_scale };

static void circ_init(Circle *c, int id, int radius)
{
    c->base.vt = &CIRC_VT;
    c->base.id = id;
    c->radius = radius;
}

/* ---- right Triangle (legs b,h) ---- */

typedef struct Triangle {
    Shape base;
    int b;
    int h;
} Triangle;

static long tri_area(const struct Shape *self)
{
    const Triangle *t = (const Triangle *)self;
    return (long)t->b * t->h * 50L;   /* b*h/2, * 100 */
}

static long tri_perim(const struct Shape *self)
{
    const Triangle *t = (const Triangle *)self;
    long hyp = isqrt_l((long)t->b * t->b + (long)t->h * t->h);
    return ((long)t->b + t->h + hyp) * 100L;
}

static void tri_scale(struct Shape *self, int pct)
{
    Triangle *t = (Triangle *)self;
    t->b = (int)((long)t->b * pct / 100);
    t->h = (int)((long)t->h * pct / 100);
}

static const ShapeVTable TRI_VT = { "Triangle", tri_area, tri_perim, tri_scale };

static void tri_init(Triangle *t, int id, int b, int h)
{
    t->base.vt = &TRI_VT;
    t->base.id = id;
    t->b = b;
    t->h = h;
}

/* ------------------------------------------------------------------ */
/* polymorphic container + visitor                                     */
/* ------------------------------------------------------------------ */

#define MAX_SHAPES 8

typedef struct World {
    Shape *items[MAX_SHAPES];   /* array of base pointers (up-cast on insert) */
    int count;
    long area_accum;            /* mutated by the visitor below */
} World;

typedef void (*visit_fn)(Shape *s, World *w);

static void world_init(World *w)
{
    w->count = 0;
    w->area_accum = 0;
}

static void world_add(World *w, Shape *s)
{
    if (w->count < MAX_SHAPES)
        w->items[w->count++] = s;
}

/* visit each element through a pointer-to-pointer walk */
static void world_visit(World *w, visit_fn fn)
{
    Shape **pp = w->items;
    Shape **end = w->items + w->count;
    while (pp < end) {
        fn(*pp, w);
        pp++;
    }
}

static void accum_area_visitor(Shape *s, World *w)
{
    w->area_accum += shape_area(s);
}

static void scale_all_visitor(Shape *s, World *w)
{
    (void)w;
    shape_scale(s, 200);    /* double every shape */
}

/* ------------------------------------------------------------------ */
/* function-pointer dispatch table (a "method selector")              */
/* ------------------------------------------------------------------ */

typedef long (*query_fn)(const Shape *s);

typedef struct Query {
    const char *label;
    query_fn fn;
} Query;

static const Query QUERIES[] = {
    { "area",  shape_area },
    { "perim", shape_perim }
};

static long run_query(const Shape *s, int which)
{
    return QUERIES[which].fn(s);
}

/* ------------------------------------------------------------------ */
/* multidimensional data across call boundaries                        */
/* ------------------------------------------------------------------ */

#define ROWS 3
#define COLS 4

typedef struct Tile {
    char ch;
    int weight;
} Tile;

typedef struct Board {
    Tile grid[ROWS][COLS];   /* 2D array of structs embedded in a struct */
    int generation;
} Board;

static void board_fill(Board *bd)
{
    int r, c;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            bd->grid[r][c].ch = (char)('A' + r * COLS + c);
            bd->grid[r][c].weight = r * 10 + c;   /* field 2D array via ptr */
        }
    }
    bd->generation = 1;
}

/* pointer-to-2D-array parameter: b decays to Tile(*)[COLS] */
static long board_weight(Tile (*b)[COLS], int rows)
{
    long sum = 0;
    int r, c;
    for (r = 0; r < rows; r++)
        for (c = 0; c < COLS; c++)
            sum += b[r][c].weight;
    return sum;
}

/* return the address of an element of a struct-embedded 2D array */
static Tile *tile_at(Board *bd, int r, int c)
{
    return &bd->grid[r][c];
}

/* ---- 2D array nested inside an array of structs ---- */

typedef struct Cell {
    unsigned char g[2][2];
    int tag;
} Cell;

#define NCELLS 3

static Cell g_cells[NCELLS];

static void cells_fill(void)
{
    int k, i, j;
    for (k = 0; k < NCELLS; k++) {
        Cell *cp = &g_cells[k];          /* pointer into array of structs */
        for (i = 0; i < 2; i++)
            for (j = 0; j < 2; j++)
                cp->g[i][j] = (unsigned char)((k * 16 + i * 4 + j) & 255);
        cp->tag = k * 100;
    }
}

static long cells_checksum(void)
{
    long sum = 0;
    int k, i, j;
    for (k = 0; k < NCELLS; k++) {
        const Cell *cp = &g_cells[k];
        for (i = 0; i < 2; i++)
            for (j = 0; j < 2; j++)
                sum += cp->g[i][j];
    }
    return sum;
}

/* ------------------------------------------------------------------ */
/* self-referential aggregates: intrusive list + BST from a pool       */
/* ------------------------------------------------------------------ */

typedef struct Node {
    Shape *shape;
    struct Node *next;
} Node;

#define MAX_NODES 8
static Node g_nodes[MAX_NODES];
static int g_node_used;

static Node *node_new(Shape *s)
{
    Node *n = &g_nodes[g_node_used++];
    n->shape = s;
    n->next = 0;
    return n;
}

/* push-front build, returns new head */
static Node *list_push(Node *head, Shape *s)
{
    Node *n = node_new(s);
    n->next = head;
    return n;
}

static long list_area_sum(const Node *head)
{
    long sum = 0;
    const Node *p = head;
    while (p != 0) {
        sum += shape_area(p->shape);
        p = p->next;
    }
    return sum;
}

/* ---- binary search tree of ints from a static pool ---- */

typedef struct TNode {
    int v;
    struct TNode *l;
    struct TNode *r;
} TNode;

#define MAX_TNODES 16
static TNode g_tpool[MAX_TNODES];
static int g_tused;

static TNode *tnode_new(int v)
{
    TNode *t = &g_tpool[g_tused++];
    t->v = v;
    t->l = 0;
    t->r = 0;
    return t;
}

static TNode *bst_insert(TNode *root, int v)
{
    if (root == 0)
        return tnode_new(v);
    if (v < root->v)
        root->l = bst_insert(root->l, v);
    else
        root->r = bst_insert(root->r, v);
    return root;
}

static long bst_inorder_sum(const TNode *root)
{
    if (root == 0)
        return 0;
    return bst_inorder_sum(root->l) + root->v + bst_inorder_sum(root->r);
}

static int bst_height(const TNode *root)
{
    int hl, hr;
    if (root == 0)
        return 0;
    hl = bst_height(root->l);
    hr = bst_height(root->r);
    return 1 + (hl > hr ? hl : hr);
}

/* ------------------------------------------------------------------ */
/* deeply nested, fully verifiable aggregate: a museum Gallery         */
/*                                                                     */
/*   Gallery                                                           */
/*     +-- Hall halls[NHALL]            (array of structs in a struct) */
/*           +-- const CuratorVTable *  (per-hall valuation policy)    */
/*           +-- Exhibit exhibits[NEX]  (array of structs in a struct) */
/*                 +-- char  *title     (malloc'd "E%02d")             */
/*                 +-- Shape *piece      (malloc'd shape; reuses the    */
/*                 |                      Rectangle/Circle/Triangle vt) */
/*                 +-- int   *ratings   (malloc'd array)               */
/*                                                                     */
/* gid = hall*NEX + exhibit; every field is a function of gid/r:       */
/*   title          = "E%02d" % gid                                    */
/*   piece kind     = gid % 3  (0 Rect, 1 Circle, 2 Triangle)          */
/*   Rect  w,h      = (2+gid), 3                                       */
/*   Circle radius  = 1+gid                                            */
/*   Tri   b,h      = (2+gid), 4                                       */
/*   piece->id      = gid                                              */
/*   nratings       = exhibit + 1                                      */
/*   rating[r]      = gid*10 + r                                       */
/* ------------------------------------------------------------------ */

#define NHALL 3
#define NEX   3     /* exhibits per hall */

/* per-hall valuation policy (vtable): value derived from the piece area */
typedef long (*value_fn)(const Shape *s);

typedef struct CuratorVTable {
    const char *name;
    value_fn    value;
} CuratorVTable;

static long value_standard(const Shape *s) { return shape_area(s) / 100L; }
static long value_premium(const Shape *s)  { return (shape_area(s) / 100L) * 2L; }
static long value_discount(const Shape *s) { return (shape_area(s) / 100L) / 2L; }

static const CuratorVTable CUR_STANDARD = { "Standard", value_standard };
static const CuratorVTable CUR_PREMIUM  = { "Premium",  value_premium };
static const CuratorVTable CUR_DISCOUNT = { "Discount", value_discount };

static const CuratorVTable *CURATORS[NHALL] = {
    &CUR_STANDARD, &CUR_PREMIUM, &CUR_DISCOUNT
};

typedef struct Exhibit {
    char  *title;       /* malloc'd string */
    Shape *piece;       /* malloc'd shape (up-cast from a derived type) */
    int    nratings;
    int   *ratings;     /* malloc'd array */
} Exhibit;

typedef struct Hall {
    char *name;                     /* malloc'd "H%d" */
    const CuratorVTable *curator;   /* per-hall valuation policy */
    int   nexhibits;
    Exhibit exhibits[NEX];          /* array of structs embedded in a struct */
} Hall;

typedef struct Gallery {
    const char *name;
    int nhalls;
    Hall halls[NHALL];              /* array of structs embedded in a struct */
} Gallery;

static Gallery g_gallery;

/* build a malloc'd shape whose dimensions are a function of gid */
static Shape *make_piece(int gid)
{
    switch (gid % 3) {
    case 0: {
        Rectangle *r = (Rectangle *)xmalloc((unsigned int)sizeof(Rectangle));
        rect_init(r, gid, 2 + gid, 3);
        return (Shape *)r;
    }
    case 1: {
        Circle *c = (Circle *)xmalloc((unsigned int)sizeof(Circle));
        circ_init(c, gid, 1 + gid);
        return (Shape *)c;
    }
    default: {
        Triangle *t = (Triangle *)xmalloc((unsigned int)sizeof(Triangle));
        tri_init(t, gid, 2 + gid, 4);
        return (Shape *)t;
    }
    }
}

/* recompute the area a piece *should* have, straight from gid (independent of
   the live shape), so the verification does not trust the constructor. */
static long expected_area(int gid)
{
    switch (gid % 3) {
    case 0:  return (long)(2 + gid) * 3 * 100L;            /* Rectangle w*h */
    case 1:  return 31416L * (1 + gid) * (1 + gid) / 100L; /* Circle pi r^2 */
    default: return (long)(2 + gid) * 4 * 50L;             /* Triangle b*h/2 */
    }
}

static void exhibit_init(Exhibit *ex, int h, int e)
{
    int gid = h * NEX + e;
    int nratings = e + 1;
    int r;
    char tmp[8];

    sprintf(tmp, "E%02d", gid);
    ex->title = (char *)xmalloc((unsigned int)(strlen(tmp) + 1));
    strcpy(ex->title, tmp);

    ex->piece = make_piece(gid);

    ex->nratings = nratings;
    ex->ratings = (int *)xmalloc((unsigned int)(nratings * (int)sizeof(int)));
    for (r = 0; r < nratings; r++)
        ex->ratings[r] = gid * 10 + r;
}

static void hall_init(Hall *hl, int h)
{
    int e;
    char tmp[8];

    sprintf(tmp, "H%d", h);
    hl->name = (char *)xmalloc((unsigned int)(strlen(tmp) + 1));
    strcpy(hl->name, tmp);

    hl->curator = CURATORS[h % NHALL];
    hl->nexhibits = NEX;

    for (e = 0; e < NEX; e++)
        exhibit_init(&hl->exhibits[e], h, e);
}

static void gallery_init(Gallery *g)
{
    int h;

    g->name = "Atrium Gallery";
    g->nhalls = NHALL;
    for (h = 0; h < NHALL; h++)
        hall_init(&g->halls[h], h);
}

static void gallery_free(Gallery *g)
{
    int h, e;

    for (h = 0; h < g->nhalls; h++) {
        Hall *hl = &g->halls[h];
        for (e = 0; e < hl->nexhibits; e++) {
            Exhibit *ex = &hl->exhibits[e];
            free(ex->ratings);
            free(ex->piece);
            free(ex->title);
        }
        free(hl->name);
    }
}

/* ------------------------------------------------------------------ */
/* over-engineered OO command machine: opcode-indexed vtable of ops    */
/* mutating an Accumulator object (a tiny stack-free calculator).      */
/* ------------------------------------------------------------------ */

typedef struct Machine {
    long acc;
    int  steps;
} Machine;

typedef void (*op_fn)(Machine *m, long operand);

static void op_set(Machine *m, long v) { m->acc = v; }
static void op_add(Machine *m, long v) { m->acc += v; }
static void op_sub(Machine *m, long v) { m->acc -= v; }
static void op_mul(Machine *m, long v) { m->acc *= v; }

enum { OP_SET, OP_ADD, OP_SUB, OP_MUL, OP_COUNT };

typedef struct OpEntry {
    const char *mnemonic;
    op_fn       fn;
} OpEntry;

static const OpEntry OPTABLE[OP_COUNT] = {
    { "SET", op_set },
    { "ADD", op_add },
    { "SUB", op_sub },
    { "MUL", op_mul }
};

typedef struct Instr {
    int  op;
    long operand;
} Instr;

typedef struct SizedPair {
    int  a;
    long b;
} SizedPair;

/* Global omitted first-dimension struct-array regression: this used to infer
 * zero rows for static/global struct arrays, and 2-D cases were initially
 * skipped by the writeback path. */
static const SizedPair G_PAIR_GRID[][2] = {
    { 1, 10L }, { 2, 20L },
    { 3, 30L }, { 4, 40L },
    { 5, 50L }, { 6, 60L }
};

/* Union element arrays (a union whose first member is an int = one init atom).
 * Braceless and braced element forms, plus a struct carrying a union field. */
typedef union IntLong {
    int  i;
    long l;
} IntLong;

typedef struct Tagged {
    int     tag;
    IntLong u;
} Tagged;

/* Partial struct-array initializer: only the first field is spelled, the rest
 * must zero-fill, and the array length must still infer as the element count. */
typedef struct Op {
    int  code;
    long arg;
} Op;

static const IntLong G_U_BRACELESS[] = { 1, 2, 3, 4 };       /* exp 4 */
static const IntLong G_U_BRACED[]    = { {10}, {20}, {30} };  /* exp 3 */
static const Tagged  G_TAGGED[]      = { {1, {100}}, {2, {200}} };  /* exp 2 */
static const Op      G_OP_PARTIAL[]  = { {7}, {8}, {9} };     /* exp 3, arg=0 */

static void machine_run(Machine *m, const Instr *prog, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        OPTABLE[prog[i].op].fn(m, prog[i].operand);   /* table dispatch */
        m->steps++;
    }
}



static Rectangle g_rect;
static Circle    g_circ;
static Triangle  g_tri;
static World     g_world;

static void test_shapes(void)
{
    printf("shapes:\n");

    rect_init(&g_rect, 1, 4, 6);
    circ_init(&g_circ, 2, 5);
    tri_init(&g_tri, 3, 3, 4);

    /* dispatch through the base pointer (up-cast) */
    shape_describe((Shape *)&g_rect);
    shape_describe((Shape *)&g_circ);
    shape_describe((Shape *)&g_tri);

    check_l("rect.area",  shape_area((Shape *)&g_rect),  2400L);
    check_l("rect.perim", shape_perim((Shape *)&g_rect), 2000L);
    check_l("circ.area",  shape_area((Shape *)&g_circ),  7854L);
    check_l("circ.perim", shape_perim((Shape *)&g_circ), 3141L);
    check_l("tri.area",   shape_area((Shape *)&g_tri),    600L);
    check_l("tri.perim",  shape_perim((Shape *)&g_tri),  1200L);

    /* virtual name through the vtable */
    check_s("rect.name", ((Shape *)&g_rect)->vt->name, "Rectangle");
    check_s("circ.name", ((Shape *)&g_circ)->vt->name, "Circle");
    check_s("tri.name",  ((Shape *)&g_tri)->vt->name,  "Triangle");
}

static void test_dispatch_table(void)
{
    Shape *s = (Shape *)&g_rect;
    printf("dispatch table:\n");
    check_l("query.area",  run_query(s, 0), 2400L);
    check_l("query.perim", run_query(s, 1), 2000L);
    printf("  query[0]=%s query[1]=%s\n", QUERIES[0].label, QUERIES[1].label);
}

static void test_world(void)
{
    long before, after;

    printf("world:\n");
    world_init(&g_world);
    world_add(&g_world, (Shape *)&g_rect);
    world_add(&g_world, (Shape *)&g_circ);
    world_add(&g_world, (Shape *)&g_tri);
    check_i("world.count", g_world.count, 3);

    g_world.area_accum = 0;
    world_visit(&g_world, accum_area_visitor);
    before = g_world.area_accum;
    check_l("world.area_sum", before, 10854L);
    printf("  total area before scale=");
    put_fixed(before);
    printf("\n");

    /* polymorphic mutation, then re-measure: each shape doubled linearly,
       so areas grow ~4x (circle/rect) and triangle 4x too. */
    world_visit(&g_world, scale_all_visitor);
    g_world.area_accum = 0;
    world_visit(&g_world, accum_area_visitor);
    after = g_world.area_accum;

    /* rect 8x12 -> 9600; circle r10 -> 31416; tri 6x8 -> 2400 = 43416 */
    check_l("world.area_after", after, 43416L);
    printf("  total area after 200%% scale=");
    put_fixed(after);
    printf("\n");
}

static Board g_board;

static void test_multidim(void)
{
    Tile *t;
    long w_struct, w_ptr;

    printf("multidim:\n");
    board_fill(&g_board);

    /* sum via pointer-to-2D-array parameter (array decay) */
    w_ptr = board_weight(g_board.grid, ROWS);
    check_l("board.weight_ptr", w_ptr, 138L);

    /* sum directly through the struct pointer (field 2D array stride) */
    w_struct = 0;
    {
        int r, c;
        for (r = 0; r < ROWS; r++)
            for (c = 0; c < COLS; c++)
                w_struct += g_board.grid[r][c].weight;
    }
    check_l("board.weight_struct", w_struct, 138L);

    /* address-of a struct-embedded 2D element returned across a call */
    t = tile_at(&g_board, 2, 3);
    check_i("tile_at.weight", t->weight, 23);
    check_i("tile_at.ch", (int)t->ch, (int)('A' + 2 * COLS + 3));

    cells_fill();
    check_l("cells.checksum", cells_checksum(), 222L);
    check_i("cells[2].tag", g_cells[2].tag, 200);
    printf("  board weight=%ld cells checksum=%ld\n",
           w_ptr, cells_checksum());
}

static void test_aggregates(void)
{
    Node *head = 0;
    TNode *root = 0;
    int seq[9];
    int i;

    printf("aggregates:\n");

    /* intrusive list of the original (pre-scale) shapes would have changed;
       rebuild fresh shapes so the area sum is deterministic. */
    rect_init(&g_rect, 1, 4, 6);
    circ_init(&g_circ, 2, 5);
    tri_init(&g_tri, 3, 3, 4);

    g_node_used = 0;
    head = list_push(head, (Shape *)&g_rect);
    head = list_push(head, (Shape *)&g_circ);
    head = list_push(head, (Shape *)&g_tri);
    check_l("list.area_sum", list_area_sum(head), 10854L);
    check_i("list.head_id", head->shape->id, 3);

    /* BST insert + recursive traversals */
    seq[0] = 5; seq[1] = 3; seq[2] = 8; seq[3] = 1; seq[4] = 4;
    seq[5] = 7; seq[6] = 9; seq[7] = 2; seq[8] = 6;
    g_tused = 0;
    for (i = 0; i < 9; i++)
        root = bst_insert(root, seq[i]);

    check_l("bst.sum", bst_inorder_sum(root), 45L);
    check_i("bst.root", root->v, 5);
    check_i("bst.height", bst_height(root), 4);
    printf("  list area sum=");
    put_fixed(list_area_sum(head));
    printf(" bst sum=%ld height=%d\n", bst_inorder_sum(root), bst_height(root));
}

/* ---- gallery driver: re-derive every field from its indices ---- */

static void test_gallery(void)
{
    int h, e, r;
    int n_exhibits = 0, n_ratings = 0;
    long rating_sum = 0, id_sum = 0, valuation = 0;
    char tmp[8];

    printf("gallery:\n");
    gallery_init(&g_gallery);

    for (h = 0; h < g_gallery.nhalls; h++) {
        Hall *hl = &g_gallery.halls[h];
        const CuratorVTable *cur = CURATORS[h % NHALL];

        sprintf(tmp, "H%d", h);
        check_s("hall.name", hl->name, tmp);
        check_i("hall.nexhibits", hl->nexhibits, NEX);
        check_s("hall.curator", hl->curator->name, cur->name);

        for (e = 0; e < hl->nexhibits; e++) {
            Exhibit *ex = &hl->exhibits[e];
            int gid = h * NEX + e;
            long area = shape_area(ex->piece);
            long via_vt = hl->curator->value(ex->piece);   /* polymorphic */
            long direct;

            sprintf(tmp, "E%02d", gid);
            check_s("exhibit.title", ex->title, tmp);

            /* the malloc'd piece: id and area both re-derived from gid */
            check_i("piece.id", ex->piece->id, gid);
            check_l("piece.area", area, expected_area(gid));

            /* valuation dispatch must match an independent recompute */
            if (h % NHALL == 0)
                direct = area / 100L;
            else if (h % NHALL == 1)
                direct = (area / 100L) * 2L;
            else
                direct = (area / 100L) / 2L;
            check_l("exhibit.value", via_vt, direct);
            valuation += via_vt;

            check_i("exhibit.nratings", ex->nratings, e + 1);
            for (r = 0; r < ex->nratings; r++) {
                check_i("rating", ex->ratings[r], gid * 10 + r);
                rating_sum += ex->ratings[r];
                n_ratings++;
            }

            id_sum += ex->piece->id;
            n_exhibits++;
        }
    }

    /* absolute aggregate anchors */
    check_i("gallery.exhibits", n_exhibits, NHALL * NEX);   /* 9 */
    check_i("gallery.ratings", n_ratings, 18);
    check_l("gallery.rating_sum", rating_sum, 792L);
    check_l("gallery.id_sum", id_sum, 36L);
    check_l("gallery.valuation", valuation, 362L);

    printf("  %d exhibits, %d ratings; rating sum=%ld id sum=%ld\n",
           n_exhibits, n_ratings, rating_sum, id_sum);
    printf("  valuation grand=%ld\n", valuation);

    gallery_free(&g_gallery);
}

/* ---- local variable scope exercises ---- */

/* function-scope static: returns a monotonically increasing id */
static int next_id(void)
{
    static int n;
    return ++n;
}

/* two sibling static locals named the same: must use distinct backing storage */
static int sibA(void)
{
    static int s = 5;
    s += 1;
    return s;
}

static int sibB(void)
{
    static int s = 50;
    s += 2;
    return s;
}

static void test_scope(void)
{
    int x = 10;
    long for_sum = 0;
    int a1, b1, a2, b2;
    int id1, id2, id3;
    int shadow_inner, shadow_mid;

    printf("scope:\n");

    /* block-scope shadowing: inner declarations hide outer ones */
    {
        int x = 20;             /* shadows outer x */
        shadow_mid = x;
        {
            int x = 30;         /* shadows the middle x */
            shadow_inner = x;
        }
        /* back to the middle x */
        check_i("scope.mid", x, 20);
    }
    check_i("scope.shadow_inner", shadow_inner, 30);
    check_i("scope.shadow_mid", shadow_mid, 20);
    check_i("scope.outer", x, 10);   /* outer x untouched */

    /* for-init declaration scope: i and sq live only inside the loop */
    for (int i = 0; i < 4; i++) {
        int sq = i * i;         /* block-local, re-created each iteration */
        for_sum += sq;
    }
    check_l("scope.for_sum", for_sum, 14L);   /* 0+1+4+9 */

    /* function-scope static accumulates across calls */
    id1 = next_id();
    id2 = next_id();
    id3 = next_id();
    check_i("scope.id1", id1, 1);
    check_i("scope.id2", id2, 2);
    check_i("scope.id3", id3, 3);

    /* sibling statics: independent storage, independent seeds/strides */
    a1 = sibA();   /* 6 */
    b1 = sibB();   /* 52 */
    a2 = sibA();   /* 7 */
    b2 = sibB();   /* 54 */
    check_i("scope.sibA1", a1, 6);
    check_i("scope.sibB1", b1, 52);
    check_i("scope.sibA2", a2, 7);
    check_i("scope.sibB2", b2, 54);

    printf("  shadow=%d/%d/%d for-sum=%ld ids=%d,%d,%d sibs=%d,%d,%d,%d\n",
           x, shadow_mid, shadow_inner, for_sum,
           id1, id2, id3, a1, b1, a2, b2);
}

/* ---- over-engineered command machine ---- */

static void test_machine(void)
{
    Machine m;
    const Instr prog[] = {
        { OP_SET, 5 },
        { OP_ADD, 3 },
        { OP_MUL, 4 },
        { OP_SUB, 2 }
    };
    int n = (int)(sizeof(prog) / sizeof(prog[0]));

    printf("machine:\n");

    m.acc = 0;
    m.steps = 0;
    machine_run(&m, prog, n);

    /* ((5 + 3) * 4) - 2 = 30 */
    check_l("machine.acc", m.acc, 30L);
    check_i("machine.steps", m.steps, 4);

    /* verify the dispatch table is wired to the right mnemonics */
    check_s("machine.op0", OPTABLE[OP_SET].mnemonic, "SET");
    check_s("machine.op3", OPTABLE[OP_MUL].mnemonic, "MUL");

    printf("  result=%ld steps=%d\n", m.acc, m.steps);
}

static void test_size_inference(void)
{
    unsigned rows;
    unsigned total;
    int r, c;
    long bsum = 0;

    printf("size inference:\n");

    rows = (unsigned)(sizeof(G_PAIR_GRID) / sizeof(G_PAIR_GRID[0]));
    total = (unsigned)(sizeof(G_PAIR_GRID) / sizeof(G_PAIR_GRID[0][0]));
    check_i("size.rows", (int)rows, 3);
    check_i("size.total", (int)total, 6);

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 2; c++) {
            int idx = r * 2 + c;
            check_i("size.a", G_PAIR_GRID[r][c].a, idx + 1);
            check_l("size.b", G_PAIR_GRID[r][c].b, (long)(idx + 1) * 10L);
            bsum += G_PAIR_GRID[r][c].b;
        }
    }
    check_l("size.bsum", bsum, 210L);
    printf("  static grid rows=%u total=%u bsum=%ld\n", rows, total, bsum);
}

/* Local 2-D struct array, union-element arrays, and partial inits -- all of
 * which used to fail to compile (clean errors) before the size-inference and
 * brace-handling fixes.  Every value is re-derived from its index. */
static void test_size_inference2(void)
{
    /* local 2-D struct array (flattened leaf form) */
    SizedPair lg[][2] = {
        { 1, 11L }, { 2, 12L },
        { 3, 13L }, { 4, 14L }
    };
    /* union element arrays: braceless and braced, local */
    IntLong lu[] = { 5, 6, 7 };          /* exp 3 */
    IntLong lb[] = { {40}, {50} };       /* exp 2 */
    /* partial local struct-array init: arg field zero-fills */
    Op lop[] = { {1}, {2}, {3}, {4} };   /* exp 4, arg=0 */
    int r, c;
    long lgsum = 0;

    printf("size inference 2:\n");

    /* ---- local 2-D struct array ---- */
    check_i("size2.lg_rows", (int)(sizeof(lg) / sizeof(lg[0])), 2);
    check_i("size2.lg_total", (int)(sizeof(lg) / sizeof(lg[0][0])), 4);
    for (r = 0; r < 2; r++) {
        for (c = 0; c < 2; c++) {
            int idx = r * 2 + c;
            check_i("size2.lg_a", lg[r][c].a, idx + 1);
            check_l("size2.lg_b", lg[r][c].b, (long)(idx + 11));
            lgsum += lg[r][c].b;
        }
    }
    check_l("size2.lg_bsum", lgsum, 50L);   /* 11+12+13+14 */

    /* ---- global union-element arrays ---- */
    check_i("size2.gub_n", (int)(sizeof(G_U_BRACELESS) / sizeof(G_U_BRACELESS[0])), 4);
    check_i("size2.gub_last", G_U_BRACELESS[3].i, 4);
    check_i("size2.gbr_n", (int)(sizeof(G_U_BRACED) / sizeof(G_U_BRACED[0])), 3);
    check_i("size2.gbr_last", G_U_BRACED[2].i, 30);

    /* ---- struct with a union field ---- */
    check_i("size2.tag_n", (int)(sizeof(G_TAGGED) / sizeof(G_TAGGED[0])), 2);
    check_i("size2.tag_tag", G_TAGGED[1].tag, 2);
    check_i("size2.tag_ui", G_TAGGED[1].u.i, 200);

    /* ---- local union-element arrays ---- */
    check_i("size2.lu_n", (int)(sizeof(lu) / sizeof(lu[0])), 3);
    check_i("size2.lu_last", lu[2].i, 7);
    check_i("size2.lb_n", (int)(sizeof(lb) / sizeof(lb[0])), 2);
    check_i("size2.lb_last", lb[1].i, 50);

    /* ---- partial struct-array inits (missing field zero-fills) ---- */
    check_i("size2.gop_n", (int)(sizeof(G_OP_PARTIAL) / sizeof(G_OP_PARTIAL[0])), 3);
    check_i("size2.gop_code", G_OP_PARTIAL[2].code, 9);
    check_l("size2.gop_arg", G_OP_PARTIAL[2].arg, 0L);
    check_i("size2.lop_n", (int)(sizeof(lop) / sizeof(lop[0])), 4);
    check_i("size2.lop_code", lop[3].code, 4);
    check_l("size2.lop_arg", lop[3].arg, 0L);

    printf("  local 2D rows=2 bsum=%ld; unions gub=4 gbr=3 lu=3 lb=2; partial gop=3 lop=4\n",
           lgsum);
}

int main(void)
{
    printf("too: OO-style C stress test for dcc\n");

    test_shapes();
    test_dispatch_table();
    test_world();
    test_multidim();
    test_aggregates();
    test_gallery();
    test_scope();
    test_machine();
    test_size_inference();
    test_size_inference2();

    printf("ran %d checks, %d failures\n", g_checks, g_fails);
    if (g_fails == 0) {
        printf("too passed with great success\n");
        return 0;
    }
    printf("too FAILED\n");
    return 1;
}

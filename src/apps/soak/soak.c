/**
 * @file soak.c
 * @brief Runs deterministic long-duration validation of the dcc runtime.
 *
 * @par Role
 * Builds as a no-input CP/M target that churns the heap, creates and verifies
 * temporary files, stresses interleaved buffered I/O, and exercises string,
 * memory, formatted-conversion, qsort, and bsearch routines. It checks all
 * generated data, prints progress and a final summary, and exits on any
 * mismatch while treating transient allocation failure as expected load.
 *
 * @par Key entry points
 * main() schedules the run; file_roundtrip(), multifile_stress(),
 * string_stress(), format_stress(), sort_stress(), and sort_edges() implement
 * the self-checking phases.
 *
 * @par Boundary
 * This application validates DCCRTL through the normal dcc target pipeline; it
 * does not implement the runtime services it stresses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Tunables.  ITERS is sized so the run takes a long time under the emulator;
 * lower it for a quick smoke test. */
#define ITERS       5000000L    /* total heap-churn iterations            */
#define DIAG_EVERY    25000L    /* print a progress line this often       */
#define FILE_EVERY     2000L    /* do a file round-trip this often        */

#define POOL          32        /* number of concurrently live heap slots */
#define MAXBLK        256U      /* max bytes per heap block                */
#define FBUF          512U      /* file round-trip buffer size             */

/* Multi-file interleaved I/O phase: open several files at once and round-robin
 * reads/writes across them to stress the single shared DMA buffer / read sector
 * cache.  MF_FILES stays below NFILES (8) so all handles fit simultaneously. */
#define MF_FILES        6       /* files held open at once (< NFILES)      */
#define MF_SIZE       512U      /* bytes per file (4 CP/M records)         */
#define MF_STEPS      160       /* interleaved ops per multi-file round    */
#define MF_EVERY   100000L      /* run a multi-file round this often       */

/* CPU/heap phases that exercise runtime library routines not covered by the
 * heap-churn and file loops.  Each is self-checking (no external oracle) and
 * runs periodically plus once at startup. */
#define STR_EVERY   50000L      /* string / memory routines + strdup       */
#define STR_MAX       160U      /* max source string length                */
#define FMT_EVERY   50000L      /* sprintf / sscanf / atoi round-trips     */
#define SORT_EVERY  75000L      /* qsort + bsearch round + edge-case pass   */
#define SORT_N        400       /* elements in the sort/search arrays (~400) */

static unsigned char *slot_p[POOL];     /* live pointer, or 0 if empty */
static unsigned int   slot_n[POOL];     /* block size in bytes         */
static unsigned int   slot_tag[POOL];   /* pattern tag for verification*/

static unsigned char fwbuf[FBUF];       /* file write buffer */
static unsigned char frbuf[FBUF];       /* file read-back buffer */
static char linebuf[64];                /* fgets line buffer */

/* Multi-file phase state.  mf_shadow is the authoritative model of what each
 * open file should contain; every read is verified against it. */
static FILE         *mf_fp[MF_FILES];
static unsigned char mf_shadow[MF_FILES * MF_SIZE];
static unsigned int  mf_gen[MF_FILES];
static const char   *mf_names[MF_FILES] = {
    "MF0.TMP", "MF1.TMP", "MF2.TMP", "MF3.TMP", "MF4.TMP", "MF5.TMP"
};

static unsigned int seed = 0xC0DEU;

/* Running statistics (32-bit: counts exceed 16 bits over a long run). */
static unsigned long n_malloc;
static unsigned long n_calloc;
static unsigned long n_realloc;
static unsigned long n_free;
static unsigned long n_oom;
static unsigned long n_files;
static unsigned long n_mfops;           /* interleaved multi-file operations */
static unsigned long n_str;             /* string/memory phase rounds */
static unsigned long n_fmt;             /* printf/scanf phase rounds */
static unsigned long n_sort;            /* sort/search phase rounds */
static unsigned long n_edge;            /* sort/search edge-case rounds */
static unsigned int  live;              /* slots currently in use */
static unsigned int  chk;               /* deterministic rolling checksum */

static unsigned int rnd(void)
{
    seed = (unsigned int)(seed * 25173U + 13849U);
    return seed;
}

static unsigned char patt(unsigned int tag, unsigned int off)
{
    return (unsigned char)((tag * 37U + off * 13U + 91U) & 255U);
}

static void fail(const char *msg, unsigned long it, int idx, unsigned int off)
{
    printf("\nFAIL %s at it=%lu slot=%d off=%u\n", msg, it, idx, off);
    printf("  stats malloc=%lu calloc=%lu realloc=%lu free=%lu oom=%lu files=%lu\n",
           n_malloc, n_calloc, n_realloc, n_free, n_oom, n_files);
    exit(1);
}

static void fillblk(int idx)
{
    unsigned char *p = slot_p[idx];
    unsigned int n = slot_n[idx];
    unsigned int tag = slot_tag[idx];
    unsigned int i;

    for (i = 0; i < n; i++)
        p[i] = patt(tag, i);
    chk = (unsigned int)(chk + tag + n);
}

static void checkblk(int idx, unsigned long it)
{
    unsigned char *p = slot_p[idx];
    unsigned int n = slot_n[idx];
    unsigned int tag = slot_tag[idx];
    unsigned int i;

    for (i = 0; i < n; i++)
        if (p[i] != patt(tag, i))
            fail("heap block corrupted", it, idx, i);
}

/* Verify every live block; called at each diagnostic checkpoint so that
 * long-lived blocks are checked for damage caused by later activity. */
static void verify_all(unsigned long it)
{
    int i;

    for (i = 0; i < POOL; i++)
        if (slot_p[i] != 0)
            checkblk(i, it);
}

static void diag(unsigned long it)
{
    verify_all(it);
    printf("[diag] it=%lu live=%u malloc=%lu calloc=%lu realloc=%lu free=%lu oom=%lu files=%lu mfops=%lu str=%lu fmt=%lu sort=%lu edge=%lu chk=%u\n",
           it, live, n_malloc, n_calloc, n_realloc, n_free, n_oom, n_files, n_mfops, n_str, n_fmt, n_sort, n_edge, chk);
}

/* Allocate into an empty slot, choosing malloc or calloc.  OOM is soft. */
static void do_alloc(int idx)
{
    unsigned int n = (rnd() % MAXBLK) + 1U;
    unsigned char *p;
    unsigned int i;

    if ((rnd() & 1U) != 0) {
        p = (unsigned char *)malloc(n);
        if (p == 0) { n_oom++; return; }
        n_malloc++;
    }
    else {
        p = (unsigned char *)calloc(n, 1U);
        if (p == 0) { n_oom++; return; }
        /* calloc must zero the block */
        for (i = 0; i < n; i++)
            if (p[i] != 0)
                fail("calloc block not zeroed", 0L, idx, i);
        n_calloc++;
    }

    slot_p[idx] = p;
    slot_n[idx] = n;
    slot_tag[idx] = rnd();
    fillblk(idx);
    live++;
}

/* Resize a live slot, verifying the preserved prefix survived the realloc. */
static void do_realloc(int idx, unsigned long it)
{
    unsigned int oldn = slot_n[idx];
    unsigned int newn = (rnd() % MAXBLK) + 1U;
    unsigned int keep = oldn < newn ? oldn : newn;
    unsigned int tag = slot_tag[idx];
    unsigned char *p;
    unsigned int i;

    p = (unsigned char *)realloc(slot_p[idx], newn);
    if (p == 0) {
        /* realloc failure must leave the original block intact. */
        n_oom++;
        checkblk(idx, it);
        return;
    }
    n_realloc++;

    /* The first keep bytes must still hold the old pattern. */
    for (i = 0; i < keep; i++)
        if (p[i] != patt(tag, i))
            fail("realloc dropped data", it, idx, i);

    slot_p[idx] = p;
    slot_n[idx] = newn;
    fillblk(idx);               /* refill to the new size (same tag) */
}

static void do_free(int idx)
{
    free(slot_p[idx]);
    slot_p[idx] = 0;
    slot_n[idx] = 0;
    n_free++;
    live--;
}

/* One file round-trip: write a patterned buffer, read it back via several
 * access patterns, and verify.  Exercises multi-record files, partial final
 * records, fseek/rewind, the read sector cache, and fgets. */
static void file_roundtrip(unsigned long it, unsigned int fseed)
{
    FILE *f;
    unsigned int n = (fseed % FBUF) + 1U;
    unsigned int off;
    unsigned int i;
    int r;
    long pos;

    /* Build a known pattern. */
    for (i = 0; i < n; i++)
        fwbuf[i] = (unsigned char)((fseed + i * 7U) & 255U);

    unlink("tsoak.tmp");
    f = fopen("tsoak.tmp", "w+");
    if (f == 0)
        fail("fopen w+ failed", it, -1, 0);

    if (fwrite(fwbuf, 1, n, f) != n)
        fail("short fwrite", it, -1, n);

    /* (a) rewind to start and read the whole buffer back. */
    rewind(f);
    r = (int)fread(frbuf, 1, n, f);
    if ((unsigned int)r != n)
        fail("short fread after rewind", it, -1, (unsigned int)r);
    for (i = 0; i < n; i++)
        if (frbuf[i] != fwbuf[i])
            fail("data mismatch after rewind", it, -1, i);

    /* (b) random-access seek: read one byte from a random offset.  Repeated
     * reads near the same offset hit the read sector cache. */
    if (n > 1U) {
        off = fseed % n;
        if (fseek(f, (long)off, SEEK_SET) != 0)
            fail("fseek SET failed", it, -1, off);
        r = (int)fread(frbuf, 1, 1, f);
        if (r != 1 || frbuf[0] != fwbuf[off])
            fail("random-access byte mismatch", it, -1, off);

        /* ftell must report the position just after that byte. */
        pos = ftell(f);
        if (pos != (long)off + 1L)
            fail("ftell wrong after seek+read", it, -1, off);
    }

    fclose(f);

    /* (c) text round-trip every few cycles: write numbered lines, rewind,
     * and read them back with fgets, checking content. */
    if ((fseed & 3U) == 0) {
        f = fopen("tsoak.tmp", "w+");
        if (f == 0)
            fail("fopen w+ (text) failed", it, -1, 0);
        fprintf(f, "line-%u\n", fseed & 0x0fffU);
        fprintf(f, "end\n");
        rewind(f);
        if (fgets(linebuf, sizeof(linebuf), f) == 0)
            fail("fgets line 1 returned null", it, -1, 0);
        if (strncmp(linebuf, "line-", 5) != 0)
            fail("fgets line 1 content wrong", it, -1, 0);
        if (fgets(linebuf, sizeof(linebuf), f) == 0)
            fail("fgets line 2 returned null", it, -1, 0);
        if (strncmp(linebuf, "end", 3) != 0)
            fail("fgets line 2 content wrong", it, -1, 0);
        fclose(f);
    }

    unlink("tsoak.tmp");
    n_files++;
}

/* Distinct byte pattern per (file, generation, offset).  Including the
 * generation lets a later write to a region produce different bytes, so a
 * stale cache that returned the pre-write bytes is detected. */
static unsigned char mf_patt(int fidx, unsigned int gen, unsigned int off)
{
    return (unsigned char)((fidx * 53U + gen * 101U + off * 7U + 17U) & 255U);
}

/* Multi-file interleaved I/O round.
 *
 * Opens MF_FILES files at once (all sharing the runtime's single 128-byte DMA
 * buffer and read sector cache), seeds each with a distinct full pattern, then
 * round-robins random reads, sequential reads, and writes across them.  Every
 * read is checked against an in-memory shadow of what the file should contain,
 * which is the decisive test for the sector cache:
 *
 *   - cross-file reads (consecutive ops on different files) must each reload the
 *     correct file's record -- a read must never return another file's cached
 *     bytes;
 *   - a read immediately after a write to the same region must see the new data
 *     -- the write must have invalidated the cache;
 *   - consecutive reads within one file/record exercise the cache-hit path and
 *     must still return correct bytes.
 *
 * A final full re-read of every file against the shadow is the end-state proof.
 */
static void multifile_stress(unsigned long it)
{
    int i;
    int s;
    int fidx;
    unsigned int off;
    unsigned int off2;
    unsigned int len;
    unsigned int len2;
    unsigned int k;
    unsigned int gen;
    unsigned int want;
    unsigned char b;
    int r;
    FILE *f;

    /* Open all files (create/truncate, read-write) and seed each with a
     * distinct full-size pattern written sequentially from offset 0. */
    for (i = 0; i < MF_FILES; i++) {
        unlink(mf_names[i]);
        mf_fp[i] = fopen(mf_names[i], "w+");
        if (mf_fp[i] == 0)
            fail("multifile fopen failed", it, i, 0);
        mf_gen[i] = 0;
        for (off = 0; off < MF_SIZE; off++)
            mf_shadow[i * MF_SIZE + off] = mf_patt(i, 0U, off);
        if ((unsigned int)fwrite(&mf_shadow[i * MF_SIZE], 1, MF_SIZE, mf_fp[i]) != MF_SIZE)
            fail("multifile init fwrite short", it, i, 0);
    }

    /* Interleave reads and writes across the open files. */
    for (s = 0; s < MF_STEPS; s++) {
        fidx = (int)(rnd() % (unsigned int)MF_FILES);
        f = mf_fp[fidx];
        off = rnd() % MF_SIZE;
        len = (rnd() % 64U) + 1U;
        if (off + len > MF_SIZE)
            len = MF_SIZE - off;

        if ((rnd() & 3U) == 0U) {
            /* WRITE a fresh generation, then immediately read it back. */
            gen = ++mf_gen[fidx];
            for (k = 0; k < len; k++) {
                b = mf_patt(fidx, gen, off + k);
                fwbuf[k] = b;
                mf_shadow[fidx * MF_SIZE + off + k] = b;
            }
            if (fseek(f, (long)off, SEEK_SET) != 0)
                fail("multifile write seek failed", it, fidx, off);
            if ((unsigned int)fwrite(fwbuf, 1, len, f) != len)
                fail("multifile fwrite short", it, fidx, off);
            if (fseek(f, (long)off, SEEK_SET) != 0)
                fail("multifile readback seek failed", it, fidx, off);
            r = (int)fread(frbuf, 1, len, f);
            if ((unsigned int)r != len)
                fail("multifile readback short", it, fidx, off);
            for (k = 0; k < len; k++)
                if (frbuf[k] != mf_shadow[fidx * MF_SIZE + off + k])
                    fail("readback mismatch (write left stale cache)", it, fidx, off + k);
        }
        else {
            /* READ across files: a cache miss that must load the right record. */
            if (fseek(f, (long)off, SEEK_SET) != 0)
                fail("multifile read seek failed", it, fidx, off);
            r = (int)fread(frbuf, 1, len, f);
            if ((unsigned int)r != len)
                fail("multifile fread short", it, fidx, off);
            for (k = 0; k < len; k++)
                if (frbuf[k] != mf_shadow[fidx * MF_SIZE + off + k])
                    fail("read mismatch (wrong file's cached record)", it, fidx, off + k);

            /* Continue reading sequentially in the same file: exercises the
             * cache-hit path within a record and must still match. */
            off2 = off + len;
            if (off2 < MF_SIZE) {
                len2 = (rnd() % 16U) + 1U;
                if (off2 + len2 > MF_SIZE)
                    len2 = MF_SIZE - off2;
                r = (int)fread(frbuf, 1, len2, f);
                if ((unsigned int)r != len2)
                    fail("multifile seq fread short", it, fidx, off2);
                for (k = 0; k < len2; k++)
                    if (frbuf[k] != mf_shadow[fidx * MF_SIZE + off2 + k])
                        fail("seq read mismatch (cache hit served stale)", it, fidx, off2 + k);
            }
        }
        n_mfops++;
    }

    /* End-state proof: every file's full contents must match the shadow. */
    for (i = 0; i < MF_FILES; i++) {
        rewind(mf_fp[i]);
        for (off = 0; off < MF_SIZE; off += 128U) {
            want = MF_SIZE - off;
            if (want > 128U)
                want = 128U;
            r = (int)fread(frbuf, 1, want, mf_fp[i]);
            if ((unsigned int)r != want)
                fail("multifile final read short", it, i, off);
            for (k = 0; k < want; k++)
                if (frbuf[k] != mf_shadow[i * MF_SIZE + off + k])
                    fail("multifile final mismatch", it, i, off + k);
        }
    }

    /* Close and remove all files. */
    for (i = 0; i < MF_FILES; i++) {
        fclose(mf_fp[i]);
        mf_fp[i] = 0;
        unlink(mf_names[i]);
    }
}

/* ============================================================
 * Phase: string / memory routines over heap buffers (+ strdup)
 *
 * Builds known strings/byte patterns in malloc'd buffers and drives the
 * runtime string and memory routines against them, verifying results.  Because
 * the working buffers are heap-allocated, any one-past-the-end write also shows
 * up as allocator corruption at the next checkpoint.  Source bytes are letters
 * 'A'..'Z' (never NUL) so the string routines see proper C strings.
 * ============================================================ */
static unsigned char str_shadow[STR_MAX];

static void string_stress(unsigned long it)
{
    char *src;
    char *dst;
    char *cat;
    char *dup;
    unsigned char *mb;
    unsigned char *ov;
    char needle[40];
    unsigned int len;
    unsigned int base;
    unsigned int i;
    unsigned int ns;
    char want;
    char *p;

    len = (rnd() % (STR_MAX - 8U)) + 4U;        /* 4 .. STR_MAX-5 */
    base = rnd();

    src = (char *)malloc(len + 1U);
    if (src == 0) { n_oom++; return; }
    for (i = 0; i < len; i++)
        src[i] = (char)('A' + (unsigned char)((base + i) % 26U));
    src[len] = 0;

    /* strlen */
    if (strlen(src) != len)
        fail("strlen wrong", it, -1, len);

    /* strcpy + strcmp + strlen of the copy */
    dst = (char *)malloc(len + 1U);
    if (dst == 0) { free(src); n_oom++; return; }
    strcpy(dst, src);
    if (strcmp(dst, src) != 0)
        fail("strcpy/strcmp mismatch", it, -1, 0);
    if (strlen(dst) != len)
        fail("strcpy length wrong", it, -1, len);

    /* strcmp must detect a single-byte difference, then restore */
    dst[len / 2U] = (char)(dst[len / 2U] + 1);
    if (strcmp(dst, src) == 0)
        fail("strcmp missed difference", it, -1, len / 2U);
    strcpy(dst, src);

    /* strchr / strrchr present + absent */
    want = src[len / 3U];
    p = strchr(src, want);
    if (p == 0 || *p != want)
        fail("strchr failed", it, -1, len / 3U);
    p = strrchr(src, want);
    if (p == 0 || *p != want)
        fail("strrchr failed", it, -1, len / 3U);
    if (strchr(src, '*') != 0)
        fail("strchr found absent", it, -1, 0);

    /* strcat / strncat over a heap buffer */
    cat = (char *)malloc(len * 2U + 1U);
    if (cat != 0) {
        strcpy(cat, src);
        strcat(cat, src);
        if (strlen(cat) != len * 2U)
            fail("strcat length wrong", it, -1, len * 2U);
        if (strncmp(cat, src, len) != 0)
            fail("strcat prefix wrong", it, -1, 0);
        if (strncmp(cat + len, src, len) != 0)
            fail("strcat suffix wrong", it, -1, len);
        free(cat);
    }

    /* strstr: a slice of src is by construction a substring of src */
    ns = len / 4U;
    if (ns >= 1U) {
        if (ns > 39U)
            ns = 39U;
        for (i = 0; i < ns; i++)
            needle[i] = src[2U + i];
        needle[ns] = 0;
        if (strstr(src, needle) == 0)
            fail("strstr missed substring", it, -1, ns);
    }

    /* memcpy / memcmp / memset / memchr over a heap buffer */
    mb = (unsigned char *)malloc(len);
    if (mb != 0) {
        memcpy(mb, src, len);
        if (memcmp(mb, src, len) != 0)
            fail("memcpy/memcmp mismatch", it, -1, 0);
        memset(mb, 0x5A, len);
        for (i = 0; i < len; i++)
            if (mb[i] != 0x5A)
                fail("memset wrong", it, -1, i);
        if (memchr(mb, 0x5A, len) == 0)
            fail("memchr missed present", it, -1, 0);
        if (memchr(mb, 0x01, len) != 0)
            fail("memchr found absent", it, -1, 0);
        free(mb);
    }

    /* memmove with overlap: shift a region up by one byte and verify against a
     * byte-loop shadow (the backward-copy path the runtime takes for dst>src). */
    ov = (unsigned char *)malloc(len);
    if (ov != 0) {
        for (i = 0; i < len; i++) {
            ov[i] = (unsigned char)(base + i);
            str_shadow[i] = ov[i];
        }
        for (i = len - 1U; i >= 1U; i--)
            str_shadow[i] = str_shadow[i - 1U];
        memmove(ov + 1, ov, len - 1U);
        for (i = 0; i < len; i++)
            if (ov[i] != str_shadow[i])
                fail("memmove overlap mismatch", it, -1, i);
        free(ov);
    }

    /* strdup: independent heap copy with equal content and a distinct pointer */
    dup = strdup(src);
    if (dup == 0) {
        n_oom++;
    } else {
        if (dup == src)
            fail("strdup returned same pointer", it, -1, 0);
        if (strcmp(dup, src) != 0)
            fail("strdup content mismatch", it, -1, 0);
        dup[0] = (char)(dup[0] + 1);            /* mutating dup must not touch src */
        if (dup[0] == src[0])
            fail("strdup not independent", it, -1, 0);
        free(dup);
    }

    free(dst);
    free(src);
    n_str++;
}

/* ============================================================
 * Phase: printf / scanf numeric round-trips
 *
 * Formats random int / unsigned / long / hex values into a heap buffer with
 * sprintf, parses them back with atoi / sscanf, and asserts equality.  This is
 * self-checking and exercises the printf buffer-mode path and the scan parser.
 * (atol is not provided by this runtime, so longs go through sscanf %ld.)
 * ============================================================ */
static void format_stress(unsigned long it)
{
    char *buf;
    int iv;
    int riv;
    long lv;
    long rlv;
    unsigned int uv;
    unsigned int ruv;
    int a;
    int b;
    int ra;
    int rb;

    buf = (char *)malloc(64U);
    if (buf == 0) { n_oom++; return; }

    /* signed int: sprintf -> atoi and sprintf -> sscanf %d */
    iv = (int)(rnd() & 0x7fffU);
    if ((rnd() & 1U) != 0)
        iv = -iv;
    sprintf(buf, "%d", iv);
    if (atoi(buf) != iv)
        fail("atoi round-trip wrong", it, -1, 0);
    riv = 0;
    if (sscanf(buf, "%d", &riv) != 1 || riv != iv)
        fail("sscanf %d round-trip wrong", it, -1, 0);

    /* unsigned int: %u */
    uv = rnd();
    sprintf(buf, "%u", uv);
    ruv = 0;
    if (sscanf(buf, "%u", &ruv) != 1 || ruv != uv)
        fail("sscanf %u round-trip wrong", it, -1, 0);

    /* long: %ld (value masked to stay well inside LONG range, then signed) */
    lv = (long)((((unsigned long)rnd() << 15) ^ (unsigned long)rnd()) & 0x3fffffffUL);
    if ((rnd() & 1U) != 0)
        lv = -lv;
    sprintf(buf, "%ld", lv);
    rlv = 0;
    if (sscanf(buf, "%ld", &rlv) != 1 || rlv != lv)
        fail("sscanf %ld round-trip wrong", it, -1, 0);

    /* unsigned hex: %x out, %x back in */
    uv = rnd();
    sprintf(buf, "%x", uv);
    ruv = 0;
    if (sscanf(buf, "%x", &ruv) != 1 || ruv != uv)
        fail("sscanf %x round-trip wrong", it, -1, 0);

    /* two fields in one format string */
    a = (int)(rnd() & 0x3fffU);
    b = (int)(rnd() & 0x3fffU);
    sprintf(buf, "%d %d", a, b);
    ra = 0;
    rb = 0;
    if (sscanf(buf, "%d %d", &ra, &rb) != 2 || ra != a || rb != b)
        fail("sscanf two-field round-trip wrong", it, -1, 0);

    free(buf);
    n_fmt++;
}

/* ============================================================
 * Phase: qsort + bsearch over a heap array (runtime routines)
 *
 * qsort and bsearch are part of this runtime (DCCRTL.MAC); they take the
 * standard comparator and call it through a function pointer, which exercises
 * the runtime indirect-call path (__call_hl).  The array under test is heap-
 * allocated and ~400 elements: sort correctness is checked against an
 * independent insertion-sort oracle, and bsearch is probed for every present
 * key plus known-absent sentinels.  sort_edges() adds the boundary cases the
 * random run is unlikely to hit (empty, single, two, already-sorted, reverse-
 * sorted, all-equal, heavy duplicates, and a descending comparator).
 * ============================================================ */
static int cmp_int(const void *a, const void *b)
{
    int va = *(const int *)a;
    int vb = *(const int *)b;

    if (va < vb)
        return -1;
    if (va > vb)
        return 1;
    return 0;
}

/* Descending order, expressed by swapping the operands of cmp_int. */
static int cmp_int_desc(const void *a, const void *b)
{
    return cmp_int(b, a);
}

static int sort_oracle[SORT_N];

/* Large random array (~400 elements): sort with the runtime qsort, check it
 * element-for-element against an insertion-sort oracle, then bsearch every
 * present value and two guaranteed-absent sentinels. */
static void sort_stress(unsigned long it)
{
    int *a;
    int n;
    int i;
    int j;
    int key;
    const int *found;

    n = (int)SORT_N - (int)(rnd() % 32U);       /* ~369 .. 400 elements */
    a = (int *)malloc((unsigned int)n * sizeof(int));
    if (a == 0) { n_oom++; return; }

    /* random values in [1,1000] (0 and 2000 are guaranteed-absent keys) */
    for (i = 0; i < n; i++) {
        a[i] = (int)(rnd() % 1000U) + 1;
        sort_oracle[i] = a[i];
    }

    /* oracle: in-place insertion sort (obviously correct) */
    for (i = 1; i < n; i++) {
        key = sort_oracle[i];
        j = i - 1;
        while (j >= 0 && sort_oracle[j] > key) {
            sort_oracle[j + 1] = sort_oracle[j];
            j--;
        }
        sort_oracle[j + 1] = key;
    }

    qsort(a, (size_t)n, sizeof(int), cmp_int);

    /* qsort output must match the oracle element-for-element (proves both
     * sorted order and that it is a permutation of the input) */
    for (i = 0; i < n; i++)
        if (a[i] != sort_oracle[i])
            fail("qsort result != oracle", it, -1, (unsigned int)i);

    /* bsearch must find every present value and reject absent sentinels */
    for (i = 0; i < n; i++) {
        key = a[i];
        found = (const int *)bsearch(&key, a, (size_t)n, sizeof(int), cmp_int);
        if (found == 0 || *found != key)
            fail("bsearch missed present key", it, -1, (unsigned int)i);
    }
    key = 0;
    if (bsearch(&key, a, (size_t)n, sizeof(int), cmp_int) != 0)
        fail("bsearch found absent low key", it, -1, 0);
    key = 2000;
    if (bsearch(&key, a, (size_t)n, sizeof(int), cmp_int) != 0)
        fail("bsearch found absent high key", it, -1, 0);

    free(a);
    n_sort++;
}

/* Boundary cases on a heap array with deterministic inputs, so the expected
 * result is known directly (no oracle needed).  Covers the empty/single/two
 * sizes, already-sorted and reverse-sorted full arrays, all-equal and heavy-
 * duplicate arrays, the first/last/below/above bsearch positions, and a
 * descending comparator. */
static void sort_edges(unsigned long it)
{
    int *a;
    int i;
    int key;
    const int *found;

    a = (int *)malloc((unsigned int)SORT_N * sizeof(int));
    if (a == 0) { n_oom++; return; }

    /* (1) empty array: qsort is a no-op and bsearch always misses */
    qsort(a, 0U, sizeof(int), cmp_int);
    key = 5;
    if (bsearch(&key, a, 0U, sizeof(int), cmp_int) != 0)
        fail("bsearch hit on empty array", it, -1, 0);

    /* (2) single element: exact hit and a miss on either side */
    a[0] = 42;
    qsort(a, 1U, sizeof(int), cmp_int);
    if (a[0] != 42)
        fail("qsort n=1 disturbed element", it, -1, 0);
    key = 42;
    found = (const int *)bsearch(&key, a, 1U, sizeof(int), cmp_int);
    if (found != (const int *)&a[0])
        fail("bsearch n=1 missed", it, -1, 0);
    key = 41;
    if (bsearch(&key, a, 1U, sizeof(int), cmp_int) != 0)
        fail("bsearch n=1 false low", it, -1, 0);
    key = 43;
    if (bsearch(&key, a, 1U, sizeof(int), cmp_int) != 0)
        fail("bsearch n=1 false high", it, -1, 0);

    /* (3) two elements given out of order: must come back sorted */
    a[0] = 2;
    a[1] = 1;
    qsort(a, 2U, sizeof(int), cmp_int);
    if (a[0] != 1 || a[1] != 2)
        fail("qsort n=2 not ordered", it, -1, 0);
    key = 1;
    if ((const int *)bsearch(&key, a, 2U, sizeof(int), cmp_int) != (const int *)&a[0])
        fail("bsearch n=2 first", it, -1, 0);
    key = 2;
    if ((const int *)bsearch(&key, a, 2U, sizeof(int), cmp_int) != (const int *)&a[1])
        fail("bsearch n=2 second", it, -1, 0);
    key = 3;
    if (bsearch(&key, a, 2U, sizeof(int), cmp_int) != 0)
        fail("bsearch n=2 false high", it, -1, 0);

    /* (4) already sorted ascending (values 0..SORT_N-1): qsort idempotent,
     * bsearch resolves the first, last, and out-of-range keys */
    for (i = 0; i < SORT_N; i++)
        a[i] = i;
    qsort(a, (size_t)SORT_N, sizeof(int), cmp_int);
    for (i = 0; i < SORT_N; i++)
        if (a[i] != i)
            fail("qsort sorted-input not idempotent", it, -1, (unsigned int)i);
    key = 0;
    if ((const int *)bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int) != (const int *)&a[0])
        fail("bsearch first element", it, -1, 0);
    key = SORT_N - 1;
    if ((const int *)bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int) != (const int *)&a[SORT_N - 1])
        fail("bsearch last element", it, -1, 0);
    key = -1;
    if (bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int) != 0)
        fail("bsearch below range", it, -1, 0);
    key = SORT_N;
    if (bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int) != 0)
        fail("bsearch above range", it, -1, 0);

    /* (5) reverse sorted: qsort must fully reorder to ascending */
    for (i = 0; i < SORT_N; i++)
        a[i] = SORT_N - 1 - i;
    qsort(a, (size_t)SORT_N, sizeof(int), cmp_int);
    for (i = 0; i < SORT_N; i++)
        if (a[i] != i)
            fail("qsort reverse-input wrong", it, -1, (unsigned int)i);

    /* (6) all equal: order is unchanged and bsearch finds the value */
    for (i = 0; i < SORT_N; i++)
        a[i] = 7;
    qsort(a, (size_t)SORT_N, sizeof(int), cmp_int);
    for (i = 0; i < SORT_N; i++)
        if (a[i] != 7)
            fail("qsort all-equal disturbed", it, -1, (unsigned int)i);
    key = 7;
    found = (const int *)bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int);
    if (found == 0 || *found != 7)
        fail("bsearch all-equal missed", it, -1, 0);
    key = 6;
    if (bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int) != 0)
        fail("bsearch all-equal false low", it, -1, 0);
    key = 8;
    if (bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int) != 0)
        fail("bsearch all-equal false high", it, -1, 0);

    /* (7) heavy duplicates: two interleaved values must group into halves */
    for (i = 0; i < SORT_N; i++)
        a[i] = (i & 1) ? 100 : 50;
    qsort(a, (size_t)SORT_N, sizeof(int), cmp_int);
    for (i = 0; i < SORT_N; i++)
        if (a[i] != ((i < SORT_N / 2) ? 50 : 100))
            fail("qsort duplicates not grouped", it, -1, (unsigned int)i);
    key = 50;
    found = (const int *)bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int);
    if (found == 0 || *found != 50)
        fail("bsearch dup low value", it, -1, 0);
    key = 100;
    found = (const int *)bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int);
    if (found == 0 || *found != 100)
        fail("bsearch dup high value", it, -1, 0);
    key = 75;
    if (bsearch(&key, a, (size_t)SORT_N, sizeof(int), cmp_int) != 0)
        fail("bsearch dup gap key", it, -1, 0);

    /* (8) descending comparator: result must be non-increasing */
    for (i = 0; i < SORT_N; i++)
        a[i] = i;
    qsort(a, (size_t)SORT_N, sizeof(int), cmp_int_desc);
    for (i = 0; i < SORT_N; i++)
        if (a[i] != SORT_N - 1 - i)
            fail("qsort descending wrong", it, -1, (unsigned int)i);

    free(a);
    n_edge++;
}

int main(void)
{
    unsigned long it;
    int idx;
    int i;
    unsigned int op;

    for (i = 0; i < POOL; i++) {
        slot_p[i] = 0;
        slot_n[i] = 0;
        slot_tag[i] = 0;
    }

    printf("tsoak start: %lu iterations, diag every %lu, file every %lu\n",
           (unsigned long)ITERS, (unsigned long)DIAG_EVERY,
           (unsigned long)FILE_EVERY);

    /* Run one multi-file round up front so even a short run covers the
     * interleaved-open-files cache stress. */
    multifile_stress(0L);

    /* Run each CPU/heap phase once up front too, so short smoke runs (ITERS
     * below the *_EVERY thresholds) still exercise them. */
    string_stress(0L);
    format_stress(0L);
    sort_stress(0L);
    sort_edges(0L);

    for (it = 1; it <= ITERS; it++) {
        idx = (int)(rnd() % (unsigned int)POOL);

        if (slot_p[idx] == 0) {
            do_alloc(idx);
        }
        else {
            /* Verify before mutating, then realloc or free. */
            checkblk(idx, it);
            op = rnd() % 3U;
            if (op == 0)
                do_realloc(idx, it);
            else
                do_free(idx);
        }

        if ((it % (unsigned long)FILE_EVERY) == 0L)
            file_roundtrip(it, rnd());

        if ((it % (unsigned long)MF_EVERY) == 0L)
            multifile_stress(it);

        if ((it % (unsigned long)STR_EVERY) == 0L)
            string_stress(it);

        if ((it % (unsigned long)FMT_EVERY) == 0L)
            format_stress(it);

        if ((it % (unsigned long)SORT_EVERY) == 0L) {
            sort_stress(it);
            sort_edges(it);
        }

        if ((it % (unsigned long)DIAG_EVERY) == 0L)
            diag(it);
    }

    /* Final sweep: verify and release everything still live. */
    verify_all(ITERS);
    for (i = 0; i < POOL; i++)
        if (slot_p[i] != 0)
            do_free(i);

    printf("tsoak summary: malloc=%lu calloc=%lu realloc=%lu free=%lu oom=%lu files=%lu mfops=%lu str=%lu fmt=%lu sort=%lu edge=%lu chk=%u\n",
           n_malloc, n_calloc, n_realloc, n_free, n_oom, n_files, n_mfops, n_str, n_fmt, n_sort, n_edge, chk);
    printf("tsoak completed with great success\n");
    return 0;
}

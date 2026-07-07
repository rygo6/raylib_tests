/*******************************************************************************************
*
*   raylib [tests] - image comparison diff
*
*   Compares a candidate set of captured frames against a reference set, frame by frame, and
*   prints PASS / FAIL / SKIP per comparison. It also writes an HTML report: matching scenes
*   listed in green, and for every mismatch the reference image, the candidate image and an
*   amplified difference image shown side by side.
*
*   Tolerance: GPU rasterization/shading can differ by a single least-significant bit between
*   runs (and between graphics backends). A per-channel tolerance treats such sub-threshold
*   noise as equal, so only meaningful differences are reported.
*
*   Exclusions: some examples render real-world state that no frame-locked timestep can make
*   deterministic (the system wall-clock, a live audio buffer, ...). Listed examples are
*   reported as SKIP and never counted as failures.
*
*   Allowances: some examples have EXPECTED variability between graphics backends that survives
*   the per-channel and spatial tolerances — cross-compiler floating-point ULP differences that
*   flip knife-edge outcomes (fractal escape-iteration boundaries, rasterization ties on
*   silhouette edges, shading noise one step above tolerance). An 'allow <example> <maxPixels>'
*   entry grants that example a per-frame differing-pixel budget: frames within budget are
*   reported as PASS~ (tolerated) and listed separately in the HTML report, so the allowance
*   stays visible. Budgets should be sized from measured behavior with modest headroom — real
*   regressions have always shown up orders of magnitude above these counts.
*
*   Settings are read from a rini config file; every path is relative to the working directory.
*   Run this from the directory that holds the reference/candidate folders so the relative image
*   paths written into the HTML resolve when opened in a browser.
*
*   USAGE:
*       image_comparison_diff [configFile]              (default: image_comparison_rlvk.ini)
*
*   Config keys (rini, "key value" pairs, '#' comments):
*       ref_dir, cmp_dir, diff_dir, report, tolerance, exclude (comma-separated names),
*       include (comma-separated names; when present, ONLY those examples are compared —
*       the regression-subset mechanism, everything else is neither compared nor reported)
*
*   Exit code: 0 if all PASS (SKIP is not a failure), else 2.
*
*   This tool is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
*   Copyright (c) 2025-2026 Ramon Santamaria (@raysan5)
*
********************************************************************************************/

#include "raylib.h"
#include <stdbool.h>

#include <stdio.h>              // Required for: printf(), fopen(), fprintf()
#include <stdlib.h>            // Required for: malloc(), free(), qsort(), strtok()
#include <string.h>          // Required for: strcmp(), strlen(), memcmp()

#define RINI_IMPLEMENTATION
#include "rini.h"       // raysan5's ini-style config reader ("key value" pairs, '#' comments)

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#ifndef DIFF_TOLERANCE
    #define DIFF_TOLERANCE      2       // Default per-channel delta at or below which pixels count as equal
#endif
#define MAX_RESULTS          8192
#define MAX_EXCLUDES          256

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
typedef enum { ST_MATCH = 0, ST_DIFF, ST_SIZE, ST_MISSING, ST_EXCLUDED, ST_TOLERATED } Status;

typedef struct {
    char example[192];          // Example name (subfolder)
    char frame[80];             // Frame file name
    char refPath[512];          // Reference image path
    char cmpPath[512];          // Candidate image path
    char diffPath[512];         // Diff image output path
    Status status;              // Comparison outcome
    long diffPixels;            // Pixels differing beyond tolerance
    long totalPixels;           // Total pixels compared
    long allowance;             // Per-frame differing-pixel budget (0 = bit-exact required)
    int maxDiff;                // Maximum per-channel delta found
    int w, h, cw, ch;           // Reference and candidate dimensions
} Result;

//----------------------------------------------------------------------------------
// Module Variables Definition (local to this module)
//----------------------------------------------------------------------------------
static Result results[MAX_RESULTS];
static int resultCount = 0;

static char excludes[MAX_EXCLUDES][192];
static int excludeCount = 0;

static char includes[MAX_EXCLUDES][192];        // Subset filter: when non-empty, only these examples are compared
static int includeCount = 0;

static char allowNames[MAX_EXCLUDES][192];      // Examples with an expected-variability budget
static long allowBudgets[MAX_EXCLUDES];         // Per-frame differing-pixel budget for each
static int allowCount = 0;

static int toleranceLevel = DIFF_TOLERANCE;     // Runtime per-channel tolerance (from config)
static int spatialTolerance = 0;                // Spatial slack (px): a differing pixel is accepted
                                                // if both images match within this radius — absorbs
                                                // 1px line/curve rasterization tie-break shifts
                                                // between graphics drivers/APIs

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
static void ParseNameList(const char *csv, char (*list)[192], int *count);  // Parse a comma-separated name list
static bool IsExcluded(const char *example);                // Check if an example is excluded
static bool IsIncluded(const char *example);                // Check if an example passes the subset filter
static void ParseAllow(const char *text);                   // Parse an 'allow <example> <maxPixels>' entry
static long GetAllowance(const char *example);              // Per-frame pixel budget (0 = none)
static bool BytesEqual(const char *a, const char *b);       // Compare two files byte-for-byte
static void MakeDiffImage(Result *r);                       // Compute metrics and write a diff image
static int CompareResults(const void *a, const void *b);    // qsort comparator (example, then frame)
static void WriteHtml(const char *htmlOut, int nMatch, int nDiff, int nSize, int nMiss, int nExcl, int nTol);

//----------------------------------------------------------------------------------
// Program main entry point
//----------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    // Initialization
    //------------------------------------------------------------------------------------
    // CLI: image_comparison_diff [configFile]. All settings come from the rini config, relative
    // to the cwd; run this from the directory holding the ref/cmp folders.
    const char *configFile = (argc > 1) ? argv[1] : "image_comparison_rlvk.ini";

    char refDir[512], cmpDir[512], diffDir[512], htmlOut[512];
    {
        bool haveCfg = FileExists(configFile);
        rini_data cfg = { 0 };
        if (haveCfg) cfg = rini_load(configFile);
        snprintf(refDir,  sizeof(refDir),  "%s", rini_get_value_text_fallback(cfg, "ref_dir",  "src"));
        snprintf(cmpDir,  sizeof(cmpDir),  "%s", rini_get_value_text_fallback(cfg, "cmp_dir",  "rlgl"));
        snprintf(diffDir, sizeof(diffDir), "%s", rini_get_value_text_fallback(cfg, "diff_dir", "diffs"));
        snprintf(htmlOut, sizeof(htmlOut), "%s", rini_get_value_text_fallback(cfg, "report",   "report.html"));
        toleranceLevel = rini_get_value_fallback(cfg, "tolerance", DIFF_TOLERANCE);
        spatialTolerance = rini_get_value_fallback(cfg, "spatial_tolerance", 0);
        // Collect every 'exclude' and 'include' entry (rini keeps duplicate keys), so they can
        // be listed one per line; each value may itself be a comma-separated list.
        for (unsigned int e = 0; e < cfg.count; e++)
            if (strcmp(cfg.entries[e].key, "exclude") == 0) ParseNameList(cfg.entries[e].text, excludes, &excludeCount);
        for (unsigned int e = 0; e < cfg.count; e++)
            if (strcmp(cfg.entries[e].key, "include") == 0) ParseNameList(cfg.entries[e].text, includes, &includeCount);
        // Collect every 'allow' entry: "<example> <maxDiffPixels>" per line
        for (unsigned int e = 0; e < cfg.count; e++)
            if (strcmp(cfg.entries[e].key, "allow") == 0) ParseAllow(cfg.entries[e].text);
        if (haveCfg) rini_unload(&cfg);
    }

    SetTraceLogLevel(LOG_WARNING);

    if (!DirectoryExists(refDir)) { printf("ERROR: reference dir not found: %s\n", refDir); return 1; }
    if (!DirectoryExists(cmpDir)) { printf("ERROR: candidate dir not found: %s\n", cmpDir); return 1; }
    MakeDirectory(diffDir);
    printf("Config: %s  |  tolerance=%d  |  spatial=%d  |  %d exclusion(s)  |  %d allowance(s)\n",
           configFile, toleranceLevel, spatialTolerance, excludeCount, allowCount);
    if (includeCount > 0) printf("SUBSET: comparing only the %d example(s) listed by 'include' (regression subset)\n", includeCount);
    printf("\n");
    //------------------------------------------------------------------------------------

    // Compare: walk reference example subfolders and match each frame against the candidate
    //------------------------------------------------------------------------------------
    FilePathList examples = LoadDirectoryFiles(refDir);
    for (unsigned int i = 0; i < examples.count; i++)
    {
        if (!DirectoryExists(examples.paths[i])) continue;

        char example[192];
        snprintf(example, sizeof(example), "%s", GetFileName(examples.paths[i]));
        if ((includeCount > 0) && !IsIncluded(example)) continue;   // subset run: not selected, not reported
        bool excluded = IsExcluded(example);

        FilePathList frames = LoadDirectoryFiles(examples.paths[i]);
        for (unsigned int f = 0; f < frames.count; f++)
        {
            if (!IsFileExtension(frames.paths[f], ".png")) continue;
            if (resultCount >= MAX_RESULTS) break;

            char frame[80];
            snprintf(frame, sizeof(frame), "%s", GetFileName(frames.paths[f]));

            Result *r = &results[resultCount++];
            memset(r, 0, sizeof(*r));
            snprintf(r->example, sizeof(r->example), "%s", example);
            snprintf(r->frame, sizeof(r->frame), "%s", frame);
            snprintf(r->refPath, sizeof(r->refPath), "%s/%s/%s", refDir, example, frame);
            snprintf(r->cmpPath, sizeof(r->cmpPath), "%s/%s/%s", cmpDir, example, frame);
            snprintf(r->diffPath, sizeof(r->diffPath), "%s/%s/%s", diffDir, example, frame);

            if (excluded) { r->status = ST_EXCLUDED; continue; }
            if (!FileExists(r->cmpPath)) { r->status = ST_MISSING; continue; }
            r->allowance = GetAllowance(example);
            if (BytesEqual(r->refPath, r->cmpPath)) { r->status = ST_MATCH; continue; }

            MakeDirectory(TextFormat("%s/%s", diffDir, example));
            r->status = ST_DIFF;
            MakeDiffImage(r);   // may downgrade to ST_MATCH (within tolerance) or ST_SIZE
        }
        UnloadDirectoryFiles(frames);
    }
    UnloadDirectoryFiles(examples);

    qsort(results, resultCount, sizeof(Result), CompareResults);
    //------------------------------------------------------------------------------------

    // Report: print PASS / FAIL / SKIP per comparison, then write the HTML report
    //------------------------------------------------------------------------------------
    int nMatch = 0, nDiff = 0, nSize = 0, nMiss = 0, nExcl = 0, nTol = 0;
    for (int i = 0; i < resultCount; i++)
    {
        Result *r = &results[i];
        switch (r->status)
        {
            case ST_MATCH:    nMatch++; printf("PASS  %s/%s\n", r->example, r->frame); break;
            case ST_TOLERATED: nTol++;  printf("PASS~ %s/%s  (%ld/%ld px differ, max delta %d — within allowance %ld)\n",
                                               r->example, r->frame, r->diffPixels, r->totalPixels, r->maxDiff, r->allowance); break;
            case ST_DIFF:     nDiff++;  printf("FAIL  %s/%s  (%ld/%ld px differ, max delta %d%s)\n",
                                               r->example, r->frame, r->diffPixels, r->totalPixels, r->maxDiff,
                                               (r->allowance > 0) ? TextFormat(" — EXCEEDS allowance %ld", r->allowance) : ""); break;
            case ST_SIZE:     nSize++;  printf("FAIL  %s/%s  (size mismatch %dx%d vs %dx%d)\n",
                                               r->example, r->frame, r->w, r->h, r->cw, r->ch); break;
            case ST_MISSING:  nMiss++;  printf("FAIL  %s/%s  (candidate missing)\n", r->example, r->frame); break;
            case ST_EXCLUDED: nExcl++;  printf("SKIP  %s/%s  (excluded: inherently non-deterministic)\n", r->example, r->frame); break;
        }
    }

    WriteHtml(htmlOut, nMatch, nDiff, nSize, nMiss, nExcl, nTol);

    int failures = nDiff + nSize + nMiss;
    printf("\n==================================================\n");
    printf("%s  --  total=%d  pass=%d  tolerated=%d  fail=%d  skip=%d  (diffs=%d size=%d missing=%d)\n",
           (failures == 0) ? "OVERALL: PASS" : "OVERALL: FAIL",
           resultCount, nMatch, nTol, failures, nExcl, nDiff, nSize, nMiss);
    printf("HTML report: %s\n", htmlOut);
    //------------------------------------------------------------------------------------

    return (failures == 0) ? 0 : 2;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------

// Parse a comma-separated list of example names (a rini "exclude"/"include" value) into a name list
static void ParseNameList(const char *csv, char (*list)[192], int *count)
{
    if ((csv == NULL) || (csv[0] == '\0')) return;

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s", csv);
    char *token = strtok(buffer, ",");
    while ((token != NULL) && (*count < MAX_EXCLUDES))
    {
        while ((*token == ' ') || (*token == '\t')) token++;    // trim leading space
        int len = (int)strlen(token);
        while ((len > 0) && ((token[len-1] == ' ') || (token[len-1] == '\t'))) token[--len] = '\0';
        if (len > 0) snprintf(list[(*count)++], 192, "%s", token);
        token = strtok(NULL, ",");
    }
}

// Check if an example name is in the exclusion list
static bool IsExcluded(const char *example)
{
    for (int i = 0; i < excludeCount; i++) if (strcmp(excludes[i], example) == 0) return true;
    return false;
}

// Check if an example name passes the subset filter (empty include list = everything passes)
static bool IsIncluded(const char *example)
{
    for (int i = 0; i < includeCount; i++) if (strcmp(includes[i], example) == 0) return true;
    return false;
}

// Parse an 'allow' entry: "<example> <maxDiffPixels>" (whitespace separated)
static void ParseAllow(const char *text)
{
    if ((text == NULL) || (text[0] == '\0') || (allowCount >= MAX_EXCLUDES)) return;

    char name[192] = { 0 };
    long budget = 0;
    if (sscanf(text, "%191s %ld", name, &budget) == 2 && budget > 0)
    {
        snprintf(allowNames[allowCount], 192, "%s", name);
        allowBudgets[allowCount] = budget;
        allowCount++;
    }
    else printf("WARNING: malformed allow entry ignored: '%s' (expected '<example> <maxPixels>')\n", text);
}

// Per-frame differing-pixel budget for an example (0 = bit-exact required)
static long GetAllowance(const char *example)
{
    for (int i = 0; i < allowCount; i++) if (strcmp(allowNames[i], example) == 0) return allowBudgets[i];
    return 0;
}

// Two files are equal if their bytes are equal (identical PNG encodes identical pixels)
static bool BytesEqual(const char *a, const char *b)
{
    int sa = 0, sb = 0;
    unsigned char *da = LoadFileData(a, &sa);
    unsigned char *db = LoadFileData(b, &sb);
    bool eq = (da != NULL) && (db != NULL) && (sa == sb) && (memcmp(da, db, (size_t)sa) == 0);
    if (da != NULL) UnloadFileData(da);
    if (db != NULL) UnloadFileData(db);
    return eq;
}

// Load both images, compute pixel-difference metrics (with tolerance) and write a diff image
// NOTE: Sets ST_SIZE on dimension mismatch, or downgrades to ST_MATCH if all diffs are within tolerance
static void MakeDiffImage(Result *r)
{
    Image ia = LoadImage(r->refPath);
    Image ib = LoadImage(r->cmpPath);
    r->w = ia.width; r->h = ia.height; r->cw = ib.width; r->ch = ib.height;

    if ((ia.width != ib.width) || (ia.height != ib.height) || (ia.width == 0) || (ib.width == 0))
    {
        r->status = ST_SIZE;
        UnloadImage(ia); UnloadImage(ib);
        return;
    }

    int w = ia.width, h = ia.height;
    long total = (long)w*h;
    Color *ca = LoadImageColors(ia);
    Color *cb = LoadImageColors(ib);
    Color *cd = (Color *)malloc((size_t)total*sizeof(Color));

    long diffPixels = 0;
    int maxDiff = 0;
    for (long p = 0; p < total; p++)
    {
        int dr = ca[p].r - cb[p].r; if (dr < 0) dr = -dr;
        int dg = ca[p].g - cb[p].g; if (dg < 0) dg = -dg;
        int db = ca[p].b - cb[p].b; if (db < 0) db = -db;
        int da = ca[p].a - cb[p].a; if (da < 0) da = -da;

        int m = dr; if (dg > m) m = dg; if (db > m) m = db; if (da > m) m = da;
        if (m > maxDiff) maxDiff = m;
        if (m > toleranceLevel)
        {
            // Spatial slack: a 1px-shifted line/curve edge (driver rasterization tie-break) is not
            // a real difference. The pixel only counts as differing when either direction fails to
            // find a match within the radius: ref(p) must appear somewhere in cmp's neighborhood
            // AND cmp(p) must appear somewhere in ref's neighborhood (symmetry catches content
            // that was added as well as removed).
            bool spatialMatch = false;
            if (spatialTolerance > 0)
            {
                int x = (int)(p%w), y = (int)(p/w);
                bool refFound = false, cmpFound = false;
                for (int oy = -spatialTolerance; (oy <= spatialTolerance) && !(refFound && cmpFound); oy++)
                {
                    int ny = y + oy;
                    if ((ny < 0) || (ny >= h)) continue;
                    for (int ox = -spatialTolerance; ox <= spatialTolerance; ox++)
                    {
                        int nx = x + ox;
                        if ((nx < 0) || (nx >= w)) continue;
                        long q = (long)ny*w + nx;

                        int e = 0, d;
                        d = ca[p].r - cb[q].r; if (d < 0) d = -d; if (d > e) e = d;
                        d = ca[p].g - cb[q].g; if (d < 0) d = -d; if (d > e) e = d;
                        d = ca[p].b - cb[q].b; if (d < 0) d = -d; if (d > e) e = d;
                        d = ca[p].a - cb[q].a; if (d < 0) d = -d; if (d > e) e = d;
                        if (e <= toleranceLevel) refFound = true;

                        e = 0;
                        d = cb[p].r - ca[q].r; if (d < 0) d = -d; if (d > e) e = d;
                        d = cb[p].g - ca[q].g; if (d < 0) d = -d; if (d > e) e = d;
                        d = cb[p].b - ca[q].b; if (d < 0) d = -d; if (d > e) e = d;
                        d = cb[p].a - ca[q].a; if (d < 0) d = -d; if (d > e) e = d;
                        if (e <= toleranceLevel) cmpFound = true;

                        if (refFound && cmpFound) break;
                    }
                }
                spatialMatch = refFound && cmpFound;
            }
            if (!spatialMatch) diffPixels++;
        }

        int rr = dr*8; if (rr > 255) rr = 255;
        int gg = dg*8; if (gg > 255) gg = 255;
        int bb = db*8; if (bb > 255) bb = 255;
        cd[p] = (Color){ (unsigned char)rr, (unsigned char)gg, (unsigned char)bb, 255 };
    }

    r->diffPixels = diffPixels;
    r->totalPixels = total;
    r->maxDiff = maxDiff;

    if (diffPixels == 0) r->status = ST_MATCH;   // differences all within tolerance -> pass
    else
    {
        // Expected-variability allowance: within the example's per-frame budget the frame is
        // tolerated (still reported and shown in the HTML, distinct from a bit-exact pass)
        if ((r->allowance > 0) && (diffPixels <= r->allowance)) r->status = ST_TOLERATED;
        Image diff = { cd, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        ExportImage(diff, r->diffPath);
    }

    free(cd);
    UnloadImageColors(ca);
    UnloadImageColors(cb);
    UnloadImage(ia);
    UnloadImage(ib);
}

// qsort comparator: order results by example name, then frame name
static int CompareResults(const void *a, const void *b)
{
    const Result *x = (const Result *)a, *y = (const Result *)b;
    int c = strcmp(x->example, y->example);
    if (c != 0) return c;
    return strcmp(x->frame, y->frame);
}

// Write the HTML report: summary, differences (with images), skipped and matched lists
static void WriteHtml(const char *htmlOut, int nMatch, int nDiff, int nSize, int nMiss, int nExcl, int nTol)
{
    FILE *o = fopen(htmlOut, "wb");
    if (o == NULL) { printf("ERROR: cannot write %s\n", htmlOut); return; }

    int failures = nDiff + nSize + nMiss;

    fprintf(o, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n");
    fprintf(o, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
    fprintf(o, "<title>raylib image comparison</title>\n<style>\n");
    fprintf(o, "  :root{color-scheme:dark light} body{font-family:system-ui,Segoe UI,Roboto,sans-serif;margin:0;padding:24px;background:#14161a;color:#e6e6e6}\n");
    fprintf(o, "  h1{margin:0 0 4px} .sub{color:#9aa0a6;margin-bottom:20px}\n");
    fprintf(o, "  .summary{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:24px}\n");
    fprintf(o, "  .pill{padding:10px 16px;border-radius:10px;font-weight:600}\n");
    fprintf(o, "  .pill.ok{background:#123d1a;color:#5ee27a} .pill.bad{background:#3d1212;color:#ff8080} .pill.neutral{background:#22262c;color:#cfd3d8} .pill.warn{background:#3d3212;color:#e0b64a}\n");
    fprintf(o, "  .verdict{font-size:20px;font-weight:700;padding:12px 18px;border-radius:10px;display:inline-block;margin-bottom:24px}\n");
    fprintf(o, "  .verdict.pass{background:#123d1a;color:#5ee27a} .verdict.fail{background:#3d1212;color:#ff8080}\n");
    fprintf(o, "  h2{border-bottom:1px solid #2a2e35;padding-bottom:6px;margin-top:32px}\n");
    fprintf(o, "  .card{background:#1c1f25;border:1px solid #2a2e35;border-radius:10px;padding:16px;margin:16px 0}\n");
    fprintf(o, "  .card h3{margin:0 0 4px} .meta{color:#9aa0a6;font-size:14px;margin-bottom:12px}\n");
    fprintf(o, "  .imgs{display:flex;gap:16px;flex-wrap:wrap} .imgs figure{margin:0} .imgs img{max-width:360px;width:100%%;border:1px solid #2a2e35;background:#000;image-rendering:pixelated}\n");
    fprintf(o, "  figcaption{font-size:13px;color:#9aa0a6;margin-top:4px}\n");
    fprintf(o, "  .list{columns:3;column-gap:24px} .list div{font-size:13px;break-inside:avoid} .ok{color:#5ee27a} .excl{color:#e0b64a}\n");
    fprintf(o, "  details>summary{cursor:pointer;font-size:18px;font-weight:700;margin-top:24px}\n");
    fprintf(o, "</style>\n</head>\n<body>\n");

    fprintf(o, "<h1>raylib image comparison</h1>\n");
    fprintf(o, "<div class=\"sub\">reference vs candidate deterministic frame captures (per-channel tolerance %d)</div>\n", toleranceLevel);

    fprintf(o, "<div class=\"verdict %s\">%s</div>\n", (failures == 0) ? "pass" : "fail",
            (failures == 0) ? "ALL PASS" : "FAIL");

    fprintf(o, "<div class=\"summary\">\n");
    fprintf(o, "  <div class=\"pill ok\">%d matched</div>\n", nMatch);
    fprintf(o, "  <div class=\"pill %s\">%d pixel diffs</div>\n", (nDiff ? "bad" : "neutral"), nDiff);
    fprintf(o, "  <div class=\"pill %s\">%d size mismatch</div>\n", (nSize ? "bad" : "neutral"), nSize);
    fprintf(o, "  <div class=\"pill %s\">%d missing</div>\n", (nMiss ? "bad" : "neutral"), nMiss);
    fprintf(o, "  <div class=\"pill %s\">%d tolerated</div>\n", (nTol ? "warn" : "neutral"), nTol);
    fprintf(o, "  <div class=\"pill neutral\">%d skipped</div>\n", nExcl);
    fprintf(o, "</div>\n");

    if (failures > 0)
    {
        fprintf(o, "<h2>Differences (%d)</h2>\n", failures);
        for (int i = 0; i < resultCount; i++)
        {
            Result *r = &results[i];
            if ((r->status == ST_MATCH) || (r->status == ST_EXCLUDED) || (r->status == ST_TOLERATED)) continue;

            fprintf(o, "<div class=\"card\">\n<h3>%s / %s</h3>\n", r->example, r->frame);
            if (r->status == ST_DIFF)
                fprintf(o, "<div class=\"meta\">%ld of %ld pixels differ (%.3f%%), max channel delta %d</div>\n",
                        r->diffPixels, r->totalPixels,
                        (r->totalPixels ? 100.0*(double)r->diffPixels/(double)r->totalPixels : 0.0), r->maxDiff);
            else if (r->status == ST_SIZE)
                fprintf(o, "<div class=\"meta\">size mismatch: reference %dx%d vs candidate %dx%d</div>\n",
                        r->w, r->h, r->cw, r->ch);
            else
                fprintf(o, "<div class=\"meta\">candidate image missing</div>\n");

            fprintf(o, "<div class=\"imgs\">\n");
            fprintf(o, "  <figure><img src=\"%s\" loading=\"lazy\"><figcaption>reference</figcaption></figure>\n", r->refPath);
            if (r->status != ST_MISSING)
                fprintf(o, "  <figure><img src=\"%s\" loading=\"lazy\"><figcaption>candidate</figcaption></figure>\n", r->cmpPath);
            if (r->status == ST_DIFF)
                fprintf(o, "  <figure><img src=\"%s\" loading=\"lazy\"><figcaption>diff (x8)</figcaption></figure>\n", r->diffPath);
            fprintf(o, "</div>\n</div>\n");
        }
    }

    if (nTol > 0)
    {
        fprintf(o, "<h2>Tolerated &mdash; expected variability, within allowance (%d)</h2>\n", nTol);
        for (int i = 0; i < resultCount; i++)
        {
            Result *r = &results[i];
            if (r->status != ST_TOLERATED) continue;

            fprintf(o, "<div class=\"card\">\n<h3>%s / %s</h3>\n", r->example, r->frame);
            fprintf(o, "<div class=\"meta\">%ld of %ld pixels differ (%.3f%%), max channel delta %d &mdash; allowance %ld</div>\n",
                    r->diffPixels, r->totalPixels,
                    (r->totalPixels ? 100.0*(double)r->diffPixels/(double)r->totalPixels : 0.0), r->maxDiff, r->allowance);
            fprintf(o, "<div class=\"imgs\">\n");
            fprintf(o, "  <figure><img src=\"%s\" loading=\"lazy\"><figcaption>reference</figcaption></figure>\n", r->refPath);
            fprintf(o, "  <figure><img src=\"%s\" loading=\"lazy\"><figcaption>candidate</figcaption></figure>\n", r->cmpPath);
            fprintf(o, "  <figure><img src=\"%s\" loading=\"lazy\"><figcaption>diff (x8)</figcaption></figure>\n", r->diffPath);
            fprintf(o, "</div>\n</div>\n");
        }
    }

    if (nExcl > 0)
    {
        fprintf(o, "<details>\n<summary>Skipped &mdash; inherently non-deterministic (%d)</summary>\n<div class=\"list\">\n", nExcl);
        for (int i = 0; i < resultCount; i++)
            if (results[i].status == ST_EXCLUDED)
                fprintf(o, "  <div class=\"excl\">&#8856; %s / %s</div>\n", results[i].example, results[i].frame);
        fprintf(o, "</div>\n</details>\n");
    }

    fprintf(o, "<details%s>\n<summary>Matched scenes (%d)</summary>\n<div class=\"list\">\n",
            (failures == 0) ? " open" : "", nMatch);
    for (int i = 0; i < resultCount; i++)
        if (results[i].status == ST_MATCH)
            fprintf(o, "  <div class=\"ok\">&#10003; %s / %s</div>\n", results[i].example, results[i].frame);
    fprintf(o, "</div>\n</details>\n");

    fprintf(o, "</body>\n</html>\n");
    fclose(o);
}

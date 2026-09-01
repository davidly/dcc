/**
 * @file dcc_app_strip.h
 * @brief Whole-program reachability stripping for dcc-generated app assembly.
 *
 * @par Role
 * Declares the in-place multi-module .MAC stripping pass used by dccrtlstrip.
 *
 * @par Boundary
 * dcc emits structural block markers; this module resolves references between
 * those blocks. Runtime block stripping remains owned by dccrtlstrip.c.
 */
#ifndef DCC_APP_STRIP_H
#define DCC_APP_STRIP_H

int dcc_strip_app_files(
    int file_count, char **paths, int root_count, char **roots);

#endif

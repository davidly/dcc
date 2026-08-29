#ifndef TMFCALL_H
#define TMFCALL_H

typedef int (*MfCallback)(int value);

extern MfCallback mf_callback_table[2];
int mf_dispatch_callback(int index, int value);

#endif

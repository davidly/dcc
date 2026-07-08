#include "./ponce_shared.h"

int bval(void)
{
    return shval() + PONCE_SHARED_MACRO;
}

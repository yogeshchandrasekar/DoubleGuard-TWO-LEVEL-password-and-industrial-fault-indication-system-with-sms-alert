#include <stdlib.h>

void RANDOM_INIT(void)
{
    srand(T1TC);
}

unsigned int RANDOM_NUMBER(void)
{
    return ((rand() % 10000));
}

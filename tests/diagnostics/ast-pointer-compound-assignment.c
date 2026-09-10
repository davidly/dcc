static int data[4];
static int *slots[2];

struct Holder {
    int *slots[2];
};

int main(void)
{
    struct Holder holder;
    int **cursor = slots;

    slots[0] = data;
    holder.slots[0] = data;
    slots[0] *= 2;
    slots[0] += data;
    holder.slots[0] &= 1;
    *cursor <<= 1;
    return 0;
}

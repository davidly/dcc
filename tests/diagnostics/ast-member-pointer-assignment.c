struct Sample {
    int word;
    char byte;
};

int main(void)
{
    struct Sample sample;
    int value;
    sample.word = &value;
    sample.byte = &value;
    return 0;
}

int global_values[32768];

struct Oversized {
    char member_values[256][256];
};

void function(void)
{
    char local_values[256][256];
}

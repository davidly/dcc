int external_value;

int main(void)
{
    for (static int local_static = 0; local_static < 1; local_static++) ;
    for (extern int external_value; external_value < 1; external_value++) ;
    for (typedef int LocalInt; 0; ) ;
    for (int function(void), object = 0; object < 1; object++) ;
    return external_value;
}
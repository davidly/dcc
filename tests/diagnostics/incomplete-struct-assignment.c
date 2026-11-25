/* Copying a struct whose members were never declared is reported, rather
 * than reaching code generation (directive seed 1014233). */
void copy(struct Missing *to, struct Missing *from)
{
    *to = *from;
}

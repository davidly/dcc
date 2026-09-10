#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "../../src/dcc/dcc_mir_select.c"
#include "dcc_mir_internal.h"

static int rejecting_candidate(MirStream *out)
{
    int label = new_label();

    mir_emit_runtime_call(out, "__mulu");
    mir_stream_printf(out, "L%d:\n\tdiscard\n", label);
    return 0;
}

static int accepting_candidate(MirStream *out)
{
    int label = new_label();

    mir_emit_runtime_call(out, "__mulu");
    mir_stream_printf(out, "L%d:\n\tret\n", label);
    return 1;
}

static size_t read_stream(MirStream *stream, char *text, size_t capacity)
{
    size_t bytes;

    mir_stream_rewind(stream);
    memset(text, 0, capacity);
    bytes = mir_stream_read(text, 1, capacity - 1, stream);
    if (bytes >= capacity - 1)
        fatal("selector isolation host fixture output overflow");
    return bytes;
}

int main(void)
{
    MirStream *control = mir_stream_open();
    MirStream *retry = mir_stream_open();
    char control_text[256];
    char retry_text[256];
    size_t control_bytes;
    size_t retry_bytes;
    int control_label_after;
    int accepted;
    int ok = control != NULL && retry != NULL;

    if (!ok)
        fatal("cannot create selector isolation host streams");

    label_id = 41;
    accepted = mir_try_selector(control, accepting_candidate);
    ok = ok && accepted == 1;
    control_label_after = label_id;
    control_bytes = read_stream(
        control, control_text, sizeof(control_text));
    ok = ok &&
         strstr(control_text, "\textrn __mulu\n\tcall __mulu\n") != NULL &&
         strstr(control_text, "L42:\n\tret\n") != NULL;

    label_id = 41;
    mir_stream_puts("prefix\n", retry);
    accepted = mir_try_selector(retry, rejecting_candidate);
    ok = ok && accepted == 0;
    ok = ok && mir_stream_size(retry) == 7 && label_id == 41;
    accepted = mir_try_selector(retry, accepting_candidate);
    ok = ok && accepted == 1;
    retry_bytes = read_stream(retry, retry_text, sizeof(retry_text));
    ok = ok && label_id == control_label_after;
    ok = ok && retry_bytes == control_bytes + 7;
    ok = ok && !memcmp(retry_text, "prefix\n", 7);
    ok = ok && !memcmp(
        retry_text + 7, control_text, control_bytes);

    mir_stream_close(retry);
    mir_stream_close(control);
    if (!ok) {
        fputs("FAIL selector candidate isolation\n", stderr);
        return 1;
    }
    puts("selector isolation host tests passed");
    return 0;
}

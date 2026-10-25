/* Source ownership regression, originally the standalone SCUA fault-name repro.
 * Covers static source, overwritten/freed host buffers and file loading, with
 * zero and four workers. Copies fault bytes INSIDE the hook. See SCUA_039_HANDOFF.md. */
#include <scua.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char name[128];
static size_t name_len;
static const char source[] = "partition Broken\n"
                             "  on Crash() assert(false,\"deliberate fault\") end\n"
                             "end\n"
                             "let broken=Broken()\n"
                             "fn go() tell broken.Crash() end\n";

static void on_fault(void *ctx, const scua_fault_info *f)
{
    (void)ctx;
    name_len = f->partition_len < sizeof(name) ? f->partition_len : sizeof(name);
    if (name_len)
        memcpy(name, f->partition, name_len);
}

static int run(const char *path, int workers, int release_input)
{
    scua_partition *p = scua_partition_new();
    char err[1024] = {0};
    name_len = 0;
    if (!p)
        return 0;
    char *owned = NULL;
    if (release_input)
    {
        owned = malloc(sizeof(source));
        if (!owned)
        {
            scua_partition_free(p);
            return 0;
        }
        memcpy(owned, source, sizeof(source));
    }
    int setup = scua_set_drain_mode(p, SCUA_DRAIN_EXPLICIT);
    if (!setup)
        setup = scua_set_clock(p, 0);
    if (!setup)
        setup = scua_set_workers(p, workers);
    if (!setup)
        setup = scua_set_fault_hook(p, on_fault, NULL);
    int loaded = setup  ? setup
                 : path ? scua_load_script_file(p, path, 64u << 20, 1000000, err, sizeof(err))
                        : scua_load_script(p, owned ? owned : source, sizeof(source) - 1, 64u << 20, 1000000,
                                           err, sizeof(err));
    if (owned)
    {
        /* A loaded script must not retain borrowed slices of this buffer. */
        volatile char *reused = owned;
        for (size_t i = 0; i < sizeof(source); ++i)
            reused[i] = (char)0xaa;
        free(owned);
    }
    int called = loaded ? loaded : scua_pcall(p, scua_get_global(p, "go"), NULL, 0, NULL, err, sizeof(err));
    scua_turn_budget b = {sizeof(b), 64, 0};
    scua_turn_report r = {sizeof(r)};
    int drained = called ? called : scua_run_turns(p, &b, &r, err, sizeof(err));
    printf("%s workers=%d load=%d call=%d drain=%d partition_len=%zu partition_hex=",
           path            ? "file"
           : release_input ? "released-memory"
                           : "memory",
           workers, loaded, called, drained, name_len);
    for (size_t i = 0; i < name_len; ++i)
        printf("%02x", name[i]);
    printf(" error=%s\n", err);
    scua_partition_free(p);
    return loaded == SCUA_OK && called == SCUA_OK && drained == SCUA_ERR_OTHER && name_len == 6 &&
           memcmp(name, "Broken", 6) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    /* Exclusively create the scratch file, never overwrite a caller's file. */
    FILE *f = fopen(argv[1], "wbx");
    if (!f)
        return 2;
    size_t written = fwrite(source, 1, sizeof(source) - 1, f);
    int closed = fclose(f);
    if (written != sizeof(source) - 1 || closed)
    {
        remove(argv[1]);
        return 2;
    }
    int correct = 0;
    for (int workers = 0; workers <= 4; workers += 4)
    {
        correct += run(NULL, workers, 0);
        correct += run(NULL, workers, 1);
        correct += run(argv[1], workers, 0);
    }
    remove(argv[1]);
    return correct == 6 ? 0 : 1;
}

#include <furi.h>

// Build parent for the cli_treadcap plugin; run `treadcap` from the CLI instead.
int32_t treadcap_app(void* p) {
    UNUSED(p);
    return 0;
}

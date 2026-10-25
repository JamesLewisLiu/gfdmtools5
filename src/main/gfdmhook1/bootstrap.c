#include <stdio.h>
#include <string.h>

#include "gfdmhook1/bootstrap.h"
#include "hook/iohook.h"
#include "hooklib/memfile.h"

static const char boot_xml[] =
    "<?xml version=\"1.0\" encoding=\"shift-jis\"?>\n"
    "<config><ea3_conf><identity>"
    "<pcbid __type=\"str\">0101020304050607086F</pcbid>"
    "<autoconf __type=\"bool\">1</autoconf>"
    "</identity><network>"
    "<services "
    "__type=\"str\">https://eamuse.konami.fun/service/services/</services>"
    "<url_slash __type=\"bool\">0</url_slash>"
    "</network></ea3_conf><avs_conf><nvram>"
    "<enable_win32fs __type=\"bool\">1</enable_win32fs>"
    "</nvram><rootfs><case_sensitive __type=\"bool\">0</case_sensitive>"
    "</rootfs><sntp><enable_ntp __type=\"bool\">1</enable_ntp></sntp><log>"
    "<level __type=\"str\">misc</level>"
    "<enable_file __type=\"bool\">1</enable_file>"
    "<enable_console __type=\"bool\">1</enable_console>"
    "</log></avs_conf></config>\n";

static char identity_xml[512];

bool gfdm_bootstrap_init(const struct gfdm_profile *profile, bool guitar)
{
    const char *mcode = guitar ? profile->mcode_gf : profile->mcode_dm;
    int size;

    /* The existing profiles carry GC + model +
     * destination/specification/revision. */
    if (strlen(mcode) != 8) {
        return false;
    }
    size = snprintf(
        identity_xml,
        sizeof(identity_xml),
        "<?xml version=\"1.0\" encoding=\"shift-jis\"?>\n"
        "<ea3_conf><soft><model __type=\"str\">%.3s</model>"
        "<dest __type=\"str\">%c</dest><spec __type=\"str\">%c</spec>"
        "<rev __type=\"str\">%c</rev><ext __type=\"str\"></ext>"
        "</soft></ea3_conf>\n",
        mcode + 2,
        mcode[5],
        mcode[6],
        mcode[7]);
    if (size < 0 || (size_t) size >= sizeof(identity_xml)) {
        return false;
    }

    memfile_hook_init();
    memfile_hook_add_fd(
        "gfdm-boot.xml", ENDING_MATCH, boot_xml, sizeof(boot_xml) - 1);
    memfile_hook_add_fd("gfdm-ea3-ident.xml", ENDING_MATCH, identity_xml, size);
    return SUCCEEDED(iohook_push_handler(memfile_hook_dispatch_irp));
}

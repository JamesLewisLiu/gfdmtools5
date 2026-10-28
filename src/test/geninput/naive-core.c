/* Compile the production input stack without exporting test-only DLL APIs.
   Keep object/dependency paths flat for the existing module build rules. */
#include "geninput/midi-state.c"
#include "geninput/midi.c"
#include "config/snap.c"
#include "geninput/dev-list.c"
#include "geninput/hid-mgr.c"
#include "geninput/hid.c"
#include "geninput/kbd-data.c"
#include "geninput/mapper-s11n.c"
#include "geninput/mapper.c"

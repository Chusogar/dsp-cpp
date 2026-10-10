/* No embedded media: the ROMs, disks and cartridges come from the command
 * line. See tools/embed_media.py and DSP_EMBED_* in CMakeLists.txt. */
#include "core/embedded_media.h"

const DspEmbeddedFile dsp_embedded_files[] = {{0, 0, 0}};
const size_t dsp_embedded_file_count = 0;
const char* const dsp_embedded_args[] = {0};

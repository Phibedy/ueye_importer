#include <ueye_importer.h>

extern "C" {
void* getInstance () {
    return new lms_ueye_importer::UeyeImporter();
}
}

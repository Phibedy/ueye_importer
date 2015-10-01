#pragma once

#include <lms/datamanager.h>
#include <lms/module.h>
#include <lms/module_config.h>
#include <lms/imaging/image.h>

#include "ueye_camera.h"

namespace lms_ueye_importer {

class UeyeImporter : public lms::Module {
public:

    bool initialize();
    bool deinitialize();

    bool cycle();

protected:

    const lms::ModuleConfig* config;
    lms::imaging::Image* imagePtr;
    
    UeyeCamera* camera;
};

}  // namespace lms_ueye_importer

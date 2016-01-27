#pragma once

#include <lms/module.h>
#include <lms/config.h>
#include <lms/imaging/image.h>

#include "ueye_camera.h"

namespace lms_ueye_importer {

class UeyeImporter : public lms::Module {
public:

    bool initialize();
    bool deinitialize();

    bool cycle();
    void configsChanged() override;

protected:

    lms::WriteDataChannel<lms::imaging::Image> imagePtr;
    
    UeyeCamera* camera;
};

}  // namespace lms_ueye_importer

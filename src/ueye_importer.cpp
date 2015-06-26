#include <iomanip>

#include <lms/type/module_config.h>
#include "lms/messaging.h"

#include "ueye_importer.h"

namespace lms_ueye_importer {

bool UeyeImporter::initialize() {
    logger.info() << "Init: UeyeImporter";
    
    // init camera
    camera = new UeyeCamera(&logger);
    
    if(!camera->open()){
        return false;
    }
    
    // Print camera information
    camera->info();
    
    // Set config
    config = getConfig("ueye");
    
    camera->setNumBuffers( config->get<size_t>("num_buffers") );
    
    camera->setAOI(
        config->get<size_t>("width"),
        config->get<size_t>("height"),
        config->get<size_t>("offset_x"),
        config->get<size_t>("offset_y")
    );
    camera->setPixelClock( config->get<int>("pixelclock") );
    auto fps = camera->setFrameRate( config->get<double>("framerate") );
    auto exposure = camera->setExposure( config->get<double>("exposure") );
    camera->setHardwareGamma( config->get<bool>("hardware_gamma") );
    camera->setGamma( config->get<double>("gamma") );
    camera->setGainBoost( config->get<bool>("gain_boost") );
    
    if( config->get<bool>("gain_auto") )
    {
        camera->setAutoGain();
    } else {
        camera->setGain( config->get<int>("gain") );
    }
    
    // camera->setGlobalShutter( config->get<bool>("global_shutter") );
    camera->setBlacklevel(
        config->get<bool>("blacklevel_auto"),
        config->get<int>("blacklevel_offset")
    );
    
    camera->setEdgeEnhancement( config->get<int>("edge_enhancement") );
    
    // TODO: fancy HDR stuff
    
    // Initialize buffers and stuff
    camera->init();
    
    // Get data channels with actual size and format
    imagePtr = datamanager()->writeChannel<lms::imaging::Image>(this, "CAMERA_IMAGE");
    imagePtr->resize(camera->getWidth(), camera->getHeight(), lms::imaging::Format::GREY);
    
    // Start capturing
    camera->start();
    
    logger.info()   << "Starting uEye Camera: "
                    << camera->getWidth() << "x" << camera->getHeight()
                    << std::setprecision(5)
                    << " @ " << fps << " fps"
                    << " (exposure: " << exposure << " ms)";
    
    return true;
}

bool UeyeImporter::deinitialize() {
    logger.info("deinit") << "Deinit: UeyeImporter";

    camera->stop();
    camera->deinit();
    camera->close();
    delete camera;

    return true;
}

bool UeyeImporter::cycle () {
    if(!camera->isInitialized())
    {
        return false;
    }
    
    logger.debug("cycle") << "uEyeImporter::cycle";
    
    // Wait for new frame event...
    if(!camera->waitForFrame(config->get<float>("timeOut",20))){
        messaging()->send("STOP_CAR","Stop it honey <3");
        std::cout<<"CAM FAILED HARD BRO"<<std::endl;
    }
    
    if(!camera->captureImage( *imagePtr ))
    {
        return false;
    }
    
    return true;
}

}

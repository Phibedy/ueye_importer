#include <iomanip>
#include <lms/type/module_config.h>
#include "lms/messaging.h"

#include "ueye_importer.h"

namespace lms_ueye_importer {

bool UeyeImporter::initialize() {
    logger.info() << "Init: UeyeImporter";
    
    // init camera
    camera = new UeyeCamera(logger);


    //use the timeout to give the cam some time (needed for fast restart as the device will be busy from the last run (because of soem reason I don't know))
    bool camAvailable = false;
    lms::Time start = lms::Time::now();
    do{
        if(camera->open()){
            camAvailable = true;
            break;
        }
    }while(lms::Time::since(start).toFloat<std::milli>() < config().get<int>("initTimeout",0));
    //return if the cam can't be accessed
    if(!camAvailable){
        return false;
    }
    
    // Print camera information
    camera->info();
    
    // Set config
    camera->setNumBuffers( config().get<size_t>("num_buffers") );
    
    camera->setAOI(
        config().get<size_t>("width"),
        config().get<size_t>("height"),
        config().get<size_t>("offset_x"),
        config().get<size_t>("offset_y")
    );
    camera->setPixelClock( config().get<int>("pixelclock") );
    auto fps = camera->setFrameRate( config().get<double>("framerate") );
    auto exposure = camera->setExposure( config().get<double>("exposure") );
    camera->setHardwareGamma( config().get<bool>("hardware_gamma") );
    camera->setGamma( config().get<double>("gamma") );
    camera->setGainBoost( config().get<bool>("gain_boost") );
    
    if( config().get<bool>("gain_auto") )
    {
        camera->setAutoGain();
    } else {
        camera->setGain( config().get<int>("gain") );
    }
    
    // camera->setGlobalShutter( config().get<bool>("global_shutter") );
    camera->setBlacklevel(
        config().get<bool>("blacklevel_auto"),
        config().get<int>("blacklevel_offset")
    );
    
    camera->setEdgeEnhancement( config().get<int>("edge_enhancement") );
    
    // HDR mode
    {
        auto kneepointsX = config().getArray<double>("hdr_kneepoints_x");
        auto kneepointsY = config().getArray<double>("hdr_kneepoints_y");
        
        if( kneepointsX.size() != kneepointsY.size() )
        {
            logger.warn("hdr_kneepoints")
                << "Number of X and Y values for HDR kneepoints differ!"
                << "( x: " << kneepointsX.size() << ", y:" << kneepointsY.size() << " )";
        }
        
        std::vector< std::pair<double, double> > kneepoints;
        
        auto xIt = kneepointsX.begin();
        auto yIt = kneepointsY.begin();
        
        while( xIt != kneepointsX.end() && yIt != kneepointsY.end() )
        {
            kneepoints.push_back( std::make_pair( *xIt++, *yIt++ ) );
        }
        
        if( kneepoints.size() > 0 )
        {
            // Set Kneepoints...
            camera->setHDRKneepoints( kneepoints );
            
            // ... and enable HDR
            camera->setHDR( true );
        }
        else
        {
            // Disable HDR
            camera->setHDR( false );
        }
    }
    
    // Initialize buffers and stuff
    camera->init();
    
    // Get data channels with actual size and format
    imagePtr = writeChannel<lms::imaging::Image>("CAMERA_IMAGE");
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
    if(!camera->isInitialized()){
        return false;
    }
    
    // Wait for new frame event...
    if(!camera->waitForFrame(config().get<float>("timeOut",20))){
        messaging()->send("CAM_FAILED","Stop it honey <3");
        logger.error("cycle.waitForFrame")<<"Cam failed, code: "<<camera->getErrorCode()<<" Error: " <<camera->getError();
        return false;
    }
    
    if(!camera->captureImage( *imagePtr )){
        logger.error("cycle.captureImage")<<"Cam failed, code: "<<camera->getErrorCode()<<" Error: " <<camera->getError();
        return false;
    }
    
    return true;
}

void UeyeImporter::configsChanged(){
    logger.info() << "ConfigsChanged: UeyeImporter";

    // Stop capturing before changing settings
    //camera->stop();

    // Print camera information
    //camera->info();

    // Set config
    //camera->setNumBuffers( config().get<size_t>("num_buffers") );
    /*
    camera->setAOI(
        config().get<size_t>("width"),
        config().get<size_t>("height"),
        config().get<size_t>("offset_x"),
        config().get<size_t>("offset_y")
    );
    */
    camera->setPixelClock( config().get<int>("pixelclock") );
    auto fps = camera->setFrameRate( config().get<double>("framerate") );
    auto exposure = camera->setExposure( config().get<double>("exposure") );
    camera->setHardwareGamma( config().get<bool>("hardware_gamma") );
    camera->setGamma( config().get<double>("gamma") );
    camera->setGainBoost( config().get<bool>("gain_boost") );

    if( config().get<bool>("gain_auto") )
    {
        camera->setAutoGain();
    } else {
        camera->setGain( config().get<int>("gain") );
    }

    // camera->setGlobalShutter( config().get<bool>("global_shutter") );
    camera->setBlacklevel(
        config().get<bool>("blacklevel_auto"),
        config().get<int>("blacklevel_offset")
    );

    camera->setEdgeEnhancement( config().get<int>("edge_enhancement") );

    // HDR mode
    {
        auto kneepointsX = config().getArray<double>("hdr_kneepoints_x");
        auto kneepointsY = config().getArray<double>("hdr_kneepoints_y");

        if( kneepointsX.size() != kneepointsY.size() )
        {
            logger.warn("hdr_kneepoints")
                << "Number of X and Y values for HDR kneepoints differ!"
                << "( x: " << kneepointsX.size() << ", y:" << kneepointsY.size() << " )";
        }

        std::vector< std::pair<double, double> > kneepoints;

        auto xIt = kneepointsX.begin();
        auto yIt = kneepointsY.begin();

        while( xIt != kneepointsX.end() && yIt != kneepointsY.end() )
        {
            kneepoints.push_back( std::make_pair( *xIt++, *yIt++ ) );
        }

        if( kneepoints.size() > 0 )
        {
            // Set Kneepoints...
            camera->setHDRKneepoints( kneepoints );

            // ... and enable HDR
            camera->setHDR( true );
        }
        else
        {
            // Disable HDR
            camera->setHDR( false );
        }
    }

    // Initialize buffers and stuff
    //camera->init();

    // Get data channels with actual size and format
    //imagePtr = datamanager()->writeChannel<lms::imaging::Image>(this, "CAMERA_IMAGE");
    //imagePtr->resize(camera->getWidth(), camera->getHeight(), lms::imaging::Format::GREY);writeChannel

    // Start capturing
    //camera->start();

    logger.info()   << "Starting uEye Camera: "
                    << camera->getWidth() << "x" << camera->getHeight()
                    << std::setprecision(5)
                    << " @ " << fps << " fps"
                    << " (exposure: " << exposure << " ms)";

}

}

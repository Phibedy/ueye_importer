#include "ueye_camera.h"
#include "lms/extra/time.h"

#define CHECK_STATUS(NAME) if( IS_SUCCESS != status ) { logger.error(NAME) << getError(); }

namespace lms_ueye_importer
{

std::unordered_map<INT, std::string> UeyeCamera::errorCodes;

UeyeCamera::UeyeCamera(lms::logging::Logger &logger)  :
    logger(logger),
    handle(0),
    status(IS_SUCCESS),
    format(IS_CM_MONO8),
    width(0),
    height(0),
    numBuffers(8),
    initialized(false),
    capturing(false)
{
    initErrorCodes();
}

UeyeCamera::~UeyeCamera()
{
    close();
}

bool UeyeCamera::open()
{
    // set camera handle id (0 = auto)
    status = is_InitCamera(&handle, NULL);
    CHECK_STATUS("InitCamera")
            return ( IS_SUCCESS == status );
}

bool UeyeCamera::close()
{
    if( 0 == handle )
    {
        // not opened
        return false;
    }

    deinit();
    
    status = is_ExitCamera(handle);
    CHECK_STATUS("ExitCamera")
            if( IS_SUCCESS == status )
    {
        handle = 0;
        return true;
    }
    return false;
}

bool UeyeCamera::init()
{
    if( initialized )
    {
        logger.warn("init") << "Already initialized";
        return false;
    }
    
    if( 0 == handle )
    {
        // Camera not opened yet
        logger.error("init") << "Camera not yet opened";
        return false;
    }
    
    initParameters();
    
    // Read back actual image size and format
    IS_SIZE_2D size;
    if(IS_SUCCESS == is_AOI(handle, IS_AOI_IMAGE_GET_SIZE, (void*)&size, sizeof(size)))
    {
        width  = size.s32Width;
        height = size.s32Height;
    }
    else
    {
        logger.error() << "Error reading image size and format from camera";
        return false;
    }
    
    // Initialize buffers
    for( size_t i = 0; i < numBuffers; ++i )
    {
        char* ptr;
        INT id;
        status = is_AllocImageMem(handle, width, height, getBPP() * 8, &ptr, &id);
        CHECK_STATUS("AllocImageMem")

                status = is_AddToSequence(handle, ptr, id);
        CHECK_STATUS("AddToSequence")

                // save reference to new buffer
                buffers[ptr] = id;
    }
    
    // Reset capture status
    status = is_CaptureStatus(handle, IS_CAPTURE_STATUS_INFO_CMD_RESET, NULL, 0);
    CHECK_STATUS("ResetCaptureStatus")

            initialized = true;
    return true;
}

bool UeyeCamera::deinit()
{
    if( !initialized )
    {
        return false;
    }
    
    // Stop capture
    stop();
    
    // print capture status
    logCaptureStatus();
    
    // Clear buffers
    status = is_ClearSequence(handle);
    CHECK_STATUS("ClearSequence")

            // Dealloc buffers
            for( auto& buf : buffers )
    {
        status = is_FreeImageMem(handle, buf.first /* ptr */, buf.second /* id */);
        CHECK_STATUS("FreeImageMem")
    }
    buffers.clear();
    
    // Disable events
    status = is_DisableEvent(handle, IS_SET_EVENT_FRAME);
    CHECK_STATUS("DisableFrameEvent")

            // status = is_DisableEvent(handle, IS_SET_EVENT_CAPTURE_STATUS);
            // CHECK_STATUS("DisableCaptureStatusEvent")

            initialized = false;
    
    return true;
}

void UeyeCamera::initParameters()
{
    // TODO: set is_EnableAutoExit() to on/off?!
    
    status = is_SetColorMode(handle, format);
    CHECK_STATUS("SetColorMode")

            is_SetExternalTrigger(handle, IS_SET_TRIGGER_OFF);
    CHECK_STATUS("SetExternalTrigger")
}

size_t UeyeCamera::getBPP()
{
    switch( format )
    {
    // TODO: list all formats
    case IS_CM_MONO8:
        return 1;
    default:
        // unknown
        return 0;
    }
}

bool UeyeCamera::start()
{
    status = is_CaptureVideo(handle, IS_DONT_WAIT);
    CHECK_STATUS("CaptureVideo")
            if( IS_SUCCESS != status )
    {
        return false;
    }
    
    status = is_EnableEvent(handle, IS_SET_EVENT_FRAME);
    CHECK_STATUS("EnableFrameEvent")
            if( IS_SUCCESS != status )
    {
        return false;
    }
    
    // status = is_EnableEvent(handle, IS_SET_EVENT_CAPTURE_STATUS);
    // CHECK_STATUS("EnableCaptureStatusEvent")
    // if( IS_SUCCESS != status )
    // {
    //     return false;
    // }
    
    // Start capture error handler thread
    // TODO
    
    // Start frame event-listener thread
    waitForFrame();
    
    capturing = true;
    
    return true;
}

bool UeyeCamera::stop()
{
    if( !capturing )
    {
        return false;
    }
    
    status = is_StopLiveVideo(handle, 0);
    CHECK_STATUS("StopLiveVideo")

            if( IS_SUCCESS != status )
    {
        return false;
    }
    
    capturing = false;
    
    // Wait for frame thread to exit
    frame_thread.join();
    
    return true;
}

bool UeyeCamera::setNumBuffers( size_t num )
{
    if(initialized)
    {
        logger.error("setNumBuffers") << "cannot set number of buffers after initilization";
        return false;
    }
    
    if( num == 0 )
    {
        logger.error("setNumBuffers") << "number of buffers must be greater than zero";
        return false;
    }
    
    numBuffers = num;
    
    return true;
}

bool UeyeCamera::captureImage( lms::imaging::Image& image )
{
    char* ptr;
    
    // We always want the latest fully captured image
    status = is_GetActSeqBuf(handle, NULL, NULL, &ptr);
#ifdef UEYE_DEBUG
    CHECK_STATUS("GetActSeqBuf")
        #endif

            if( IS_SUCCESS == status && NULL != ptr && buffers.find(ptr) != buffers.end() )
    {
        status = is_LockSeqBuf(handle, IS_IGNORE_PARAMETER, ptr);
#ifdef UEYE_DEBUG
        CHECK_STATUS("LockSeqBuf")
        #endif

                status = is_CopyImageMem(handle, ptr, buffers[ptr], (char*)image.data());
#ifdef UEYE_DEBUG
        CHECK_STATUS("CopyImageMem")
        #endif

                status = is_UnlockSeqBuf(handle, IS_IGNORE_PARAMETER, ptr);
#ifdef UEYE_DEBUG
        CHECK_STATUS("UnlockSeqBuf")
        #endif

                return true;
    }
    
    return false;
}

bool UeyeCamera::waitForFrame(float timeOut){

    lms::extra::PrecisionTime start = lms::extra::PrecisionTime::now();
    bool success = true;
    INT ret = 0;
    do {
        //std::cout<<"waiting forIMAGE"<<std::endl;
        ret = is_WaitEvent( this->handle, IS_SET_EVENT_FRAME, 100 );
        float res =(lms::extra::PrecisionTime::now() - start).toFloat<std::milli, double>();
        if( res > timeOut){
            success = false;
            break;
        }
    } while( IS_TIMED_OUT == ret && this->capturing );
    return success;
}

bool UeyeCamera::setAOI(size_t width, size_t height, size_t offsetX, size_t offsetY)
{
    if( width & 0x3 )
    {
        logger.warn("setAOI") << "ROI width should be a multiple of 4";
    }
    
    if( height & 0x3 )
    {
        logger.warn("setAOI") << "ROI height should be a multiple of 4";
    }
    
    IS_RECT rect;

    rect.s32X       = offsetX;
    rect.s32Y       = offsetY;
    rect.s32Width   = width;
    rect.s32Height  = height;

    status = is_AOI( handle, IS_AOI_IMAGE_SET_AOI, (void*)&rect, sizeof(rect));
    CHECK_STATUS("AOI")
            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setPixelClock( unsigned int clock )
{
    status = is_PixelClock(handle, IS_PIXELCLOCK_CMD_SET, (void*)&clock, sizeof(clock));
    CHECK_STATUS("PixelClock")
            return ( IS_SUCCESS == status );
}

double UeyeCamera::setExposure(double exposure)
{
    double exposureParam = exposure;
    status = is_Exposure(handle, IS_EXPOSURE_CMD_SET_EXPOSURE, (void*)&exposureParam, sizeof(exposureParam));
    CHECK_STATUS("Exposure")

            if( IS_SUCCESS == status )
    {
        return exposureParam;
    }
    return 0.0;
}

double UeyeCamera::setFrameRate(double fps)
{
    double actual;
    status = is_SetFrameRate(handle, fps, &actual);
    CHECK_STATUS("SetFrameRate")

            if( IS_SUCCESS == status )
    {
        return actual;
    }
    return 0.0;
}

bool UeyeCamera::setHardwareGamma(bool enable)
{
    status = is_SetHardwareGamma(handle, enable ? IS_SET_HW_GAMMA_ON : IS_SET_HW_GAMMA_OFF);
    CHECK_STATUS("SetHardwareGamma")

            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setGamma(double gamma)
{
    INT value = static_cast<INT>( gamma * 100.0 );
    status = is_Gamma(handle, IS_GAMMA_CMD_SET, (void*)&value, sizeof(value));
    CHECK_STATUS("Gamma")

            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setGainBoost(bool enable)
{
    status = is_SetGainBoost(handle, enable ? IS_SET_GAINBOOST_ON : IS_SET_GAINBOOST_OFF);
    CHECK_STATUS("SetGainBoost")

            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setAutoGain()
{
    return setGain( IS_SET_ENABLE_AUTO_GAIN );
}

bool UeyeCamera::setGain(int value)
{
    status = is_SetHardwareGain(handle, value, IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER);
    CHECK_STATUS("SetHardwareGain")

            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setGlobalShutter(bool enable)
{
    status = is_SetGlobalShutter(handle, enable ? IS_SET_GLOBAL_SHUTTER_ON : IS_SET_GLOBAL_SHUTTER_OFF);
    CHECK_STATUS("SetGlobalShutter")

            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setEdgeEnhancement(int level)
{
    status = is_EdgeEnhancement(handle, IS_EDGE_ENHANCEMENT_CMD_SET, (void*)&level, sizeof(level));
    CHECK_STATUS("EdgeEnhancement")

            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setBlacklevel(bool autolevel, int offset)
{
    // Set mode
    INT mode = autolevel ? IS_AUTO_BLACKLEVEL_ON : IS_AUTO_BLACKLEVEL_OFF;
    status = is_Blacklevel(handle, IS_BLACKLEVEL_CMD_SET_MODE, (void*)&mode, sizeof(mode));
    CHECK_STATUS("Blacklevel mode")

            if( IS_SUCCESS != status )
    {
        return false;
    }
    
    // set offset
    status = is_Blacklevel(handle, IS_BLACKLEVEL_CMD_SET_OFFSET, (void*)&offset, sizeof(offset));
    CHECK_STATUS("Blacklevel offset")

            return ( IS_SUCCESS == status );
}

bool UeyeCamera::setHDR(bool enable)
{
    INT mode = enable ? IS_ENABLE_HDR : IS_DISABLE_HDR;
    status = is_EnableHdr(handle, mode);
    
    CHECK_STATUS("HDR enable")
    return ( IS_SUCCESS == status );
}

bool UeyeCamera::setHDRKneepoints( const std::vector< std::pair<double, double> >& kneepoints )
{
    KNEEPOINTARRAY array;
    
    INT numPoints = kneepoints.size();
    if( numPoints > 10)
    {
        logger.warn("setHDRKneepoints") << "Maxmimum number of HDR points limited to 10 (requested: " << kneepoints.size() << ")";
        numPoints = 10;
    }
    
    auto it = kneepoints.begin();
    
    // Fill KNEEPOINTARRAY
    array.NumberOfUsedKneepoints = numPoints;
    for( INT i = 0; i < numPoints; i++ )
    {
        KNEEPOINT& k = array.Kneepoint[i];
        k.x = it->first;
        k.y = it->second;
        ++it;
    }
    
    status = is_SetHdrKneepoints(handle, &array, numPoints);
    CHECK_STATUS("HDR Kneepoints")
    return ( IS_SUCCESS == status );
}

void UeyeCamera::info()
{
    SENSORINFO data;
    status = is_GetSensorInfo(handle, &data);
    CHECK_STATUS("GetSensorInfo")

            if( IS_SUCCESS != status )
    {
        return;
    }
    
    logger.info() << "SensorName: "     << data.strSensorName;
    logger.info() << "ColorMode: "      << ((data.nColorMode == IS_COLORMODE_MONOCHROME) ? "mono" : "color");
    logger.info() << "MaxWidth:"        << data.nMaxWidth;
    logger.info() << "MaxHeight:"       << data.nMaxHeight;
    logger.info() << "MasterGain:"      << std::boolalpha << data.bMasterGain;
    logger.info() << "GlobalShutter:"   << std::boolalpha << data.bGlobShutter;
    logger.info() << "PixelSize:"       << ( float(data.wPixelSize) * 0.01f ) << " um";
}

void UeyeCamera::logCaptureStatus()
{
    UEYE_CAPTURE_STATUS_INFO captureStatus;
    status = is_CaptureStatus(handle, IS_CAPTURE_STATUS_INFO_CMD_GET, (void*)&captureStatus, sizeof(captureStatus));
    CHECK_STATUS("CaptureStatus")

            if( IS_SUCCESS != status )
    {
        return;
    }
    
    if( captureStatus.dwCapStatusCnt_Total == 0 )
    {
        // no errors, nothing to log
        logger.info("captureStatus") << "No errors occured during capture";
        return;
    }
    
    logger.warn("captureStatus") << "Total Errors: "        << captureStatus.dwCapStatusCnt_Total;
    logger.warn("captureStatus") << "API_NO_DEST_MEM"       << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_API_NO_DEST_MEM];
    logger.warn("captureStatus") << "API_CONVERSION_FAILED" << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_API_CONVERSION_FAILED];
    logger.warn("captureStatus") << "API_IMAGE_LOCKED"      << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_API_IMAGE_LOCKED];
    logger.warn("captureStatus") << "DRV_OUT_OF_BUFFERS"    << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_DRV_OUT_OF_BUFFERS];
    logger.warn("captureStatus") << "DRV_DEVICE_NOT_READY"  << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_DRV_DEVICE_NOT_READY];
    logger.warn("captureStatus") << "USB_TRANSFER_FAILED"   << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_USB_TRANSFER_FAILED];
    logger.warn("captureStatus") << "DEV_TIMEOUT"           << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_DEV_TIMEOUT];
    logger.warn("captureStatus") << "ETH_BUFFER_OVERRUN"    << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_ETH_BUFFER_OVERRUN];
    logger.warn("captureStatus") << "ETH_MISSED_IMAGES"     << captureStatus.adwCapStatusCnt_Detail[IS_CAP_STATUS_ETH_MISSED_IMAGES];
}

int UeyeCamera::getErrorCode()
{
    return status;
}

std::string UeyeCamera::getError()
{
    
    if( IS_NO_SUCCESS == status )
    {
        // Use geterror
        INT err;
        IS_CHAR* errstr;
        if( IS_SUCCESS == is_GetError(handle, &err, &errstr) )
        {
            return std::string(errstr);
        }
        else
        {
            return std::string("Error reading is_GetError");
        }
    } else if( errorCodes.find(status) != errorCodes.end() ) {
        return std::string( errorCodes[status] );
    }
    return std::string("Unknown error code");
}

void UeyeCamera::initErrorCodes()
{
    if( !errorCodes.empty() )
    {
        return;
    }
    
    errorCodes = {
        { -1, "NO_SUCCESS"},
        {  0, "SUCCESS"},
        {  1, "INVALID_CAMERA_HANDLE"},
        {  2, "IO_REQUEST_FAILED"},
        {  3, "CANT_OPEN_DEVICE"},
        { 11, "CANT_OPEN_REGISTRY"},
        { 12, "CANT_READ_REGISTRY"},
        { 15, "NO_IMAGE_MEM_ALLOCATED"},
        { 16, "CANT_CLEANUP_MEMORY"},
        { 17, "CANT_COMMUNICATE_WITH_DRIVER"},
        { 18, "FUNCTION_NOT_SUPPORTED_YET"},
        { 30, "INVALID_IMAGE_SIZE"},
        { 32, "INVALID_CAPTURE_MODE"},
        { 49, "INVALID_MEMORY_POINTER"},
        { 50, "FILE_WRITE_OPEN_ERROR"},
        { 51, "FILE_READ_OPEN_ERROR"},
        { 52, "FILE_READ_INVALID_BMP_ID"},
        { 53, "FILE_READ_INVALID_BMP_SIZE"},
        {108, "NO_ACTIVE_IMG_MEM"},
        {112, "SEQUENCE_LIST_EMPTY"},
        {113, "CANT_ADD_TO_SEQUENCE"},
        {117, "SEQUENCE_BUF_ALREADY_LOCKED"},
        {118, "INVALID_DEVICE_ID"},
        {119, "INVALID_BOARD_ID"},
        {120, "ALL_DEVICES_BUSY"},
        {122, "TIMED_OUT"},
        {123, "NULL_POINTER"},
        {125, "INVALID_PARAMETER"},
        {127, "OUT_OF_MEMORY"},
        {129, "ACCESS_VIOLATION"},
        {139, "NO_USB20"},
        {140, "CAPTURE_RUNNING"},
        {145, "IMAGE_NOT_PRESENT"},
        {148, "TRIGGER_ACTIVATED"},
        {151, "CRC_ERROR"},
        {152, "NOT_YET_RELEASED"},
        {153, "NOT_CALIBRATED"},
        {154, "WAITING_FOR_KERNEL"},
        {155, "NOT_SUPPORTED"},
        {156, "TRIGGER_NOT_ACTIVATED"},
        {157, "OPERATION_ABORTED"},
        {158, "BAD_STRUCTURE_SIZE"},
        {159, "INVALID_BUFFER_SIZE"},
        {160, "INVALID_PIXEL_CLOCK"},
        {161, "INVALID_EXPOSURE_TIME"},
        {162, "AUTO_EXPOSURE_RUNNING"},
        {163, "CANNOT_CREATE_BB_SURF"},
        {164, "CANNOT_CREATE_BB_MIX"},
        {165, "BB_OVLMEM_NULL"},
        {166, "CANNOT_CREATE_BB_OVL"},
        {167, "NOT_SUPP_IN_OVL_SURF_MODE"},
        {168, "INVALID_SURFACE"},
        {169, "SURFACE_LOST"},
        {170, "RELEASE_BB_OVL_DC"},
        {171, "BB_TIMER_NOT_CREATED"},
        {172, "BB_OVL_NOT_EN"},
        {173, "ONLY_IN_BB_MODE"},
        {174, "INVALID_COLOR_FORMAT"},
        {175, "INVALID_WB_BINNING_MODE"},
        {176, "INVALID_I2C_DEVICE_ADDRESS"},
        {177, "COULD_NOT_CONVERT"},
        {178, "TRANSFER_ERROR"},
        {179, "PARAMETER_SET_NOT_PRESENT"},
        {180, "INVALID_CAMERA_TYPE"},
        {181, "INVALID_HOST_IP_HIBYTE"},
        {182, "CM_NOT_SUPP_IN_CURR_DISPLAYMODE"},
        {183, "NO_IR_FILTER"},
        {184, "STARTER_FW_UPLOAD_NEEDED"},
        {185, "DR_LIBRARY_NOT_FOUND"},
        {186, "DR_DEVICE_OUT_OF_MEMORY"},
        {187, "DR_CANNOT_CREATE_SURFACE"},
        {188, "DR_CANNOT_CREATE_VERTEX_BUFFER"},
        {189, "DR_CANNOT_CREATE_TEXTURE"},
        {190, "DR_CANNOT_LOCK_OVERLAY_SURFACE"},
        {191, "DR_CANNOT_UNLOCK_OVERLAY_SURFACE"},
        {192, "DR_CANNOT_GET_OVERLAY_DC"},
        {193, "DR_CANNOT_RELEASE_OVERLAY_DC"},
        {194, "DR_DEVICE_CAPS_INSUFFICIENT"},
        {195, "INCOMPATIBLE_SETTING"},
        {196, "DR_NOT_ALLOWED_WHILE_DC_IS_ACTIVE"},
        {197, "DEVICE_ALREADY_PAIRED"},
        {198, "SUBNETMASK_MISMATCH"},
        {199, "SUBNET_MISMATCH"},
        {200, "INVALID_IP_CONFIGURATION"},
        {201, "DEVICE_NOT_COMPATIBLE"},
        {202, "NETWORK_FRAME_SIZE_INCOMPATIBLE"},
        {203, "NETWORK_CONFIGURATION_INVALID"},
        {204, "ERROR_CPU_IDLE_STATES_CONFIGURATION"},
        {205, "DEVICE_BUSY"},
        {206, "SENSOR_INITIALIZATION_FAILED"}
    };
}

}

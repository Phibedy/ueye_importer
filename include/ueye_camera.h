#pragma once

#include <unordered_map>
#include <string>
#include <thread>

#include <lms/type/module_config.h>
#include <lms/imaging/image.h>
#include <lms/logger.h>

#include <ueye.h>

namespace lms_ueye_importer
{
class UeyeCamera
{
public:
    UeyeCamera(lms::logging::Logger *rootLogger);
    ~UeyeCamera();
    
    // Open camera device
    bool open();
    bool close();
    
    bool init();
    bool deinit();
    bool isInitialized() { return initialized; }
    
    // Start capturing
    bool start();
    bool stop();

    // Capture image
    /**
     * @brief waitForFrame
     * @return true if the capturing was successfull
     */
    bool waitForFrame(float timeOut = INFINITY);
    bool captureImage(lms::imaging::Image& image);

    // Info
    size_t getWidth() { return width; }
    size_t getHeight() { return height; }

    // Configuration
    bool setNumBuffers(size_t num);
    bool setAOI(size_t width, size_t height, size_t offsetX = 0, size_t offsetY = 0);
    
    bool setPixelClock( unsigned int clock );
    double setFrameRate( double fps );
    double setExposure( double exposure );
    
    bool setHardwareGamma(bool enable);
    bool setGamma(double gamma);
    
    bool setGainBoost(bool enable);
    bool setAutoGain();
    bool setGain(int value);
    
    bool setGlobalShutter(bool enable);
    bool setBlacklevel(bool autolevel, int offset);
    bool setEdgeEnhancement(int level);
    
    bool setHDR( bool enable );
    bool setHDRKneepoints( const std::vector< std::pair<double, double> >& kneepoints );
    
    // Debug info
    void info();
    void logCaptureStatus();
    
    // Error handling
    std::string getError();
    int getErrorCode();
    
protected:
    
    lms::logging::ChildLogger logger;
    
    HIDS handle;
    INT status;
    
    size_t format;
    size_t width;
    size_t height;
    
    size_t numBuffers;
    
    bool initialized;
    bool capturing;
    
    std::unordered_map<char*, INT> buffers;
    
    std::thread frame_thread;
    // std::thread capture_status_thread;
    
    static std::unordered_map<INT, std::string> errorCodes;
    
    size_t getBPP();
    void initParameters();
    
    static void initErrorCodes();
};

}

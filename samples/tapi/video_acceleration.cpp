#include <iostream>
#include "opencv2/core.hpp"
#include "opencv2/core/ocl.hpp"
#include "opencv2/core/utility.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"

#include "fps_counter.h"

using namespace cv;
using namespace std;

int main(int argc, char** argv)
{
    const char* keys =
        "{ i input    |      | specify input image }"
        "{ c camera   |  0   | specify camera id   }"
        "{ gpu GPU    | true | use GPU acceleration (Video Acceleration and OpenCL) }"
        "{ o output   |      | specify output save path}"
        "{ h help     |      | print help message }";

    cv::CommandLineParser cmd(argc, argv, keys);
    if (cmd.has("help"))
    {
        cout << "Usage : clahe [options]" << endl;
        cout << "Available options:" << endl;
        cmd.printMessage();
        return EXIT_SUCCESS;
    }

    string infile = cmd.get<string>("i");
    string outfile = cmd.get<string>("o");
    int camid = cmd.get<int>("c");

    bool gpu_mode = cmd.get<bool>("gpu");
    ocl::setUseOpenCL(gpu_mode);

    VideoCapture capture(infile);
    capture.set(CAP_PROP_HW_ACCELERATION, gpu_mode ? VIDEO_ACCELERATION_ANY : VIDEO_ACCELERATION_NONE);
    if (capture.get(CAP_PROP_HW_ACCELERATION) != 0) {
        cout << "Video Acceleration device initilized" << endl;
    } else {
        cout << "Video Acceleration device not available" << endl;
    }

    VideoWriter writer;
    if (!outfile.empty()) {
        int fourcc = VideoWriter::fourcc('H','2','6','4');
        double fps = capture.get(CAP_PROP_FPS);
        Size frameSize = { (int)capture.get(CAP_PROP_FRAME_WIDTH), (int)capture.get(CAP_PROP_FRAME_HEIGHT) };
        writer = VideoWriter(outfile, cv::CAP_FFMPEG, fourcc, fps, frameSize);
    }

#if 0
    if (!capture.set(CAP_PROP_MODE, 3)) { // AV_HWDEVICE_TYPE_VAAPI=3
        cout << "Can't enable HW decoding in VideoCapture" << endl;
        return EXIT_FAILURE;
    }

    if (!writer.set(CAP_PROP_MODE, 3)) { // AV_HWDEVICE_TYPE_VAAPI=3
        cout << "Can't enable HW decoding in VideoWriter" << endl;
        return EXIT_FAILURE;
    }
#endif

    cout << "\nControls:\n"
         << "\tm - switch OpenCL enabled/disabled mode"
         << "\tESC - exit\n";

    FPSCounter fps_counter(0.5); // print FPS every 0.5 seconds

    for (;;)
    {
        UMat frame, outframe;

        capture.read(frame);
        if (frame.empty()) {
            cout << "End of stream" << endl;
            break;
        }

        //writer.write(frame);

#if 1
        //cv::bitwise_not(frame, outframe);
        cv::cvtColor(frame, outframe, COLOR_BGRA2RGBA);

        imshow("output", outframe);

        char key = (char)waitKey(3);
        if(key == 'o')
            imwrite(outfile, outframe);
        else if(key == 27)
            break;
        else if(key == 'm')
        {
            ocl::setUseOpenCL(!cv::ocl::useOpenCL());
            cout << "Switched to " << (ocl::useOpenCL() ? "OpenCL enabled" : "CPU") << " mode\n";
        }
#endif
        fps_counter.NewFrame();
    }

    return EXIT_SUCCESS;
}

#include <iostream>
#include <chrono>
#include "opencv2/core.hpp"
#include "opencv2/core/ocl.hpp"
#include "opencv2/core/utility.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"

using namespace cv;
using namespace std;

const char* keys =
"{ i input    |        | input video file }"
"{ o output   |        | output video file, or specify 'null' to measure decoding without rendering to screen}"
"{ backend    | ffmpeg | VideoCapture and VideoWriter backend, valid values: 'any', 'ffmpeg', 'mf', 'gstreamer' }"
"{ accel      | any    | GPU Video Acceleration, valid values: 'none', 'any', 'd3d9', 'd3d11', 'vaapi', 'qsv' }"
"{ device     | -1     | Video Acceleration device (GPU) index (-1 means default device) }"
"{ invert     | false  | apply simple image processing - invert pixels by calling cv::bitwise_not }"
"{ codec      | H264   | codec id (four characters string) of output file encoder }"
"{ opencl     | true   | use OpenCL (inside VideoCapture/VideoWriter and for image processing) }"
"{ h help     |        | print help message }";

struct {
    cv::VideoCaptureAPIs backend;
    const char* str;
} backend_strings[] = {
    { cv::CAP_ANY, "any" },
    { cv::CAP_FFMPEG, "ffmpeg" },
    { cv::CAP_MSMF, "mf" },
    { cv::CAP_GSTREAMER, "gstreamer" },
};

struct {
    int acceleration;
    const char* str;
} acceleration_strings[] = {
    { VIDEO_ACCELERATION_NONE, "none" },
    { VIDEO_ACCELERATION_ANY, "any" },
    { VIDEO_ACCELERATION_D3D9, "d3d9" },
    { VIDEO_ACCELERATION_D3D11, "d3d11" },
    { VIDEO_ACCELERATION_VAAPI, "vaapi" },
    { VIDEO_ACCELERATION_QSV, "qsv" },
};

class FPSCounter {
public:
    FPSCounter(double interval) : interval(interval) {
    }

    ~FPSCounter() {
        NewFrame(true);
    }

    void NewFrame(bool last_frame = false) {
        num_frames++;
        auto now = std::chrono::high_resolution_clock::now();
        if (!last_time.time_since_epoch().count()) {
            last_time = now;
        }

        double sec = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_time).count();
        if (sec >= interval || last_frame) {
            fprintf(output, "FpsCounter(%.2fsec): FPS=%.2f\n", sec, num_frames / sec);
            fflush(output);
            num_frames = 0;
            last_time = now;
        }
    }

private:
    FILE* output = stdout;
    double interval = 1;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_time;
    int num_frames = 0;
};

int main(int argc, char** argv)
{
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
    string codec = cmd.get<string>("codec");
    int device = cmd.get<int>("device");
    bool use_opencl = cmd.get<bool>("opencl");
    bool invert = cmd.get<bool>("invert");

    cv::VideoCaptureAPIs backend = cv::CAP_ANY;
    string backend_str = cmd.get<string>("backend");
    for (size_t i = 0; i < sizeof(backend_strings)/sizeof(backend_strings[0]); i++) {
        if (backend_str == backend_strings[i].str) {
            backend = backend_strings[i].backend;
            break;
        }
    }

    int accel = VIDEO_ACCELERATION_ANY;
    string accel_str = cmd.get<string>("accel");
    for (size_t i = 0; i < sizeof(acceleration_strings) / sizeof(acceleration_strings[0]); i++) {
        if (accel_str == acceleration_strings[i].str) {
            accel = acceleration_strings[i].acceleration;
            break;
        }
    }

    ocl::setUseOpenCL(use_opencl);

    VideoCapture capture(infile, backend);
    if (!capture.isOpened()) {
        cerr << "Failed to open VideoCapture" << endl;
        return 1;
    }
    if (accel != VIDEO_ACCELERATION_NONE) {
        capture.set(CAP_PROP_HW_DEVICE, device);
        capture.set(CAP_PROP_HW_ACCELERATION, accel);
        if (!capture.isOpened()) {
            cerr << "Failed to set CAP_PROP_HW_ACCELERATION" << endl;
            return 1;
        }
        cout << "VideoCapture backend = " << capture.getBackendName() << endl;
        accel = (int)capture.get(CAP_PROP_HW_ACCELERATION);
        for (size_t i = 0; i < sizeof(acceleration_strings) / sizeof(acceleration_strings[0]); i++) {
            if (accel == acceleration_strings[i].acceleration) {
                cout << "VideoCapture acceleration = " << acceleration_strings[i].str << endl;
                cout << "VideoCapture acceleration device = " << (int)capture.get(CAP_PROP_HW_DEVICE) << endl;
                break;
            }
        }
    }

    VideoWriter writer;
    if (!outfile.empty() && outfile != "null") {
        const char* codec_str = codec.c_str();
        int fourcc = VideoWriter::fourcc(codec_str[0], codec_str[1], codec_str[2], codec_str[3]);
        double fps = capture.get(CAP_PROP_FPS);
        Size frameSize = { (int)capture.get(CAP_PROP_FRAME_WIDTH), (int)capture.get(CAP_PROP_FRAME_HEIGHT) };
        writer = VideoWriter(outfile, backend, fourcc, fps, frameSize, {
                VIDEOWRITER_PROP_HW_ACCELERATION, accel,
                VIDEOWRITER_PROP_HW_DEVICE, device
        });
        if (!writer.isOpened()) {
            cerr << "Failed to open VideoWriter" << endl;
            return 1;
        }
        cout << "VideoWriter backend = " << writer.getBackendName() << endl;
        accel = (int)capture.get(CAP_PROP_HW_ACCELERATION);
        for (size_t i = 0; i < sizeof(acceleration_strings) / sizeof(acceleration_strings[0]); i++) {
            if (accel == acceleration_strings[i].acceleration) {
                cout << "VideoWriter acceleration = " << acceleration_strings[i].str << endl;
                cout << "VideoWriter acceleration device = " << (int)writer.get(CAP_PROP_HW_DEVICE) << endl;
                break;
            }
        }
    }

    cout << "\nStarting frame loop. Press ESC to exit\n";

    FPSCounter fps_counter(0.5); // print FPS every 0.5 seconds

    for (;;)
    {
        UMat frame, outframe;

        capture.read(frame);
        if (frame.empty()) {
            cout << "End of stream" << endl;
            break;
        }

        if (invert) {
            cv::bitwise_not(frame, outframe);
            //cv::cvtColor(frame, outframe, COLOR_BGRA2RGBA);
        }
        else {
            outframe = frame;
        }

        if (writer.isOpened()) {
            writer.write(outframe);
        }

        if (outfile.empty()) {
            imshow("output", outframe);
            char key = (char) waitKey(1);
            if (key == 27)
                break;
            else if (key == 'm') {
                ocl::setUseOpenCL(!cv::ocl::useOpenCL());
                cout << "Switched to " << (ocl::useOpenCL() ? "OpenCL enabled" : "CPU") << " mode\n";
            }
        }
        fps_counter.NewFrame();
    }

    return EXIT_SUCCESS;
}

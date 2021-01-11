#pragma once

#include <chrono>

class FPSCounter {
public:
    FPSCounter(double interval) : interval(interval) {
    }
    
    ~FPSCounter() {
        NewFrame(true);
    }

    void NewFrame(bool last_frame = false) {
        std::lock_guard<std::mutex> lock(mutex);
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
    FILE *output = stdout;
    double interval = 1;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_time;
    int num_frames = 0;
    std::mutex mutex;
};

# Fire CPU optimization build

This package keeps final fire color/core/candidate analysis at 960x540 and targets CPU reduction.

## Applied changes

1. HSV and BGR are split once per detection and reused.
2. The white-core mask is built once and reused.
3. 3x3, 5x5 and 9x9 morphology kernels are cached.
4. MOG2 runs on half-resolution grayscale; full-resolution absdiff remains enabled.
5. Previous-frame buffers use copyTo() to reuse allocation.
6. Contour-local point arrays are no longer copied; drawContours uses an offset.
7. Expensive contour inspection is capped at the 12 largest candidates.
8. Debug image generation is disabled by default.

## Expected trade-off

The main quality risk is a weaker MOG2 response for extremely tiny motion. Full-resolution frame difference, fire color, white core and halo analysis are retained to compensate. Limiting contour checks to 12 may miss a small fire in a scene containing more than 12 larger false candidates. Set FIRE_MAX_CONTOURS_TO_CHECK to 20 for such scenes.

## Build

Linux/Raspberry Pi:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j4
    ./build/fire_benchmark /path/to/test.mp4 500

Windows PowerShell:

    cmake -S . -B build -DOpenCV_DIR="C:/path/to/opencv/build"
    cmake --build build --config Release
    .\build\Release\fire_benchmark.exe "C:\path\to\test.mp4" 500

Compare the same video and same frame count against the existing detector.

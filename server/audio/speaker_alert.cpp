#include "speaker_alert.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

namespace {
    // 절대경로/장치번호를 소스에 박아두면 계정·기기가 다른 파이에서 안 돌아간다 —
    // CMakeLists.txt가 빌드 시점에 채워주는 매크로를 그대로 쓴다 (DB_PATH와 동일한 패턴).
    constexpr const char* kAudioDevice = AUDIO_DEVICE;
    constexpr const char* kAudioFile = AUDIO_FILE;

    std::atomic<bool> g_alarmActive{false};
    std::thread g_thread;
    std::mutex g_childMutex;   // g_childPid를 재생 스레드/Stop 호출 스레드가 같이 건드려서 보호 필요
    pid_t g_childPid = -1;

    void playLoop() {
        while (g_alarmActive.load()) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("[스피커] fork 실패");
                break;
            }
            if (pid == 0) {
                execlp("aplay", "aplay", "-D", kAudioDevice, kAudioFile, (char*)nullptr);
                _exit(127);   // execlp 실패 시에만 도달
            }
            {
                std::lock_guard<std::mutex> lock(g_childMutex);
                g_childPid = pid;
            }
            int status;
            waitpid(pid, &status, 0);
            {
                std::lock_guard<std::mutex> lock(g_childMutex);
                g_childPid = -1;
            }

            // 오디오 장치가 없는 등 aplay가 즉시 실패하면 간격 없이 반복 재시도돼서 로그가
            // 도배됨(정상 재생은 음성 길이만큼 이미 간격이 있어 영향 없음) — 리뷰 지적 반영.
            // 통짜로 2초 자면 Stop()의 join()도 그만큼 묶이므로, 100ms씩 쪼개서 중단 신호를 본다.
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                for (int i = 0; i < 20 && g_alarmActive.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

void SpeakerAlert_Start() {
    if (g_alarmActive.exchange(true)) return;   // 이미 재생 중이면 무시
    if (g_thread.joinable()) g_thread.join();   // 이전 스레드 잔재 정리
    g_thread = std::thread(playLoop);
    std::cout << "[스피커] 경고음 반복 재생 시작\n";
}

void SpeakerAlert_Stop() {
    if (!g_alarmActive.exchange(false)) return;   // 원래 꺼져 있었으면 무시
    {
        std::lock_guard<std::mutex> lock(g_childMutex);
        if (g_childPid > 0) kill(g_childPid, SIGTERM);   // 재생 중인 aplay 즉시 중단
    }
    if (g_thread.joinable()) g_thread.join();
    std::cout << "[스피커] 경고음 재생 중단\n";
}
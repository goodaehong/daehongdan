#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#define AUDIO_DEVICE "hw:2,0"
#define AUDIO_FILE "/home/yuna/daehongdan/audio/강남대로.wav"

// aplay 프로세스를 fork/exec로 실행하고 종료까지 대기
static int play_audio(const char *device, const char *filepath) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return -1;
    }

    if (pid == 0) {
        // 자식 프로세스: aplay 실행
        execlp("aplay", "aplay", "-D", device, filepath, (char *)NULL);

        // execlp가 실패하면 여기로 옴
        perror("execlp failed");
        _exit(127);
    }

    // 부모 프로세스: 자식 종료 대기
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid failed");
        return -1;
    }

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            fprintf(stderr, "aplay exited with code %d\n", exit_code);
            return -1;
        }
        printf("재생 완료: %s\n", filepath);
        return 0;
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "aplay가 시그널 %d로 종료됨\n", WTERMSIG(status));
        return -1;
    }

    return -1;
}

int main(int argc, char *argv[]) {
    const char *device = AUDIO_DEVICE;
    const char *file = AUDIO_FILE;

    // 커맨드라인 인자 -> override
    if (argc >= 2) file = argv[1];
    if (argc >= 3) device = argv[2];

    printf("오디오 재생 테스트 시작\n");
    printf("  device: %s\n", device);
    printf("  file:   %s\n", file);

    if (play_audio(device, file) != 0) {
        fprintf(stderr, "오디오 재생 실패\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
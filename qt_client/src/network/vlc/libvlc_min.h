#ifndef LIBVLC_MIN_H
#define LIBVLC_MIN_H

// libvlc 공식 SDK(헤더) 없이, 우리가 실제로 쓰는 함수만 최소로 선언한 바인딩.
// libvlc는 순수 C ABI라서 MinGW에서도 문제없이 링크 가능 (MSVC로 빌드된 dll이어도 상관없음).
// 함수 시그니처는 libvlc 3.x 안정 API 기준.

extern "C" {

typedef struct libvlc_instance_t libvlc_instance_t;
typedef struct libvlc_media_t libvlc_media_t;
typedef struct libvlc_media_player_t libvlc_media_player_t;

libvlc_instance_t *libvlc_new(int argc, const char *const *argv);
void libvlc_release(libvlc_instance_t *p_instance);

libvlc_media_t *libvlc_media_new_location(libvlc_instance_t *p_instance, const char *psz_mrl);
void libvlc_media_release(libvlc_media_t *p_md);
void libvlc_media_add_option(libvlc_media_t *p_md, const char *psz_options);

libvlc_media_player_t *libvlc_media_player_new_from_media(libvlc_media_t *p_md);
void libvlc_media_player_release(libvlc_media_player_t *p_mi);
void libvlc_media_player_set_hwnd(libvlc_media_player_t *p_mi, void *drawable);
int libvlc_media_player_play(libvlc_media_player_t *p_mi);
void libvlc_media_player_stop(libvlc_media_player_t *p_mi);

}

#endif // LIBVLC_MIN_H

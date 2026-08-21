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
// 재생바(ClipPlayerWidget) 용 — 현재 재생 위치/전체 길이(ms) 조회 및 이동.
long long libvlc_media_player_get_time(libvlc_media_player_t *p_mi);
void libvlc_media_player_set_time(libvlc_media_player_t *p_mi, long long i_time);
long long libvlc_media_player_get_length(libvlc_media_player_t *p_mi);
// 소스 프레임을 지정한 W:H 비율로 잘라낸 뒤 렌더 타겟에 맞춰 스케일한다. 렌더 타겟과 같은
// 비율을 넘기면 원본 비율 유지(레터박스) 없이 타겟을 꽉 채운다 — 초과분은 크롭됨(CSS cover와 동일).
void libvlc_video_set_crop_geometry(libvlc_media_player_t *p_mi, const char *psz_geometry);

}

#endif // LIBVLC_MIN_H

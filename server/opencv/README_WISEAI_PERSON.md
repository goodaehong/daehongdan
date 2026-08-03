# PNM-C16083RVQ WiseAI person boxes

`RP_Fire` receives the camera's person detections from the ONVIF XML metadata
track in each RTSP profile. It does not run a second person-detection model.

## Camera setup

In the camera web page, enable **Object detection** and select **Person** for
each of the four sensors. Object data from an excluded area is not transmitted
unless the camera option for transmitting excluded-area object data is enabled.

The application connects to these four matching video/metadata profiles:

- `/0/profile10/media.smp`
- `/1/profile10/media.smp`
- `/2/profile10/media.smp`
- `/3/profile10/media.smp`

## Windows prerequisites and check

Both `ffmpeg.exe` and `ffprobe.exe` must be in `PATH`. The application uses
FFmpeg only to copy the small XML data stream; FFmpeg does not decode the video.

Run this from PowerShell. The password is prompted securely and is not stored
by the script:

```powershell
Set-Location "C:\Users\3-19\Desktop\PJ\fire&smoke\RP_Fire\RP_Fire"
.\CheckWiseAiMetadata.ps1 -CameraIp 172.20.35.186
```

To save ten seconds of raw metadata for parser troubleshooting:

```powershell
.\CheckWiseAiMetadata.ps1 -CameraIp 172.20.35.186 -CaptureSeconds 10
```

## Runtime output

The 4-channel grid draws each person in green. Console coordinates use the
original channel resolution and have this form:

```text
CH1 | personMeta=1 | persons=1 | boxes=[{x:120,y:42,w:96,h:244,score:0.91,id:17}]
```

`x` and `y` are the upper-left pixel. `w` and `h` are width and height. For a
640x360 stream the valid image area is `x=0..639`, `y=0..359`.

`WiseAI OFF` or `personMeta=0` means that no fresh XML metadata is arriving. It
does not mean that the video, fire detector, or smoke detector has stopped.

## Raspberry Pi

Install FFmpeg once:

```bash
sudo apt update
sudo apt install -y ffmpeg
```

There is one metadata-copy process per active channel. These processes copy XML
packets only and do not perform video decoding, so their CPU cost is much lower
than another AI model or another video decoder.

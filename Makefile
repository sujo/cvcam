CXXFLAGS   = `pkg-config --cflags opencv4` -g3
LDLIBS     = `pkg-config --libs opencv4` -lv4l2

TARGETS=cvcam

all: $(TARGETS)

.PHONY: setup-loopback
setup-loopback:
	-sudo rmmod v4l2loopback
	sudo modprobe v4l2loopback devices=1 exclusive_caps=1 card_label="CVCam" video_nr=8

chromakey-ffmpeg: setup-loopback
	ffmpeg  -i ~/Pictures/Home-Office-Waterfront.jpg \
	  -f v4l2 -framerate 30 -video_size 1280x720 -i /dev/video5 \
          -filter_complex \
	    "[0:v]scale=1280x720[bg]; [1:v]chromakey=0x007018:0.094:0.03[ckout]; [bg][ckout]overlay,format=yuv420p" \
          -f v4l2 -r 30 /dev/video8

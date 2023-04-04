CXXFLAGS   = `pkg-config --cflags opencv4` -g3
LDLIBS     = `pkg-config --libs opencv4`

TARGETS=cvcam

all: $(TARGETS)

.PHONY: setup-loopback
setup-loopback:
	-sudo rmmod v4l2loopback
	sudo modprobe v4l2loopback devices=1 exclusive_caps=1 video_nr=5 card_label="CVCam"


# Prerequisites

Required packages (OpenSUSE):

Build prerequisites:

- opencv-devel
- v4l2loopback-dkms
- v4l2loopback-utils
- gcc-g++

Optional viewers:

- cheese
- vlc
- mpv


# Building the tool

$ make


# Check existing video devices

$ v4l2-ctl --list-devices


# Create virtual camera loopback device

$ make setup-loopback


# Resources

- https://docs.opencv.org/4.x/d1/dc5/tutorial_background_subtraction.html
- https://arcoresearchgroup.wordpress.com/2020/06/02/virtual-camera-for-opencv-using-v4l2loopback/

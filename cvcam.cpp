#include <opencv2/opencv.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define VID_WIDTH  640
#define VID_HEIGHT 480
#define VIDEO_IN   "/dev/video0"
#define VIDEO_OUT  "/dev/video6"

int
main(void) {
   // open and configure input camera (/dev/video0)
   cv::VideoCapture cam(VIDEO_IN);
   if (not cam.isOpened()) {
      std::cerr << "ERROR: could not open camera!\n";
      return -1;
   }
   cam.set(cv::CAP_PROP_FRAME_WIDTH, VID_WIDTH);
   cam.set(cv::CAP_PROP_FRAME_HEIGHT, VID_HEIGHT);

   // open output device
   int output = open(VIDEO_OUT, O_RDWR);
   if(output < 0) {
      std::cerr << "ERROR: could not open output device!\n" <<
	 strerror(errno); return -2;
   }

   // acquire video format from device
   struct v4l2_format vid_format;
   memset(&vid_format, 0, sizeof(vid_format));
   vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

   if (ioctl(output, VIDIOC_G_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: unable to get video format!\n" <<
	 strerror(errno); return -1;
   }

   // configure desired video format on device
   size_t framesize = VID_WIDTH * VID_HEIGHT * 3;
   vid_format.fmt.pix.width = cam.get(cv::CAP_PROP_FRAME_WIDTH);
   vid_format.fmt.pix.height = cam.get(cv::CAP_PROP_FRAME_HEIGHT);
   vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
   vid_format.fmt.pix.sizeimage = framesize;
   vid_format.fmt.pix.field = V4L2_FIELD_NONE;

   if (ioctl(output, VIDIOC_S_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: unable to set video format!\n" <<
	 strerror(errno); return -1;
   }

   // loop over these actions:
   while (true) {
      cv::Mat frame;
      cam >> frame;

      cv::Mat result;
      cv::cvtColor(frame, result, cv::COLOR_RGB2GRAY);
      cv::cvtColor(result, result, cv::COLOR_GRAY2RGB);

      size_t written = write(output, result.data, framesize);
      if (written < 0) {
         std::cerr << "ERROR: could not write to output device!\n";
         close(output);
         break;
      }

   }

}

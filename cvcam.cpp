#include <iostream>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <linux/videodev2.h>
#include <libv4l2.h>
#include <opencv2/opencv.hpp>

#define VID_WIDTH  1280
#define VID_HEIGHT 720

float g_a1, g_a2;

//#define CALIBRATE

#ifdef CALIBRATE
void mouseCallback(int ev, int x, int y, int flags, void* userdata)
{
   g_a1 = x/50.0;
   g_a2 = y/50.0;
   std::cout << "a1=" << g_a1 << ", a2=" << g_a2 << "\n";
}
#endif


int configure_device(int fd, v4l2_buf_type t)
{
   struct v4l2_format vid_format;
   memset(&vid_format, 0, sizeof(vid_format));
   vid_format.type = t;

   if (v4l2_ioctl(fd, VIDIOC_G_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: Unable to get video format: " << strerror(errno) << "\n";
      return 5;
   }

   vid_format.fmt.pix.width = VID_WIDTH; //cam.get(CAP_PROP_FRAME_WIDTH);
   vid_format.fmt.pix.height = VID_HEIGHT; //cam.get(CAP_PROP_FRAME_HEIGHT);
   vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
   vid_format.fmt.pix.sizeimage = VID_WIDTH * VID_HEIGHT * 3;
   vid_format.fmt.pix.field = V4L2_FIELD_NONE;
   //cam.set(CAP_PROP_AUTO_WB, false);

   if (v4l2_ioctl(fd, VIDIOC_S_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: Unable to set video format: " << strerror(errno) << "\n";
      return 5;
   }
   return 0;
}


int
main(int argc, char *argv[]) {

   const char* param_spec =
      "{ help h         |             | Print usage }"
      "{ input          | /dev/video1 | Video device for primary video stream input }"
      "{ output         | /dev/video8 | Video device for the output stream. Can be created with the v4l2loopback kernel module. }"
      "{ image          | /home/spike/myicarus.jpg   | Image that replaces the background removed from the input stream }"
      "{ rb             | 4.0         | weight for blue-green difference on alpha }"
      "{ g              | 6.2         | blue-green difference scale }"
      ;

   const std::string imageFile = "/home/spike/myicarus.jpg";

   const std::string inputFile = "/dev/video1";
   if (inputFile.size() == 0) {
      std::cerr << "Missing parameter: input device\n";
      return 1;
   }

   const std::string outputFile = "/dev/video8";
   if (outputFile.size() == 0) {
      std::cerr << "Missing parameter: output device\n";
      return 2;
   }

   std::cout << "Opening input device: " << inputFile << "\n";
   int input = v4l2_open(inputFile.c_str(), O_RDWR);
   if(input < 0) {
      std::cerr << "ERROR: Could not open input stream " << inputFile << ": " << strerror(errno) << "\n";
      return 3;
   }

   int err = configure_device(input, V4L2_BUF_TYPE_VIDEO_CAPTURE);
   if (err) {
      return err;
   }

   // open output device
   std::cout << "Opening output stream: " << outputFile << "\n";
   int output = v4l2_open(outputFile.c_str(), O_WRONLY);
   if(output < 0) {
      std::cerr << "ERROR: Could not open output stream " << outputFile << ": " << strerror(errno) << "\n";
      return 4;
   }

   err = configure_device(output, V4L2_BUF_TYPE_VIDEO_OUTPUT);
   if (err) {
      return err;
   }

   // prepare virtual background
   cv::Mat bgImage{VID_HEIGHT, VID_WIDTH, CV_8UC3, {0,255,0}}; // green background
   if (imageFile.size()) {
      std::cerr << "Loading image: " << imageFile << "\n";
      cv::Size size = bgImage.size();
      bgImage = cv::imread(imageFile);
      if (bgImage.empty()) {
         std::cerr << "Unable to read image from " << imageFile << "\n";
         return 10;
      }
      cv::resize(bgImage, bgImage, size, 0, 0, cv::INTER_LINEAR);
   }

   g_a1 = 4.0;
   g_a2 = 6.2;

   const size_t linesize = VID_WIDTH * 3;
   const size_t framesize = VID_WIDTH * VID_HEIGHT * 3;


   struct timeval tv{0, 0};
   gettimeofday(&tv, NULL);
   long sec = tv.tv_sec;

   unsigned long frames = 0;
   char frame[framesize];

   bool quit = false;
   while(!quit) {
      int r = v4l2_read(input, frame, framesize);
      if (r < 0) {
         std::cerr << "Empty frame.\n";
         quit = true;
         break;
      }
      ++frames;

      for (int i = 0; i < framesize-2; i += 3) {
         float r = static_cast<unsigned char>(frame[i]) / 255.0,
               g = static_cast<unsigned char>(frame[i+1]) / 255.0,
               b = static_cast<unsigned char>(frame[i+2]) / 255.0;

         //alpha = Scalar::all(1.0) - g_a1 * (frameChannels[1] - g_a2 * frameChannels[0]);
         float alpha = 1.0 + g_a1 * (r + b) - g_a2 * g;

         if (alpha > 1.0) alpha = 1.0;
         else if (alpha < 0.0) alpha = 0.0;

         alpha *= alpha;
         const float Ialpha = 1.0 - alpha;
         alpha *= 255.0;

         // remove green borders
         g = std::min(g, std::max(r, b));

         frame[i] = r * alpha + Ialpha * bgImage.data[i+2]; // cv::Mat uses BGR format
         frame[i+1] = g * alpha + Ialpha * bgImage.data[i+1];
         frame[i+2] = b * alpha + Ialpha * bgImage.data[i];
      }

      //output << frame;
      size_t written = v4l2_write(output, frame, framesize);
      if (written < 0) {
         close(output);
         std::cerr << "ERROR: Could not write to output device: " << strerror(errno) << "\n";
         return 7;
      }

      gettimeofday(&tv, NULL);
      long sec2 = tv.tv_sec;
      if (sec2 > sec) {
         std::cout << "\rFPS: " << frames << "              " << std::flush;
         frames = 0;
         sec = sec2;
      }
   } // main loop

   return 0;
}

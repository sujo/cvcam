#include <iostream>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <linux/videodev2.h>
#include <libv4l2.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utility.hpp> // CommandLineParser

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

void printSize(const std::string &name, const cv::Mat &m) {
   std::cout << name << " size: " << m.cols << "*" << m.rows << "*" << m.elemSize() << " type " << m.type() << "\n";
}

int
main(int argc, char *argv[]) {

   using namespace cv;

   const char* param_spec =
      "{ help h         |             | Print usage }"
      "{ input          | /dev/video0 | Video device for primary video stream input }"
      "{ output         | /dev/video6 | Video device for the output stream. Can be created with the v4l2loopback kernel module. }"
      "{ image          | your-file   | Image that replaces the background removed from the input stream }"
      "{ rb             | 4.0         | weight for blue-green difference on alpha }"
      "{ g              | 6.2         | blue-green difference scale }"
      ;

   CommandLineParser params{argc, argv, param_spec};
   params.about("This tool replaces the static background from a video stream with an image.\n");

   if (params.has("help")) {
      params.printMessage();
      return 0;
   }

   const std::string inputFile = params.get<String>("input");
   if (inputFile.size() == 0) {
      std::cerr << "Missing parameter: input device\n";
      return 1;
   }

   const std::string outputFile = params.get<String>("output");
   if (outputFile.size() == 0) {
      std::cerr << "Missing parameter: output device\n";
      return 2;
   }

   g_a1 = params.get<float>("rb");
   g_a2 = params.get<float>("g");

   std::cout << "Opening input device: " << inputFile << "\n";
   VideoCapture cam(inputFile);
   if (!cam.isOpened()) {
      std::cerr << "ERROR: Could not open input stream " << inputFile << ".\n";
      return 3;
   }
   cam.set(CAP_PROP_FRAME_WIDTH, VID_WIDTH);
   cam.set(CAP_PROP_FRAME_HEIGHT, VID_HEIGHT);
   cam.set(CAP_PROP_AUTO_WB, false);

   Size sz = Size(VID_WIDTH, VID_HEIGHT);

   // open output device
   std::cout << "Opening output stream: " << outputFile << "\n";
   int output = v4l2_open(outputFile.c_str(), O_WRONLY);
   if(output < 0) {
      std::cerr << "ERROR: Could not open output stream " << outputFile << ": " << strerror(errno) << "\n";
      return 4;
   }

   size_t framesize = VID_WIDTH * VID_HEIGHT * 3;
   struct v4l2_format vid_format;
   memset(&vid_format, 0, sizeof(vid_format));
   vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

   if (ioctl(output, VIDIOC_G_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: Unable to get video format: " << strerror(errno) << "\n";
      return 5;
   }

   // configure desired video format on device
   vid_format.fmt.pix.width = VID_WIDTH; //cam.get(CAP_PROP_FRAME_WIDTH);
   vid_format.fmt.pix.height = VID_HEIGHT; //cam.get(CAP_PROP_FRAME_HEIGHT);
   vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
   vid_format.fmt.pix.sizeimage = framesize;
   vid_format.fmt.pix.field = V4L2_FIELD_NONE;

   if (v4l2_ioctl(output, VIDIOC_S_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: Unable to set video format: " << strerror(errno) << "\n";
      return 6;
   }


   // prepare virtual background
   Mat bgImage{VID_HEIGHT, VID_WIDTH, CV_8UC3, {0,255,0}}; // green background
   const std::string imageFile = params.get<String>("image");
   if (imageFile.size()) {
      std::cerr << "Loading image: " << imageFile << "\n";
      Size size = bgImage.size();
      bgImage = imread(imageFile);
      if (bgImage.empty()) {
         std::cerr << "Unable to read image from " << imageFile << "\n";
         return 10;
      }
      resize(bgImage, bgImage, size, 0, 0, INTER_LINEAR);
   }
   std::vector<Mat> frameChannels, bgImageChannels0, bgImageChannels;

   bgImage.convertTo(bgImage, CV_32FC3, 1.0/255);
   split(bgImage, bgImageChannels0);
   split(bgImage, bgImageChannels);

   Mat src, front, back;
   Mat alpha = Mat::zeros(sz, CV_32F);

   struct timeval tv{0, 0};
   gettimeofday(&tv, NULL);
   long sec = tv.tv_sec;
   unsigned long frames = 0;

#ifdef CALIBRATE
   namedWindow("cvcam");
   setMouseCallback("cvcam", mouseCallback);
#endif

   for (int key = 0; key != 27 /* ESC */; key = waitKey(10)) {
      Mat frame;
      cam >> frame;
      if (frame.empty()) {
         std::cerr << "Empty frame.\n";
         break;
      }
      ++frames;

      frame.convertTo(frame, CV_32F, 1.0/255);
      split(frame, frameChannels);

      //alpha = Scalar::all(1.0) - g_a1 * (frameChannels[1] - g_a2 * frameChannels[0]);
      alpha = 1.0 + g_a1 * (frameChannels[0] + frameChannels[2]) - g_a2 * frameChannels[1];
      threshold(alpha, alpha, 1, 1, THRESH_TRUNC);
      threshold(alpha, alpha, 0, 0, THRESH_TOZERO);
      multiply(alpha, alpha, alpha);

      for (int i=0; i < 3; ++i) {
         multiply(alpha, frameChannels[i], frameChannels[i]);
         multiply(1.0 - alpha, bgImageChannels0[i], bgImageChannels[i]);  
      }
      merge(frameChannels, front);
      merge(bgImageChannels, back);
      frame = front + back;
      frame.convertTo(frame, CV_8UC3, 255);

      imshow("cvcam", frame);

      //output << frame;
      size_t written = v4l2_write(output, frame.data, framesize);
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

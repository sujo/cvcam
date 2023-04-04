#include <iostream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utility.hpp> // CommandLineParser

#define VID_WIDTH  640
#define VID_HEIGHT 480
#define VIDEO_OUT  "/dev/video6"

int
main(int argc, char *argv[]) {

   using namespace cv;

   const char* param_spec =
      "{ help h         |             | Print usage }"
      "{ input          | /dev/video0 | Video device for primary video stream input }"
      "{ output         | /dev/video5 | Video device for the output stream. Can be created with the v4l2loopback kernel module. }"
      "{ image          |             | Image that replaces the background removed from the input stream }"
      "{ learningSecs   | 5           | Number of seconds learning the background }"
      ;

   CommandLineParser params{argc, argv, param_spec};
   params.about("This tool replaces the static background from a video stream with an image.\n");

   if (params.has("help")) {
      params.printMessage();
      return 0;
   }

   std::string inputFile = params.get<String>("input");
   if (inputFile.size() == 0) {
      std::cerr << "Missing parameter: input device\n";
      return 1;
   }

   std::string outputFile = params.get<String>("output");
   if (outputFile.size() == 0) {
      std::cerr << "Missing parameter: output device\n";
      return 2;
   }

   std::cout << "Opening input stream: " << inputFile << "\n";
   VideoCapture cam(inputFile.c_str());
   if (!cam.isOpened()) {
      std::cerr << "ERROR: Could not open input stream " << inputFile << ".\n";
      return 3;
   }

   cam.set(CAP_PROP_FRAME_WIDTH, VID_WIDTH);
   cam.set(CAP_PROP_FRAME_HEIGHT, VID_HEIGHT);

   // open output device
   std::cout << "Opening output stream: " << outputFile << "\n";
   int output = open(outputFile.c_str(), O_RDWR);
   if(output < 0) {
      std::cerr << "ERROR: Could not open output stream " << outputFile << ": " << strerror(errno) << "\n";
      return 4;
   }

   struct v4l2_format vid_format;
   memset(&vid_format, 0, sizeof(vid_format));
   vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

   if (ioctl(output, VIDIOC_G_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: Unable to get video format: " << strerror(errno) << "\n";
      return 5;
   }

   // configure desired video format on device
   size_t framesize = VID_WIDTH * VID_HEIGHT * 3;
   vid_format.fmt.pix.width = cam.get(CAP_PROP_FRAME_WIDTH);
   vid_format.fmt.pix.height = cam.get(CAP_PROP_FRAME_HEIGHT);
   vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
   vid_format.fmt.pix.sizeimage = framesize;
   vid_format.fmt.pix.field = V4L2_FIELD_NONE;

   if (ioctl(output, VIDIOC_S_FMT, &vid_format) < 0) {
      std::cerr << "ERROR: Unable to set video format: " << strerror(errno) << "\n";
      return 6;
   }

   Ptr<BackgroundSubtractor> pBackSub = createBackgroundSubtractorMOG2();

   Mat mask;
   struct timeval tv{0, 0};
   gettimeofday(&tv, NULL);
   long sec = tv.tv_sec;
   unsigned long frames = 0;

   double learningRate = 0.2;
   unsigned long learningSecs = params.get<unsigned long>("learningSecs");
   while (true) {
      Mat frame;
      cam >> frame;
      if (frame.empty()) {
         std::cerr << "Empty frame.\n";
         break;
      }
      ++frames;

      pBackSub->apply(frame, mask, learningRate);

      Mat outFrame;
      outFrame = mask;

      size_t written = write(output, outFrame.data, framesize);
      if (written < 0) {
         close(output);
         std::cerr << "ERROR: Could not write to output device: " << strerror(errno) << "\n";
         return 7;
      }

      gettimeofday(&tv, NULL);
      long sec2 = tv.tv_sec;
      if (sec2 > sec) {
         std::cout << "FPS: " << frames << "\n";
         frames = 0;
         sec = sec2;
         if (learningSecs > 0) {
            --learningSecs;
         } else {
            learningRate = 0.0;
         }
      }
   } // main loop

   return 0;
}

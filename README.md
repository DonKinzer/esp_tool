esp_tool is an image generator and downloader utility for the ESP8266, communicating with the device over a serial link.

The utility can generate image files for the ESP8266 by extracting information from an ELF file.  It can combine multiple images into a single file either in padded mode (as similar utilities do) or in sparse mode.  The advantage of having a single image is that it simplifies downloading.  A padded combined image, however, can take much longer to download to the device than the sparse image.  Essentially, the sparse image avoids having to transmit all of the pad bytes.

The esp_tool utility can also automatically extract the load address from the image name if the name contains the hexadecimal load address as it often does.  For example, if one of the load image files has the name myApp_0x4c000.bin, esp_tool can extract the 0x4c000 load address from the image name.  This capability simplifies the process of downloading multiple image files as it does for combining multiple images.

The C++ code for esp_tool has been compiled for Windows using both VC6 and VS12, an nmake makefile is provided.  It has also been compiled and briefly tested on Debian.  The code contains conditionals for Mac OS X but it has not been built and tested on that platform.

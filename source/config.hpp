#ifndef ICONFIG_HPP
#define ICONFIG_HPP

#include <iostream>
#include <string>

using std::cout;
using std::endl;
using std::string;

const int DefaultWidth = 1280;
const int DefaultHeight = 720;

class Screen {
  int width;
  int height;
  Screen() : width(0), height(0) {}
  Screen(int w, int h) : width(w), height(h) {}
  friend class IConfig;
  friend class Pconfig;
  friend class Sconfig;

public:
  void set(int w, int h) {
    width = w;
    height = h;
  }
  int getW() { return width; }
  int getH() { return height; }
};

class IConfig {
public:
  virtual ~IConfig() = default;
  Screen screen;
};

class Pconfig : public IConfig {
public:
  Pconfig() { screen = Screen(1920, 1080); }
};

class Sconfig : public IConfig {
public:
  Sconfig() { screen = Screen(DefaultWidth, DefaultHeight); }
};

#endif
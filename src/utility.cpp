#include "utility.h"
#include "params.h"
#include <math.h>

double deg2rad(double x) { return x * M_PI / 180; }
double rad2deg(double x) { return x * 180 / M_PI; }
double mph_to_ms(double mph) { return mph / 2.24; } // m.s-1
double ms_to_mph(double ms) { return ms * 2.24; } // mph

// d coord for left lane
double get_dleft(int lane)
{
  double dleft = lane * param_lane_width;
  return dleft;
}

// d coord for right lane
double get_dright(int lane)
{
  double dright = (lane + 1) * param_lane_width;
  return dright;
}

// d coord for center lane
double get_dcenter(int lane)
{
  double dcenter = (lane + 0.5) * param_lane_width;
  return dcenter;
}

int get_lane(double d)
{
  return (int)(d / param_lane_width);
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
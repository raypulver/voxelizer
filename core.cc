#include <limits>
#include <vector>
#include <cstddef>
#include <cstdarg>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <unistd.h>
#include <getopt.h>
#include <cmath>
#include <iostream>
#include <fstream>
#include <map>
#include <png.h>
#include <libgen.h>
#include <json-c/json.h>
#include "pdb.h"
#include "voxelizer.h"
#include "hmm.h"

using namespace std;

template <typename T> T log(T arg, string str) {
  cout << str << ": " << arg << endl;
  return arg;
}

template <typename T> T find_max(T *vals, size_t sz) {
  size_t i;
  T max = numeric_limits<T>::min();
  for (i = 0; i < sz; ++i) {
    if (vals[i] > max) max = vals[i];
  }
  return max;
}

template <typename T> void triple_min_max(T *coords, size_t n, T *xmin, T *xmax, T *ymin, T *ymax, T *zmin, T *zmax) {
  size_t i;
  *xmin = numeric_limits<T>::max();
  *ymin = numeric_limits<T>::max();
  *zmin = numeric_limits<T>::max();
  *xmax = numeric_limits<T>::min();
  *ymax = numeric_limits<T>::min();
  *zmax = numeric_limits<T>::min(); 
  for (i = 0; i < n; ++i) {
    if (coords[i*3] < *xmin) *xmin = coords[i*3];
    if (coords[i*3] > *xmax) *xmax = coords[i*3];
    if (coords[i*3 + 1] < *ymin) *ymin = coords[i*3 + 1];
    if (coords[i*3 + 1] > *ymax) *ymax = coords[i*3 + 1];
    if (coords[i*3 + 2] < *zmin) *zmin = coords[i*3 + 2];
    if (coords[i*3 + 2] > *zmax) *zmax = coords[i*3 + 2];
  }
}

template <typename T> T sum(T *values, size_t sz) {
  size_t i;
  T sum = 0;
  for (i = 0; i < sz; ++i) {
    sum += values[i];
  }
  return sum;
}

template <int format> uint8_t PNG<format>::Pixel::GetValue() {
  return g;
}

template <int format> void *PNG<format>::GetBuffer() {
  return buffer;
}

bool InCircle(double x, double h, double y, double k, double r) {
  if (pow(x - h, 2) + pow(y - k, 2) < pow(r, 2)) return true;
  return false;
}

void WriteCircle(int x, int y, const char *filename) {
  PNG<PNG_FORMAT_GA>::Pixel *data = new PNG<PNG_FORMAT_GA>::Pixel[x*y];
  for (int i = 0; i < x; i++) {
    for (int j = 0; j < y; j++) {
      if (InCircle(i, (double) x/2, j, (double) y/2, (double) min(x, y)/4)) {
        data[i*x + j].g = 175;
        data[i*x + j].a = 255;
      } else if (InCircle(i, (double) x/2, j, (double) y/2, (double) min(x, y)/2)) {
        data[i*x + j].g = 75;
        data[i*x + j].a = 255;
      } else {
	data[i*x + j].a = 0;
	data[i*x + j].g = 0;
      }
    }
  }
  PNG<PNG_FORMAT_GA> png (x, y, data);
  png.Write(filename);
}

bool InTriangle(int i, int x, int j, int y) {
  if (i < (double) x/2 && i > (double) x/3 && j < (double) y/3 && j > (double) y/8) return true;
  else return false;
}
void WriteTriangle(int x, int y, const char *filename) {
  PNG<PNG_FORMAT_GA>::Pixel *data = new PNG<PNG_FORMAT_GA>::Pixel[x*y];
  for (int i = 0; i < x; i++) {
    for (int j = 0; j < y; j++) {
      if (InTriangle(i, x, j, y)) {
        data[i*x + j].g = 175;
        data[i*x + j].a = 255;
      } else if (InTriangle(i, x, j, y) || InTriangle(i + 1, x, j + 1, y)) {
        data[i*x + j].g = 120;
        data[i*x + j].a = 255;
      } else {
	data[i*x + j].a = 0;
	data[i*x + j].g = 0;
      }
    }
  }
  PNG<PNG_FORMAT_GA> png (x, y, data);
  png.Write(filename);
}

template <int format> PNG<format>::PNG(int x, int y, void *buf) : buffer(buf) {
  img.width = x;
  img.height = y;
  img.version = PNG_IMAGE_VERSION;
  img.opaque = nullptr;
  img.format = format;
  img.flags = 0;
  img.colormap_entries = 0;
  width = x;
  height = y;
  colormap = nullptr;
}

template <int format> PNG<format>* PNG<format>::FromFile(string filename) {
  png_image img;
  memset(&img, 0, (sizeof(png_image)));
  img.version = PNG_IMAGE_VERSION;
  if (png_image_begin_read_from_file(&img, filename.c_str())) {
    png_bytep buffer;
    img.format = format;
    buffer = (png_bytep) malloc(PNG_IMAGE_SIZE(img));
      if (buffer != nullptr && png_image_finish_read(&img, NULL, buffer, 0, NULL)) {
        return new PNG<format>(img.width, img.height, buffer);
      }
  }
  return nullptr;
}

template <int format> PNG<format>::~PNG() {
  if (buffer) free(buffer);
  if (colormap) free(colormap);
}

template <int format> int PNG<format>::Write(string filename) {
  return Write(filename.c_str());
}

template <int format> int PNG<format>::Write(const char *filename) {
  return png_image_write_to_file(&img, filename, 0, buffer, 0, colormap);
}
  
template <typename T> T *ZSlice(T *values, size_t x, size_t y, size_t z) {
  size_t i, j;
  T *retval = new T[x*y];
  for (i = 0; i < x; ++i) {
    for (j = 0; j < y; ++j) {
      retval[i*x + j] = values[i*x*y + j*y + z];
    }
  }
  return retval;
}
  
char *base;

template <typename T, typename... Args> void Die(T fmt, Args ...args) {
  char buf[MAX_ERROR_FORMAT_STRING_SIZE];
  memset(buf, 0, sizeof(buf));
  sprintf(buf, fmt, args...);
  string err = base;
  err += ": ";
  err += buf;
  cerr << err << endl;
  exit(1);
}

template <typename T> void Die(T arg) {
  Die("%s", arg);
}

PDB::Ptr PDB::New(char *filename, uint8_t density) { return PDB::Ptr(new PDB(filename, density)); }

PDB::PDB(char *filename, uint8_t dens) : density(dens) {
  handle = plugin.open_file_read(filename, "pdb", &natoms);
  if (!handle) Die("<VMDPLUGIN> open_file_read(\"%s\") failed.", filename);
  atoms = new molfile_atom_t[natoms];
  ts.coords = new float[natoms*3];
  plugin.read_structure(handle, &optflags, atoms);
  plugin.read_next_timestep(handle, natoms, &ts);
  triple_min_max(ts.coords, natoms, &xmin, &xmax, &ymin, &ymax, &zmin, &zmax);
}

PDB::~PDB() {
  plugin.close_file_read(handle);
  delete[] atoms;
  delete[] ts.coords; 
}

void MultiPDBVoxelizer::SetRadius(double r) {
  radius = r;
  vradius = step*r;
  step *= (double) (maxpxl + r*6)/maxpxl;
  vradius = r*step;
  xoffset += r*3;
  yoffset += r*3;
  zoffset += r*3;
}

void MultiPDBVoxelizer::SetDimensions(int i, int j, int k) { x = i, y = j, z = k, v = x*y*z, a = x*y; }
void MultiPDBVoxelizer::push_back(PDB::Ptr p) { pdbs.push_back(p); }
void MultiPDBVoxelizer::CalculateSpan() {
  xmin = numeric_limits<float>::max();
  ymin = numeric_limits<float>::max();
  zmin = numeric_limits<float>::max();
  xmax = numeric_limits<float>::min();
  ymax = numeric_limits<float>::min();
  zmax = numeric_limits<float>::min();
  for (auto it = pdbs.begin(); it != pdbs.end(); it++) {
    if (((*it)->xmin) < xmin) xmin = (*it)->xmin;
    if (((*it)->ymin) < ymin) ymin = (*it)->ymin;
    if (((*it)->zmin) < zmin) zmin = (*it)->zmin;
    if (((*it)->xmax) > xmax) xmax = (*it)->xmax;
    if (((*it)->ymax) > ymax) ymax = (*it)->ymax;
    if (((*it)->zmax) > zmax) zmax = (*it)->zmax;
  }
  xdiff = xmax - xmin;
  ydiff = ymax - ymin;
  zdiff = zmax - zmin;
  maxdim = varmax(xdiff, ydiff, zdiff);
  xratio = xdiff/maxdim;
  yratio = ydiff/maxdim;
  zratio = zdiff/maxdim;
  if (zratio == 1.0) maxpxl = z;
  if (yratio == 1.0) maxpxl = y;
  if (xratio == 1.0) maxpxl = x;
  xadj = xmin/xratio;
  yadj = ymin/yratio;
  zadj = zmin/zratio;
  step = maxdim/maxpxl;
  xoffset = xdiff*(1 - xratio)*step/2;
  yoffset = ydiff*(1 - yratio)*step/2;
  zoffset = xdiff*(1 - zratio)*step/2;
}

PNG<PNG_FORMAT_GA>::Pixel *MultiPDBVoxelizer::Voxelize() {
  int density = 0, maxdens = 0, i, j, k, l;
  double mincoords[3];
  double maxcoords[3];
  double center[3];
  double centercoords[3];
  PDB *winner = nullptr;
  PNG<PNG_FORMAT_GA>::Pixel *retval = new PNG<PNG_FORMAT_GA>::Pixel[v];
  for (i = 0; i < x; ++i) {
    for (j = 0; j < y; ++j) {
      for (k = 0; k < z; ++k) {
        winner = nullptr;
        maxdens = 0;
        for (auto it = pdbs.begin(); it != pdbs.end(); it++) {
          density = 0;
          for (l = 0; l < (*it)->natoms; l++) {
            center[0] = (*it)->ts.coords[l*3];
            center[1] = (*it)->ts.coords[l*3 + 1];
            center[2] = (*it)->ts.coords[l*3 + 2];
            mincoords[0] = xadj + (double) (i - xoffset)*step;
            centercoords[0] = mincoords[0] + step/2;
            maxcoords[0] = mincoords[0] + step;
            mincoords[1] = yadj + (double) (j - yoffset)*step;
            centercoords[1] = mincoords[1] + step/2;
            maxcoords[1] = mincoords[1] + step;
            mincoords[2] = zadj + (double) (k - zoffset)*step;
            centercoords[2] = mincoords[2] + step/2;
            maxcoords[2] = mincoords[2] + step;
            if ((center[0] >= mincoords[0] &&
              center[0] < maxcoords[0] &&
              center[1] >= mincoords[1] &&
              center[1] < maxcoords[1] &&
              center[2] >= mincoords[2] &&
              center[2] < maxcoords[2]) || 
              InSphere(centercoords[0], center[0], centercoords[1], center[1], centercoords[2], center[2], vradius*get_pte_vdw_radius((*it)->atoms[l].atomicnumber))) {
              ++density;
            }
          }
          if (density > maxdens) {
            maxdens = density;
            winner = it->get();
          }
        }
        if (!winner) { retval[i*a + j*y + k] = {0, 0}; }
        else { retval[i*a + j*y + k] = { winner->density, 0xff }; }
      }
    }
  }
  return retval;
}

template <typename T> void ParseFilename(char *fn, vector<char *> &filenames, vector<T> &values) {
  char *ptr = fn + strlen(fn);
  filenames.push_back(fn);
  for (; ptr >= fn; ptr--) {
    if (*ptr == ':') {
      *ptr = '\0';
      values.push_back(atoi(ptr + 1));
      return;
    }
  }
  values.push_back(100);
}

json_object *NewDoubleOrInt(double d) {
  if (!d) return json_object_new_int(0);
  return json_object_new_double(d);
}

HMM::Ptr HMM::New() { return HMM::Ptr(new HMM); }
json_object *HMM::as_json_object() {
  json_object *retval = json_object_new_object();
  json_object *state_index = json_object_new_array();
  for (auto it = states.begin(); it != states.end(); it++) {
    json_object *state = json_object_new_object();
    json_object_object_add(state, "density", json_object_new_int(it->first));
    json_object_object_add(state, "duration", json_object_new_int(it->second));
    json_object_array_add(state_index, state);
  }
  json_object_object_add(retval, "states", state_index);
  json_object *initial_states = json_object_new_array();
  json_object_object_add(retval, "initial", initial_states);
  for (auto it = initial.begin(); it != initial.end(); it++) {
    json_object_array_add(initial_states, NewDoubleOrInt(*it));
  }
  json_object *matrix_array = json_object_new_array();
  json_object_object_add(retval, "matrix", matrix_array); 
  for (size_t i = 0; i < states.size(); ++i) {
    json_object *row = json_object_new_array();
    json_object_array_add(matrix_array, row);
    for (size_t j = 0; j < states.size(); ++j) {
      json_object_array_add(row, NewDoubleOrInt(matrix[i*states.size() + j]));
    }
  }
  return retval;
}

HMMGroup::Ptr HMMGroup::New() { return HMMGroup::Ptr(new HMMGroup); }
json_object *HMMGroup::as_json_object() {
  json_object *retval = json_object_new_object();
  json_object_object_add(retval, "xpos", xpos->as_json_object());
  json_object_object_add(retval, "xneg", xneg->as_json_object());
  json_object_object_add(retval, "ypos", ypos->as_json_object());
  json_object_object_add(retval, "yneg", yneg->as_json_object());
  json_object_object_add(retval, "zpos", zpos->as_json_object());
  json_object_object_add(retval, "zneg", zneg->as_json_object());
  return retval;
}

void IncreaseOrDecrease(size_t &n, uint8_t sign) {
  if (sign) n--;
  else n++;
}

template <typename T> HMM::Ptr CalculateHMM(T *items, size_t *coords, uint8_t fix, uint8_t sign) {
  size_t permutecoords[3];
  size_t multiplier[3];
  size_t idx = 0;
  for (size_t m = 0; m < 3; ++m) {
    if (fix == m) continue;
    permutecoords[idx] = coords[m];
    if (m == 0) multiplier[idx] = coords[0]*coords[1];
    if (m == 1) multiplier[idx] = coords[1];
    if (m == 2) multiplier[idx] = 1;
    idx++; 
  }
  switch (fix) {
    case 0:
      multiplier[idx] = coords[0]*coords[1];
      break;
    case 1:
      multiplier[idx] = coords[1];
      break;
    case 2:
      multiplier[idx] = 1;
  }
  permutecoords[idx] = coords[fix];
  HMM::Ptr retval = HMM::New();
  uint8_t last_depth = 0;
  uint8_t depth = 0;
  size_t duration = 0;
  bool starting = true;
  bool initial = true;
  HMM::State last_state;
  vector<HMM::State> initial_states;
  vector<HMM::Transition> transitions;
  for (size_t i = 0; i < permutecoords[0]; ++i) {
    for (size_t j = 0; j < permutecoords[1]; ++j) {
      for (size_t k = (sign == 1 ? permutecoords[2] - 1: 0); (sign == 1 ? k != ((size_t) 0 - (size_t) 1) : k < permutecoords[2]); IncreaseOrDecrease(k, sign)) {
        depth = items[i*multiplier[0] + j*multiplier[1] + multiplier[2]*k].GetValue();
        if (!starting && (last_depth != depth)) {
          if (initial)
            initial_states.push_back(HMM::State(last_depth, duration));
          else transitions.push_back(HMM::Transition(last_state, HMM::State(last_depth, duration)));
          retval->states.push_back(HMM::State(last_depth, duration));
          last_state = HMM::State(last_depth, duration);
          duration = 0;
          initial = false;
        }
        duration++;
        last_depth = depth;
        starting = false;
      }
      if (initial) {
        initial_states.push_back(HMM::State(depth, duration));
      }
      else transitions.push_back(HMM::Transition(last_state, HMM::State(depth, duration)));
      retval->states.push_back(HMM::State(depth, duration));
      memset(&last_state, 0, sizeof(HMM::State)); 
      duration = 0;
      last_depth = 0;
      depth = 0;
      initial = true;
      starting = true;
    }
  }
  sort(retval->states.begin(), retval->states.end());
  auto it = unique(retval->states.begin(), retval->states.end(), [&] (HMM::State a, HMM::State b) -> bool {
    return ((a.first == b.first) && (b.second == a.second));
  });
  retval->states.resize(distance(retval->states.begin(), it));
  retval->matrix.resize(retval->states.size() * retval->states.size());
  map<HMM::State, size_t> state_map;
  for (auto it = retval->states.begin(); it != retval->states.end(); it++) {
    state_map[*it] = distance(retval->states.begin(), it);
  }
  for (auto it = transitions.begin(); it != transitions.end(); it++) {
    retval->matrix[state_map[it->first]*retval->states.size() + state_map[it->second]]++;
  }
  for (size_t i = 0; i < retval->states.size(); ++i) {
    double sum = 0;
    for (size_t j = 0; j < retval->states.size(); ++j) {
      sum += retval->matrix[i*retval->states.size() + j];
    }
    if (sum) for (size_t j = 0; j < retval->states.size(); ++j) {
      retval->matrix[i*retval->states.size() + j] /= sum;
    }
  }
  retval->initial.resize(retval->states.size());
  for (auto it = initial_states.begin(); it != initial_states.end(); it++) {
    retval->initial[state_map[*it]]++;
  }
  double sum = 0;
  for (size_t i = 0; i < retval->states.size(); ++i) {
    sum += retval->initial[i];
  }
  if (sum) for (size_t i = 0; i < retval->states.size(); ++i) {
    retval->initial[i] /= sum;
  }
  return retval;
}

json_object *HMM2DToJsonObject(HMM2D::Ptr a) {
  json_object *retval = json_object_new_object();
  json_object *states = json_object_new_array();
  for (auto it = a->states.begin(); it != a->states.end(); it++) {
    json_object_array_add(states, json_object_new_int(*it));
  }
  json_object_object_add(retval, "states", states);
  json_object *transitions = json_object_new_array();
  json_object *initial = json_object_new_array();
  json_object *obs = json_object_new_array();
  json_object *obj = json_object_new_object();
  for (size_t i = 0; i < a->states.size(); ++i) {
    json_object *row = json_object_new_array();
    json_object_array_add(transitions, row);
    for (size_t j = 0; j < a->states.size(); ++j) {
      json_object_array_add(row, json_object_new_double(a->xtransition[i*a->states.size() + j]));
    }
  }
  for (size_t i = 0; i < a->xinitial.size(); ++i) {
    json_object_array_add(initial, json_object_new_double(a->xinitial[i]));
  }
  for (size_t i = 0; i < a->xobs.size(); ++i) {
    json_object_array_add(obs, json_object_new_int(a->xobs[i]));
  }
  json_object_object_add(obj, "observed", obs);
  json_object_object_add(obj, "transition", transitions);
  json_object_object_add(obj, "initial", initial);
  json_object_object_add(retval, "x", obj);
  obj = json_object_new_object();
  transitions = json_object_new_array();
  initial = json_object_new_array();
  for (size_t i = 0; i < a->states.size(); ++i) {
    json_object *row = json_object_new_array();
    json_object_array_add(transitions, row);
    for (size_t j = 0; j < a->states.size(); ++j) {
      json_object_array_add(row, json_object_new_double(a->ytransition[i*a->states.size() + j]));
    }
  }
  for (size_t i = 0; i < a->yinitial.size(); ++i) {
    json_object_array_add(initial, json_object_new_double(a->yinitial[i]));
  }
  obs = json_object_new_array();
  for (size_t i = 0; i < a->yobs.size(); ++i) {
    json_object_array_add(obs, json_object_new_int(a->yobs[i]));
  }
  json_object_object_add(obj, "observed", obs);
  json_object_object_add(obj, "transition", transitions);
  json_object_object_add(obj, "initial", initial);
  json_object_object_add(retval, "y", obj);
  return retval;
}

template <typename T> HMM2D::Ptr Calculate2DHMM(T *items, size_t *coords) {
  HMM2D::Ptr retval = HMM2D::New();
  HMM2D::PartialState last_depth = 0;
  HMM2D::PartialState depth = 0;
  bool starting = true;
  bool initial = true;
  vector<HMM2D::PartialState> xinitial_states;
  vector<HMM2D::PartialTransition> xtransitions;
  vector<HMM2D::PartialState> yinitial_states;
  vector<HMM2D::PartialTransition> ytransitions;
  for (size_t i = 0; i < coords[0]; ++i) {
    for (size_t j = 0; j < coords[1]; ++j) {
      depth = items[i*coords[0] + j].GetValue();
      if (initial) xinitial_states.push_back(depth);
      else xtransitions.push_back(HMM2D::PartialTransition(last_depth, depth));
      retval->states.push_back(depth);
      last_depth = depth;
      initial = false;
    }
    initial = true;
  }
  for (size_t i = 0; i < coords[1]; ++i) {
    for (size_t j = 0; j < coords[0]; ++j) {
      depth = items[j*coords[0] + i].GetValue();
      if (initial) yinitial_states.push_back(depth);
      else ytransitions.push_back(HMM2D::PartialTransition(last_depth, depth));
      last_depth = depth;
      initial = false;
    }
    initial = true;
  }
  sort(retval->states.begin(), retval->states.end());
  auto it = unique(retval->states.begin(), retval->states.end());
  retval->states.resize(distance(retval->states.begin(), it));
  retval->xtransition.resize(retval->states.size() * retval->states.size());
  retval->ytransition.resize(retval->states.size() * retval->states.size());
  retval->xinitial.resize(retval->states.size());
  retval->yinitial.resize(retval->states.size());
  fill(retval->xtransition.begin(), retval->xtransition.end(), 0);
  fill(retval->ytransition.begin(), retval->ytransition.end(), 0);
  fill(retval->xinitial.begin(), retval->xinitial.end(), 0);
  fill(retval->yinitial.begin(), retval->yinitial.end(), 0);
  for (auto it = retval->states.begin(); it != retval->states.end(); it++) {
    retval->state_map[*it] = distance(retval->states.begin(), it);
  }
  for (auto it = xtransitions.begin(); it != xtransitions.end(); it++) {
    retval->xtransition[retval->state_map[it->first]*retval->states.size() + retval->state_map[it->second]]++;
  }
  for (auto it = retval->xtransition.begin(); it != retval->xtransition.end(); it++) {
    retval->xtransitionwhole.push_back(*it);
  }
  for (size_t j = 0; j < retval->states.size(); ++j) {
      double sum = 0;
      for (size_t i = 0; i < retval->states.size(); i++) {
        sum += retval->xtransitionwhole[i*retval->states.size() + j];
      }
      retval->xcolsums.push_back(sum);
  }
  for (size_t i = 0; i < retval->states.size(); ++i) {
    double sum = 0;
    for (size_t j = 0; j < retval->states.size(); ++j) {
      sum += retval->xtransition[i*retval->states.size() + j];
    }
    retval->xrowsums.push_back(sum);
    if (sum) for (size_t j = 0; j < retval->states.size(); ++j) {
      retval->xtransition[i*retval->states.size() + j] /= sum;
    }
  }
  for (auto it = xinitial_states.begin(); it != xinitial_states.end(); it++) {
    retval->xinitial[retval->state_map[*it]]++;
  }
  double sum = 0;
  for (size_t i = 0; i < retval->states.size(); ++i) {
    sum += retval->xinitial[i];
  }
  if (sum) for (size_t i = 0; i < retval->states.size(); ++i) {
    retval->xinitial[i] /= sum;
  }
  for (auto it = ytransitions.begin(); it != ytransitions.end(); it++) {
    retval->ytransition[retval->state_map[it->first]*retval->states.size() + retval->state_map[it->second]]++;
  }
  for (auto it = retval->ytransition.begin(); it != retval->ytransition.end(); it++) {
    retval->ytransitionwhole.push_back(*it);
  }
  for (size_t j = 0; j < retval->states.size(); ++j) {
    double sum = 0;
    for (size_t i = 0; i < retval->states.size(); ++i) {
      sum += retval->ytransitionwhole[i*retval->states.size() + j];
    }
    retval->ycolsums.push_back(sum);
  }
  for (size_t i = 0; i < retval->states.size(); ++i) {
    double sum = 0;
    for (size_t j = 0; j < retval->states.size(); ++j) {
      sum += retval->ytransition[i*retval->states.size() + j];
    }
    retval->yrowsums.push_back(sum);
    if (sum) for (size_t j = 0; j < retval->states.size(); ++j) {
      retval->ytransition[i*retval->states.size() + j] /= sum;
    }
  }
  for (auto it = yinitial_states.begin(); it != yinitial_states.end(); it++) {
    retval->yinitial[retval->state_map[*it]]++;
  }
  sum = 0;
  for (size_t i = 0; i < retval->states.size(); ++i) {
    sum += retval->yinitial[i];
  }
  if (sum) for (size_t i = 0; i < retval->states.size(); ++i) {
    retval->yinitial[i] /= sum;
  }
  return retval;
}

template <typename T> HMMGroup::Ptr CalculateHMMGroup(T *items, size_t *dimensions) {
  HMMGroup::Ptr retval = HMMGroup::New();
  retval->xpos = CalculateHMM(items, dimensions, 0, 0);
  retval->xneg = CalculateHMM(items, dimensions, 0, 1);
  retval->ypos = CalculateHMM(items, dimensions, 1, 0);
  retval->yneg = CalculateHMM(items, dimensions, 1, 1);
  retval->zpos = CalculateHMM(items, dimensions, 2, 0);
  retval->zneg = CalculateHMM(items, dimensions, 2, 1);
  return retval;
}

Permutation *EMStartingWith(HMM2D::Ptr a, HMM2D::Direction d, size_t len, HMM2D::PartialState s, double threshold) {
  vector<double> backup = a->GetInitial(d);
  fill(a->GetInitial(d).begin(), a->GetInitial(d).end(), 0);
  a->GetInitial(d)[s] = 1;
  Permutation* retval = EMMax(a, d, len + 1, threshold);
  Permutation* starting = retval->last[0];
  a->GetInitial(d) = backup;
  return starting;
}

Permutation *EMMaxFront(HMM2D::Ptr a, HMM2D::Direction d, size_t len, double threshold) {
  Permutation *retval = new Permutation();
  Permutation *tmp = retval;
  retval->probability = 100;
  retval->state = 255;
  for (size_t i = 0; i < a->states.size(); ++i) {
    retval->last.push_back(EMFront(a, d, a->states[i], len, threshold));
  }
  return retval;
}

void CopyRow(vector<double> &dest, vector<double> &src, size_t len, size_t row) {
  dest.empty();
  dest.resize(len);
  for (size_t i = 0; i < len; ++i) {
    dest[i] = src[row*len + i];
  }
}

void ForeachProbableCombinationOfLength(HMM2D::Ptr a, HMM2D::Direction d, size_t len, function<void(HMM2D::State &, double &)> fn, HMM2D::State &state, double &probability, double threshold) {
  if (len == 0) { fn(state, probability); }
  else {
    for (size_t i = 0; i < a->states.size(); ++i) {
      double thisprob;
      if (!state.size()) {
        thisprob = a->GetInitial(d)[i];
      } else thisprob = a->GetTransition(d)[a->states.size()*a->state_map[state.back()] + i];
      probability *= thisprob;
      if (probability < threshold) {}
      else {
        state.push_back(a->states[i]);
        ForeachProbableCombinationOfLength(a, d, len - 1, fn, state, probability, threshold);
        state.pop_back();
      }
      probability /= thisprob;
    }
  }
}
          

void ForeachProbableCombinationOfLength(HMM2D::Ptr a, HMM2D::Direction d, size_t len, function<void(HMM2D::State &, double &)> fn, double threshold) {
  double prob = 1;
  HMM2D::State state;
  ForeachProbableCombinationOfLength(a, d, len, fn, state, prob, threshold);
}

Permutation *EMFrontImpl(HMM2D::Ptr a, HMM2D::Direction d, HMM2D::PartialState last, HMM2D::PartialState s, size_t len, double threshold, double current) {
  Permutation *retval = new Permutation();
  retval->state = 255;
  retval->probability = 100;
  if (!len) return retval;
  double prob;
  if (last == 255) prob = a->GetInitial(d)[a->state_map[s]];
  else prob = a->GetTransition(d)[a->state_map[last]*a->states.size() + a->state_map[s]]*current;
  if (prob < threshold) return retval;
  retval->state = s;
  retval->probability = prob;
  if (len == 1) return retval;
  for (size_t i = 0; a->states.size(); ++i) {
    retval->last.push_back(EMFrontImpl(a, d, s, a->states[i], len - 1, threshold, retval->probability));
  }
  return retval;
}
  
Permutation *EMFront(HMM2D::Ptr a, HMM2D::Direction d, HMM2D::PartialState s, size_t len, double threshold) {
  return EMFrontImpl(a, d, 255, s, len, threshold, 1);
}

json_object *VectorToJsonObject(HMM2D::State &a) {
  json_object *arr = json_object_new_array();
  for (auto it = a.cbegin(); it != a.cend(); it++) {
    json_object_array_add(arr, json_object_new_int(*it));
  }
  return arr;
}

void ForeachPermutation(Permutation *p, function<bool(HMM2D::State &, size_t, double)> fn, HMM2D::State &state, size_t &idx) {
  Permutation *tmp;
  if (p->last.size() == 0) {
    state.push_back(p->state);
    fn(state, idx, p->probability);
    idx++;
    state.resize(state.size() - 1);
  } else {
    if (p->state != 255) state.push_back(p->state);
    for (auto it = p->last.begin(); it != p->last.end(); it++) {
      ForeachPermutation(*it, fn, state, idx);
    }
    if (p->state != 255) state.resize(state.size() - 1);
  }
}

void ForeachPermutation(Permutation *p, function<bool(HMM2D::State &, size_t, double)> fn) {
  HMM2D::State permute;
  size_t i = 0;
  ForeachPermutation(p, fn, permute, i);
}

double SumThe2DState(HMM2D::State &state) {
  size_t sum = 0;
  for (auto s : state) {
    sum += s;
  }
  return sum;
}

double SumThe2DStateExceptFirst(HMM2D::State &state) {
  size_t sum = 0;
  HMM2D::State::const_iterator it = state.cbegin();
  it++;
  for (; it != state.cend(); it++) {
    sum += *it;
  }
  return sum;
}
    
double Prob(HMM2D::Ptr a, HMM2D::Direction d, HMM2D::PartialState &state, size_t t) {
  if (d == HMM2D::Direction::X) {
    if (a->xobs[t/a->xobs.size()] == 0 && state > 0) return 0;
    else return 1;
  } else {
    if (a->yobs[t % a->xobs.size()] == 0 && state > 0) return 0;
    else return 1;
  }
}
  
Viterbi2DResult::~Viterbi2DResult() {
  if (lastx) delete lastx;
  if (lasty) delete lasty;
}
Viterbi2DResult *Viterbi2DMax(HMM2D::Ptr a, size_t t) {
  double max = 0;
  Viterbi2DResult *retval;
  for (size_t x = 0; x < a->states.size(); ++x) {
    Viterbi2DResult *result = Viterbi2D(a, t, a->states[x]);
    if (result->probability > max) {
      max = result->probability;
      retval = result;
    } else delete result;
  }
  return retval;
}


Viterbi2DResult *Viterbi2D(HMM2D::Ptr a, size_t t, HMM2D::PartialState k) {
  Viterbi2DResult *result = new Viterbi2DResult();
  result->probability = Prob(a, HMM2D::Direction::X, k, t)*Prob(a, HMM2D::Direction::Y, k, t);
  double prob = result->probability;
  if (result->probability == 0) return result;
  double max = 0;
  if (t == 0) {
    result->lastx = nullptr;
    result->lasty = nullptr;
    result->probability *= a->xinitial[a->state_map[k]]*a->yinitial[a->state_map[k]];
    result->x = k;
    result->y = k;
    result->direction = HMM2D::Direction::X;
    return result;
  }
  if (!(t % a->xobs.size())) {
    result->lasty = nullptr;
    result->direction = HMM2D::Direction::X;
    for (size_t y = 0; y < a->states.size(); ++y) {
      Viterbi2DResult *yviterbi = Viterbi2D(a, t - a->xobs.size(), a->states[y]);
      double overall = prob*a->xinitial[a->state_map[k]]*a->ytransition[y*a->states.size() + a->state_map[k]]*yviterbi->probability;
      if (overall > max) {
        if (result->lasty) delete result->lasty;
        result->lastx = nullptr;
        result->lasty = yviterbi;
        result->y = a->states[y];
        result->probability = overall;
      } else delete yviterbi;
    }
  } else if (t < a->xobs.size()) {
    result->lastx = nullptr;
    result->direction = HMM2D::Direction::Y;
    for (size_t x = 0; x < a->states.size(); ++x) {
      Viterbi2DResult *xviterbi = Viterbi2D(a, t - 1, a->states[x]);
      double overall = prob*a->yinitial[a->state_map[k]]*a->xtransition[x*a->states.size() + a->state_map[k]]*xviterbi->probability;
      if (overall > max) {
        if (result->lastx) delete result->lastx;
        result->lasty = nullptr;
        result->lastx = xviterbi;
        result->x = a->states[x];
        result->probability = overall;
      } else delete xviterbi;
    }
  } else {
    for (size_t x = 0; x < a->states.size(); ++x) {
      for (size_t y = 0; y < a->states.size(); ++y) {
        Viterbi2DResult *xviterbi = Viterbi2D(a, t - 1, a->states[x]);
        Viterbi2DResult *yviterbi = Viterbi2D(a, t - a->xobs.size(), a->states[y]);
        double xprob = a->xtransition[a->states.size()*x + a->state_map[k]]*xviterbi->probability;
        double yprob = a->ytransition[a->states.size()*y + a->state_map[k]]*yviterbi->probability;
        double overall = prob*xprob*yprob;
        if (overall > max) {
          result->probability = overall;
          result->x = a->states[x];
          result->y = a->states[y];
          result->lastx = xviterbi;
          result->lasty = yviterbi;
          if (xprob > yprob) {
            result->direction = HMM2D::Direction::X;
          } else {
            result->direction = HMM2D::Direction::Y;
          }
        } else {
          delete xviterbi;
          delete yviterbi;
        }
      }
    }
  }
  return result;
}

HMM2D::Ptr HMM2D::New() {
  return HMM2D::Ptr(new HMM2D());
}

vector<double> &HMM2D::GetTransition(HMM2D::Direction d) {
  if (d == HMM2D::Direction::X) {
    return xtransition;
  } else return ytransition;
}

vector<double> &HMM2D::GetInitial(HMM2D::Direction d) {
  if (d == HMM2D::Direction::X) {
    return xinitial;
  } else return yinitial;
}

vector<HMM2D::Observation> &HMM2D::GetObservations(HMM2D::Direction d) {
  if (d == HMM2D::Direction::X) return xobs;
  else return yobs;
}

Permutation *EMMax(HMM2D::Ptr a, HMM2D::Direction d, size_t len, double threshold) {
  Permutation *result = new Permutation();
  result->probability = 100;
  result->state = 255;
  for (size_t i = 0; i < a->states.size(); ++i) {
    Permutation *previous = EM(a, d, len, a->states[i], threshold);
    if (previous->probability < threshold) {}
    else result->last.push_back(previous);
  }
  return result;
}

Permutation *EM(HMM2D::Ptr a, HMM2D::Direction d, size_t len, HMM2D::PartialState s, double threshold) {
  Permutation *result;
  if (!len) {
    result = new Permutation();
    result->state = s;
    result->probability = a->GetInitial(d)[a->state_map[s]];
    return result;
  }
  double max = 0;
  for (size_t i = 0; i < a->states.size(); i++) {
    result = new Permutation();
    Permutation *previous = EM(a, d, len - 1, a->states[i], threshold);
    result->probability = a->GetTransition(d)[i*a->states.size() + a->state_map[s]] * previous->probability;
    if (result->probability > max) {
      max = result->probability;
      result->state = s;
    }
    if (result->probability < threshold) {
      delete previous;
    }
    else result->last.push_back(previous);
  }
  result->probability = max;
  sort(result->last.begin(), result->last.end(), [] (Permutation *a, Permutation *b) -> bool {
    if (a->probability > b->probability) return true;
    else return false;
  });
  return result;
}


ViterbiResult::ViterbiResult(ViterbiResult *plast, HMM::State *pptr, double pprobability) : last(plast), ptr(pptr), probability(pprobability) {}
ViterbiResult::~ViterbiResult() { if (last) delete last; }

ViterbiResult *Viterbi(HMM::Ptr m, const vector<HMM::Observation> &obs, size_t len, HMM::State *s, ViterbiResult *last) {
  size_t idx = s - &m->states[0];
  if (len == 0) {
    return new ViterbiResult(nullptr, s, m->emit[idx*m->obs.size() + obs[0]]*m->initial[idx]);
  }
  double max = 0;
  HMM::State *state = nullptr;
  ViterbiResult *previous = nullptr;
  for (auto it = m->states.begin(); it != m->states.end(); it++) {
    auto idx = distance(m->states.begin(), it);
    ViterbiResult *result = Viterbi(m, obs, len - 1, &*it, last);
    double prob = m->emit[idx*m->obs.size() + obs[len]]*m->matrix[distance(m->states.begin(), it)*m->states.size() + distance(m->states.begin(), find(m->states.begin(), m->states.end(), *result->ptr))] * result->probability;
    if (!previous) {
      max = prob;
      state = &*it;
      previous = result;
    } else if (prob > max) {
      delete previous;
      max = prob;
      state = &*it;
      previous = result;
    } else {
      delete result;
    }
  }
  return new ViterbiResult(previous, state, max);
}

ViterbiResult *ViterbiMax(HMM::Ptr m, vector<HMM::Observation> &obs) {
  for (auto it = obs.begin(); it != obs.end(); it++) {
    *it = distance(m->obs.begin(), find(m->obs.begin(), m->obs.end(), *it));
  }
  return ViterbiMax(m, obs, obs.size() - 1, nullptr);
}

ViterbiResult *ViterbiMax(HMM::Ptr m, const vector<HMM::Observation> &obs, size_t len, ViterbiResult *last) {
  double max = 0;
  HMM::State *s = nullptr;
  ViterbiResult *previous = nullptr;
  ViterbiResult *retval = nullptr;
  for (auto it = m->states.begin(); it != m->states.end(); it++) {
    ViterbiResult *result = Viterbi(m, obs, len - 1, &*it, last);
    double prob = m->emit[distance(m->states.begin(), it)*m->obs.size() + obs[len]]*m->matrix[distance(m->states.begin(), it)*m->states.size() + distance(m->states.begin(), find(m->states.begin(), m->states.end(), *result->ptr))] * result->probability;
    if (!previous) {
      max = prob;
      s = &*it;
      previous = result;
    } else if (prob > max) {
      delete previous;
      max = prob;
      s = &*it;
      previous = result;
    } else {
      delete result;
    }
  }
  return new ViterbiResult(previous, s, max);
}

json_object *StateToJsonObject(HMM::State s) {
  json_object *retval = json_object_new_object();
  json_object_object_add(retval, "density", json_object_new_int(s.first));
  json_object_object_add(retval, "duration", json_object_new_int(s.second));
  return retval;
}

void PrintViterbiResult(ViterbiResult *vr) {
  json_object *result = json_object_new_array();
  while (vr) {
    json_object_array_add(result, StateToJsonObject(*vr->ptr));
    vr = vr->last;
  }
  cout << json_object_to_json_string(result) << endl;
  json_object_put(result);
}

template <int format> int PNG<format>::GetWidth() {
  return img.width;
}
template <int format> int PNG<format>::GetHeight() {
  return img.height;
}

template <int format> typename PNG<format>::Pixel *PNG<format>::GetPixelArray() {
  return (typename PNG<format>::Pixel *) buffer;
}

template class PNG<PNG_FORMAT_GA>;
template void Die<char const*, char const*>(char const*, char const*);

template HMMGroup::Ptr CalculateHMMGroup<PNG<1>::Pixel>(PNG<1>::Pixel*, unsigned long*);
template void Die<char const*>(char const*);
template void Die<char const*, double>(char const*, double);
template void Die<char const*, int, char*>(char const*, int, char*);
template void Die<char const*, int>(char const*, int);
template void ParseFilename<unsigned char>(char*, std::vector<char*, std::allocator<char*> >&, std::vector<unsigned char, std::allocator<unsigned char> >&);
template PNG<1>::Pixel* ZSlice<PNG<1>::Pixel>(PNG<1>::Pixel*, unsigned long, unsigned long, unsigned long);

Permutation::~Permutation() {
  for (auto it = last.begin(); it != last.end(); it++) {
    delete *it;
  }
}

template HMM2D::Ptr Calculate2DHMM<typename PNG<1>::Pixel>(typename PNG<1>::Pixel *, size_t *);

template <int format> void GenProjections(PNG<format> *png, vector<HMM2D::Observation> &xobs, vector<HMM2D::Observation> &yobs) {
  typename PNG<format>::Pixel *pixels = png->GetPixelArray();
  for (size_t i = 0; i < png->GetWidth(); ++i) {
    HMM2D::State tmp;
    for (size_t j = 0; j < png->GetHeight(); ++j) {
      tmp.push_back(pixels[i*png->GetHeight() + j].g);
    }
    yobs.push_back(SumThe2DState(tmp));
  }
  for (size_t j = 0; j < png->GetHeight(); ++j) {
    HMM2D::State tmp;
    for (size_t i = 0; i < png->GetWidth(); ++i) {
      tmp.push_back(pixels[i*png->GetHeight() + j].g);
    }
    xobs.push_back(SumThe2DState(tmp));
  }
}

template void GenProjections(PNG<PNG_FORMAT_GA> *, vector<HMM2D::Observation> &, vector<HMM2D::Observation> &);
/*

PNG<PNG_FORMAT_GA> *Reconstruct(HMM2D::Ptr a, Viterbi2DResult *result) {
  size_t x = a->xobs.size();
  size_t y = a->yobs.size();
  PNG<PNG_FORMAT_GA>::Pixel *pxls = new PNG<PNG_FORMAT_GA>::Pixel[x*y];
  while (result->last) {
    for (size_t i = 0; i < result->x.size(); i++) {
      if (result->x[i] == 0) {
        pxls[a->xobs.size()*i + y] = {0, 0};
      } else pxls[a->xobs.size()*i + y] = { result->x[i], 0xff };
    }
    for (size_t i = 0; i < result->y.size(); i++) {
      if (result->y[i] == 0) {
        pxls[a->xobs.size()*x + i] = {0, 0};
      } else pxls[a->xobs.size()*x + i] = { result->y[i], 0xff };
    }
    if (result->direction == HMM2D::Direction::X) {
      x--;
      result = result->last;
    } else if (result->direction == HMM2D::Direction::Y) {
      y--;
      result = result->last;
    }
  }
  if (!result->x.back()) {
    pxls[a->xobs.size()*x + y] = {0, 0};
  } else pxls[a->xobs.size()*x + y] = { result->x.back(), 0xff };
  return new PNG<PNG_FORMAT_GA>(a->xobs.size(), a->yobs.size(), pxls);
}
*/

void RowNormalize(HMM2D *a) {
  for (size_t i = 0; i < a->states.size(); ++i) {
    size_t sum = 0;
    size_t ysum = 0;
    for (size_t j = 0; j < a->states.size(); ++j) {
      sum += a->xtransitionwhole[i*a->states.size() + j];
      ysum += a->ytransitionwhole[i*a->states.size() + j];
    }
    for (size_t j = 0; j < a->states.size(); ++j) {
      a->xtransition[i*a->states.size() + j] = static_cast<double>(a->xtransitionwhole[i*a->states.size() + j])/sum;
      a->ytransition[i*a->states.size() + j] = static_cast<double>(a->ytransitionwhole[i*a->states.size() + j])/ysum;
    }
  }
} 

void Transpose(vector<size_t> &vec, size_t len, vector<size_t> &xsums, vector<size_t> &ysums) {
  size_t columns = vec.size()/len;
  for (size_t i = 0; i < len; ++i) {
    for (size_t j = 0; j < (len - i); ++j) {
      if (i == j) continue;
      size_t tmp = vec[i*len + j];
      vec[i*len + j] = vec[j*len + i]*xsums[j]/ysums[i];
      vec[j*len + i] = tmp*xsums[i]/ysums[j];
    }
  }
  vector<size_t> tmp = xsums;
  xsums = ysums;
  ysums = tmp;
}

void ExecutionCache::CalculateForAMatrix(HMM2D::Ptr a, double threshold) {
  Permutation *p = EMMax(a, HMM2D::Direction::X, 8, numeric_limits<double>::min());
  delete p;
}
void ExecutionCache::CalculateForAMatrixWithThreshold(HMM2D::Ptr a, double threshold) {
  Permutation *p = EMMax(a, HMM2D::Direction::X, 8, 1e-3);
  delete p;
}
static ExecutionCache cache;

void HMM2D::Rotate(double d) {
  vector<size_t> xy;
  size_t num_states = states.size();
  if (cos(d) < 0) Transpose(xtransitionwhole, num_states, xrowsums, xcolsums);
  if (sin(d) < 0) Transpose(ytransitionwhole, num_states, yrowsums, ycolsums);
  RowNormalize(this);
  vector<size_t> xyrowsum;
  vector<size_t> xycolsum;
  for (size_t i = 0; i < num_states; i++) {
    for (size_t j = 0; j < num_states; j++) {
      double sum = 0;
      for (size_t k = 0; k < num_states; k++) {
        sum += xtransition[num_states*i + k]*ytransition[num_states*k + j];
      }
      xy.push_back(sum);
      xyrowsum.push_back(cos(d)*xrowsums[i] + sin(d)*yrowsums[j]);
      xycolsum.push_back(cos(d)*xcolsums[i] + sin(d)*ycolsums[j]);
    }
  }
  vector<size_t> newx;
  vector<size_t> newy;
  vector<size_t> newxrowsums;
  vector<size_t> newxcolsums;
  vector<size_t> newyrowsums;
  vector<size_t> newycolsums;
  for (size_t i = 0; i < num_states; i++) {
    for (size_t j = 0; j < num_states; j++) {
      if (abs(cos(d)) < 1e-10) {
        newx.push_back(ytransitionwhole[i*num_states + j]);
      } else if (abs(sin(d)) < 1e-10) {
        newx.push_back(xtransitionwhole[i*num_states + j]);
      } else {
        double fy = ceil(abs(sin(d)/cos(d)) - 1);
        double fx = ceil(abs(cos(d)/sin(d)) - 1);
        newx.push_back((fy*ytransition[i*num_states + j] + fx*xtransitionwhole[i*num_states + j] + xy[i*num_states + j])/(fx + fy + 1));
      }
    }
    newxrowsums.push_back(cos(d)*xrowsums[i] + sin(d)*yrowsums[i]);
    newxcolsums.push_back(cos(d)*xcolsums[i] + sin(d)*ycolsums[i]);
  }
  d += M_PI/2;
  if ((cos(d - M_PI/2) < 0 && cos(d) > 0) || (cos(d) < 0 && cos(d - M_PI/2 > 0))) Transpose(xtransitionwhole, num_states, xrowsums, xcolsums);
  if ((sin(d - M_PI/2) < 0 && sin(d) > 0) || (sin(d) < 0 && sin(d - M_PI/2 > 0))) Transpose(ytransitionwhole, num_states, xrowsums, xcolsums);
  xy.clear();
  xycolsum.clear();
  xyrowsum.clear();
  for (size_t i = 0; i < num_states; i++) {
    for (size_t j = 0; j < num_states; j++) {
      double sum = 0;
      for (size_t k = 0; k < num_states; k++) {
        sum += xtransition[num_states*i + k]*ytransition[num_states*k + j];
      }
      xy.push_back(sum);
      xyrowsum.push_back(cos(d)*xrowsums[i] + sin(d)*yrowsums[j]);
      xycolsum.push_back(cos(d)*xcolsums[i] + sin(d)*ycolsums[j]);
    }
  }
  for (size_t i = 0; i < num_states; ++i) {
    for (size_t j = 0; j < num_states; ++j) {
      if (abs(cos(d)) < 1e-10) {
        newy.push_back(ytransitionwhole[i*num_states + j]);
      } else if (abs(sin(d)) < 1e-10) {
        newy.push_back(xtransitionwhole[i*num_states + j]);
      } else {
        double fy = ceil(abs(sin(d)/cos(d)) - 1);
        double fx = ceil(abs(cos(d)/sin(d)) - 1);
        newy.push_back((fy*ytransition[i*num_states + j] + fx*xtransition[i*num_states + j] + xy[i*num_states + j])/(fx + fy + 1));
      }
    }
    newyrowsums.push_back(cos(d)*xrowsums[i] + sin(d)*yrowsums[i]);
    newycolsums.push_back(cos(d)*xcolsums[i] + sin(d)*ycolsums[i]);
  }
  xrowsums = newxrowsums;
  xcolsums = newxcolsums;
  yrowsums = newyrowsums;
  ycolsums = newycolsums;
  xtransitionwhole = newx;
  ytransitionwhole = newy;
  RowNormalize(this);
}
HMM2D::Ptr Calculate2DHMMReverse(PNG<PNG_FORMAT_GA>::Pixel *items, size_t *coords) {
  HMM2D::Ptr retval = HMM2D::New();
  HMM2D::PartialState last_depth = 0;
  HMM2D::PartialState depth = 0;
  bool starting = true;
  bool initial = true;
  vector<HMM2D::PartialState> xinitial_states;
  vector<HMM2D::PartialTransition> xtransitions;
  vector<HMM2D::PartialState> yinitial_states;
  vector<HMM2D::PartialTransition> ytransitions;
  for (size_t i = coords[0] - 1; i != numeric_limits<size_t>::max(); --i) {
    for (size_t j = coords[1] - 1; j != numeric_limits<size_t>::max(); --j) {
      depth = items[i*coords[0] + j].GetValue();
      if (initial) xinitial_states.push_back(depth);
      else xtransitions.push_back(HMM2D::PartialTransition(last_depth, depth));
      retval->states.push_back(depth);
      last_depth = depth;
      initial = false;
    }
    initial = true;
  }
  for (size_t i = coords[1] - 1; i != numeric_limits<size_t>::max(); --i) {
    for (size_t j = coords[0] - 1; j != numeric_limits<size_t>::max(); --j) {
      depth = items[j*coords[0] + i].GetValue();
      if (initial) yinitial_states.push_back(depth);
      else ytransitions.push_back(HMM2D::PartialTransition(last_depth, depth));
      last_depth = depth;
      initial = false;
    }
    initial = true;
  }
  sort(retval->states.begin(), retval->states.end());
  auto it = unique(retval->states.begin(), retval->states.end());
  retval->states.resize(distance(retval->states.begin(), it));
  retval->xtransition.resize(retval->states.size() * retval->states.size());
  retval->ytransition.resize(retval->states.size() * retval->states.size());
  retval->xinitial.resize(retval->states.size());
  retval->yinitial.resize(retval->states.size());
  fill(retval->xtransition.begin(), retval->xtransition.end(), 0);
  fill(retval->ytransition.begin(), retval->ytransition.end(), 0);
  fill(retval->xinitial.begin(), retval->xinitial.end(), 0);
  fill(retval->yinitial.begin(), retval->yinitial.end(), 0);
  for (auto it = retval->states.begin(); it != retval->states.end(); it++) {
    retval->state_map[*it] = distance(retval->states.begin(), it);
  }
  for (auto it = xtransitions.begin(); it != xtransitions.end(); it++) {
    retval->xtransition[retval->state_map[it->first]*retval->states.size() + retval->state_map[it->second]]++;
  }
  for (size_t i = 0; i < retval->states.size(); ++i) {
    double sum = 0;
    for (size_t j = 0; j < retval->states.size(); ++j) {
      sum += retval->xtransition[i*retval->states.size() + j];
    }
    if (sum) for (size_t j = 0; j < retval->states.size(); ++j) {
      retval->xtransition[i*retval->states.size() + j] /= sum;
    }
  }
  for (auto it = xinitial_states.begin(); it != xinitial_states.end(); it++) {
    retval->xinitial[retval->state_map[*it]]++;
  }
  double sum = 0;
  for (size_t i = 0; i < retval->states.size(); ++i) {
    sum += retval->xinitial[i];
  }
  if (sum) for (size_t i = 0; i < retval->states.size(); ++i) {
    retval->xinitial[i] /= sum;
  }
  for (auto it = ytransitions.begin(); it != ytransitions.end(); it++) {
    retval->ytransition[retval->state_map[it->first]*retval->states.size() + retval->state_map[it->second]]++;
  }
  for (size_t i = 0; i < retval->states.size(); ++i) {
    double sum = 0;
    for (size_t j = 0; j < retval->states.size(); ++j) {
      sum += retval->ytransition[i*retval->states.size() + j];
    }
    if (sum) for (size_t j = 0; j < retval->states.size(); ++j) {
      retval->ytransition[i*retval->states.size() + j] /= sum;
    }
  }
  for (auto it = yinitial_states.begin(); it != yinitial_states.end(); it++) {
    retval->yinitial[retval->state_map[*it]]++;
  }
  sum = 0;
  for (size_t i = 0; i < retval->states.size(); ++i) {
    sum += retval->yinitial[i];
  }
  if (sum) for (size_t i = 0; i < retval->states.size(); ++i) {
    retval->yinitial[i] /= sum;
  }
  return retval;
}

hmm2d_t *HMM2DToC(HMM2D::Ptr a) {
  size_t i, j;
  hmm2d_t *retval = init_hmm2d();
  retval->n = a->states.size();
  retval->states = (size_t *) calloc(retval->n, sizeof(size_t));
  retval->pix = init_vector(retval->n);
  retval->piy = init_vector(retval->n);
  for (i = 0; i < retval->n; ++i) {
    retval->states[i] = a->states[i];
    vector_push(retval->pix, (long double) a->xinitial[i]);
    vector_push(retval->piy, (long double) a->yinitial[i]);
  }
  retval->ax = init_matrix(retval->n, retval->n);
  retval->ay = init_matrix(retval->n, retval->n);
  for (i = 0; i < retval->n; ++i) {
    for (j = 0; j < retval->n; ++j) {
      *matrix_el(retval->ax, i, j) = a->xtransition[i*retval->n + j];
      *matrix_el(retval->ay, i, j) = a->ytransition[i*retval->n + j];
    }
  }
  retval->xobs = init_obs_vector(a->xobs.size());
  for (i = 0; i < a->xobs.size(); ++i) {
    obs_vector_push(retval->xobs, a->xobs[i]);
  }
  retval->yobs = init_obs_vector(a->yobs.size());
  for (i = 0; i < a->yobs.size(); ++i) {
    obs_vector_push(retval->yobs, a->yobs[i]);
  }
  return retval;
}

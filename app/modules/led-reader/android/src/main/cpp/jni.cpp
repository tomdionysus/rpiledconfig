//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
// The JNI side of LedReaderPlugin.kt: one Tracker per plugin instance, fed the Y plane of
// each camera frame straight from its direct ByteBuffer, on the camera's thread.
#include <jni.h>

#include <cstdint>

#include "tracker.h"

using rpiledconfig::reader::Plane;
using rpiledconfig::reader::Tracker;

namespace {

// The layout of the state array LedReaderPlugin.kt reads; keep the two in step.
enum Field {
  led_seen,
  coverage,
  contrast,
  level,
  tries,
  frames_failed,
  erasures,
  read,
  errors,
  filled,
  frames_combined,
  has_tile,
  tile_x,
  tile_y,
  tile_width,
  tile_height,
  plane_width,
  plane_height,
  candidates,
  sample,
  has_spot,
  spot_x,
  spot_y,
  id_first,
  field_count = id_first + 8,
};

}  // namespace

extern "C" {

// `symbol_ms` is the T of the rate the phone asked the Pi for.
JNIEXPORT jlong JNICALL Java_com_tomcully_ledreader_LedReaderPlugin_nativeCreate(JNIEnv*, jclass, jdouble symbol_ms) {
  rpiledconfig::reader::TrackerConfig config;
  if (symbol_ms > 0) config.symbol = symbol_ms;
  return reinterpret_cast<jlong>(new Tracker(config));
}

JNIEXPORT void JNICALL Java_com_tomcully_ledreader_LedReaderPlugin_nativeDestroy(JNIEnv*, jclass, jlong handle) {
  delete reinterpret_cast<Tracker*>(handle);
}

JNIEXPORT jint JNICALL Java_com_tomcully_ledreader_LedReaderPlugin_nativeFieldCount(JNIEnv*, jclass) { return field_count; }

JNIEXPORT jboolean JNICALL Java_com_tomcully_ledreader_LedReaderPlugin_nativePush(JNIEnv* env, jclass, jlong handle, jdouble t_ms, jobject buffer, jint width,
                                                                                 jint height, jint row_stride, jint pixel_stride, jdoubleArray out) {
  auto* tracker = reinterpret_cast<Tracker*>(handle);
  const auto* data = static_cast<const std::uint8_t*>(env->GetDirectBufferAddress(buffer));
  const jlong capacity = env->GetDirectBufferCapacity(buffer);
  if (tracker == nullptr || data == nullptr || capacity < static_cast<jlong>(row_stride) * (height - 1) + static_cast<jlong>(width) * pixel_stride) return JNI_FALSE;
  if (env->GetArrayLength(out) < field_count) return JNI_FALSE;

  const auto& s = tracker->push(t_ms, Plane{data, width, height, row_stride, pixel_stride});
  double v[field_count] = {};
  v[led_seen] = s.reader.led_seen ? 1 : 0;
  v[coverage] = s.reader.coverage;
  v[contrast] = s.reader.contrast;
  v[level] = s.reader.level ? 1 : 0;
  v[tries] = s.reader.tries;
  v[frames_failed] = s.reader.frames_failed;
  v[erasures] = s.reader.erasures;
  v[read] = s.reader.result ? 1 : 0;
  if (s.reader.result) {
    v[errors] = s.reader.result->errors;
    v[filled] = s.reader.result->erasures;
    v[frames_combined] = s.reader.frames_combined;
    for (int i = 0; i < 8; ++i) v[id_first + i] = s.reader.result->id[static_cast<std::size_t>(i)];
  }
  if (s.tile) {
    v[has_tile] = 1;
    v[tile_x] = s.tile->x;
    v[tile_y] = s.tile->y;
    v[tile_width] = s.tile->width;
    v[tile_height] = s.tile->height;
  }
  v[plane_width] = s.plane_width;
  v[plane_height] = s.plane_height;
  v[candidates] = s.candidates;
  v[sample] = s.sample;
  if (s.spot) {
    v[has_spot] = 1;
    v[spot_x] = s.spot->x;
    v[spot_y] = s.spot->y;
  }
  env->SetDoubleArrayRegion(out, 0, field_count, v);
  return JNI_TRUE;
}

}  // extern "C"

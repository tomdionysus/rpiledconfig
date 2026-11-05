// RPiLEDConfig - app. Copyright (C) 2026 T. Cully. Licensed under the Apache License, Version 2.0 - see LICENSE.
//
// The VisionCamera frame-processor plugin "ledReader": each call hands the frame's Y plane to
// the C++ tracker and reader (cpp/, through jni.cpp) and returns their state as a map. One
// native tracker per plugin instance, so a scan screen that initialises its own plugin starts
// clean. Needs the camera in the yuv pixel format, whose first plane is luminance.
package com.tomcully.ledreader

import com.mrousavy.camera.frameprocessors.Frame
import com.mrousavy.camera.frameprocessors.FrameProcessorPlugin
import com.mrousavy.camera.frameprocessors.VisionCameraProxy
import java.nio.ByteBuffer

class LedReaderPlugin(@Suppress("UNUSED_PARAMETER") proxy: VisionCameraProxy, options: Map<String, Any>?) : FrameProcessorPlugin() {
  // The T of the rate the phone asked the Pi for.
  private var handle: Long = nativeCreate((options?.get("symbolMs") as? Number)?.toDouble() ?: 0.0)
  private val out = DoubleArray(nativeFieldCount())

  override fun callback(frame: Frame, params: Map<String, Any>?): Any? {
    val image = frame.image
    val plane = image.planes[0]
    // Frame timestamps on Android are nanoseconds.
    val tMs = frame.timestamp / 1e6
    if (!nativePush(handle, tMs, plane.buffer, image.width, image.height, plane.rowStride, plane.pixelStride, out)) return null

    val state = HashMap<String, Any>()
    state["t"] = tMs
    state["ledSeen"] = out[0] != 0.0
    state["coverage"] = out[1]
    state["contrast"] = out[2]
    state["level"] = out[3] != 0.0
    state["tries"] = out[4]
    state["framesFailed"] = out[5]
    state["erasures"] = out[6]
    if (out[7] != 0.0) {
      val id = StringBuilder()
      for (i in 0 until 8) id.append(String.format("%02x", out[23 + i].toInt()))
      state["idHex"] = id.toString()
      state["errors"] = out[8]
      state["filled"] = out[9]
      state["framesCombined"] = out[10]
    }
    if (out[11] != 0.0) {
      state["tileX"] = out[12]
      state["tileY"] = out[13]
      state["tileWidth"] = out[14]
      state["tileHeight"] = out[15]
    }
    state["planeWidth"] = out[16]
    state["planeHeight"] = out[17]
    state["candidates"] = out[18]
    state["sample"] = out[19]
    if (out[20] != 0.0) {
      state["spotX"] = out[21]
      state["spotY"] = out[22]
    }
    state["orientation"] = frame.orientation.unionValue
    return state
  }

  protected fun finalize() {
    if (handle != 0L) nativeDestroy(handle)
    handle = 0L
  }

  companion object {
    init {
      System.loadLibrary("ledreader")
    }

    @JvmStatic private external fun nativeCreate(symbolMs: Double): Long

    @JvmStatic private external fun nativeDestroy(handle: Long)

    @JvmStatic private external fun nativeFieldCount(): Int

    @JvmStatic private external fun nativePush(
      handle: Long,
      tMs: Double,
      buffer: ByteBuffer,
      width: Int,
      height: Int,
      rowStride: Int,
      pixelStride: Int,
      out: DoubleArray,
    ): Boolean
  }
}

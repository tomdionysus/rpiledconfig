// RPiLEDConfig - app. Copyright (C) 2026 T. Cully. Licensed under the Apache License, Version 2.0 - see LICENSE.
//
// Registers the "ledReader" frame-processor plugin with VisionCamera. The application calls
// this at start, before any JS looks the plugin up.
package com.tomcully.ledreader

import com.mrousavy.camera.frameprocessors.FrameProcessorPluginRegistry

object LedReaderRegistration {
  private var registered = false

  @Synchronized
  fun register() {
    if (registered) return
    FrameProcessorPluginRegistry.addFrameProcessorPlugin("ledReader") { proxy, options -> LedReaderPlugin(proxy, options) }
    registered = true
  }
}
